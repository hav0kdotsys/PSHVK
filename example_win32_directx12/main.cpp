// Dear ImGui: standalone example application for Windows API + DirectX 11 & 12

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_IMPLEMENTATION

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
// #pragma comment(lib, "d3d11.lib")

#include <directx/d3d12.h>
#include <dxgi1_5.h>

#include <tchar.h>
#include <chrono>

#include "util/system.h"
#include "settings.h"
#include "custom_widgets.h"
#include "hvk_gui.h"
#include "util/texhelper.h"
#include "util/disk.h"
#include "util/web_helper.h"
#include "util/theme_helper.h"
#include "glow_pipeline.h"

#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include <cstdarg>
#include <cstdio>

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

#include <vector>
#include <atomic>
#include <mutex>

#ifdef _DEBUG
static void DebugLog(const char* fmt, ...)
{
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        OutputDebugStringA(buffer);
        OutputDebugStringA("\n");
        printf("%s\n", buffer);

		FILE* logFile = nullptr;
		fopen_s(&logFile, "debug.hvklog", "a");
		if (logFile)
		{
			fprintf(logFile, "%s\n", buffer);
			fclose(logFile);
		}
}
#else
#define DebugLog(...) (void)0
#endif

struct PendingFrame
{
        std::vector<unsigned char> bytes;
};

static std::mutex g_texMutex;
static std::vector<PendingFrame> g_pendingFrames;
static std::atomic<bool> g_texturesReady{ false };
static std::atomic<bool> g_texStop{ false };

struct BgReloadJob
{
	std::atomic<bool> requested{ false };
	std::atomic<bool> bytes_ready{ false };
	std::atomic<bool> upload_submitted{ false };

	std::mutex mtx;
	std::wstring path;
	std::vector<unsigned char> bytes;

	// DX12 tracking
	ImTextureID new_tex = (ImTextureID)nullptr;
	UINT64 fence_value = 0;

	// DX12 resources for the new texture (upload buffer released after fence)
	ID3D12Resource* new_texture_res = nullptr;
	ID3D12Resource* new_upload_res = nullptr;

	D3D12_CPU_DESCRIPTOR_HANDLE new_cpu = { 0 };
	D3D12_GPU_DESCRIPTOR_HANDLE new_gpu = { 0 };

};

static BgReloadJob g_bgJob;




// Config for example app
static const int APP_NUM_FRAMES_IN_FLIGHT = 2;
static const int APP_NUM_BACK_BUFFERS = 2;
static const int APP_SRV_HEAP_SIZE = 64;

static ResolutionUI g_ResUI;
static WatermarkStats wmStats;
static HVKSYS g_Sys;

ImTextureID BgTexture = (ImTextureID)nullptr;
HVKTexture bg{};
std::vector<HVKTexture> g_LoadingFrames;
std::vector<ImTextureID> FrameTextures;
DiskSelection sel{};
FPSLimiter g_fpsLimiter;
AppState g_App;

std::thread texThread;
std::thread tLoadingAnim;

void RefreshDisks()
{
	g_App.PhysicalDisks.clear();
	g_App.Volumes.clear();
	g_App.Partitions.clear();

	for (int i = 0; i < 32; i++)
	{
		DiskInfo info{};
		if (Disk::GetDiskInfo(i, info))
			g_App.PhysicalDisks.push_back(info);
	}

	g_App.Volumes = Disk::ListVolumes();

	g_App.NeedsRefresh = false;
}


bool Disk::IsValidIndex(int idx, int size)
{
	return idx >= 0 && idx < size;
}

void Disk::RefreshPartitionsForSelectedDisk()
{
	if (!IsValidIndex(
		g_App.Selection.PhysicalIndex,
		(int)g_App.PhysicalDisks.size()))
	{
		g_App.Partitions.clear();
		return;
	}

	g_App.Partitions =
		Disk::ListPartitions(g_App.Selection.PhysicalIndex);
}


struct FrameContext
{
	ID3D12CommandAllocator* CommandAllocator;
	UINT64                      FenceValue;
};

// Simple free list based allocator
struct ExampleDescriptorHeapAllocator
{
	ID3D12DescriptorHeap* Heap = nullptr;
	D3D12_DESCRIPTOR_HEAP_TYPE  HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
	D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu;
	D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu;
	UINT                        HeapHandleIncrement;
	ImVector<int>               FreeIndices;

	void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
	{
		IM_ASSERT(Heap == nullptr && FreeIndices.empty());
		Heap = heap;
		D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
		HeapType = desc.Type;
		HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
		HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
		HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(HeapType);
		FreeIndices.reserve((int)desc.NumDescriptors);
		for (int n = desc.NumDescriptors; n > 0; n--)
			FreeIndices.push_back(n - 1);
	}
	void Destroy()
	{
		Heap = nullptr;
		FreeIndices.clear();
	}
	void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
	{
		IM_ASSERT(FreeIndices.Size > 0);
		int idx = FreeIndices.back();
		FreeIndices.pop_back();
		out_cpu_desc_handle->ptr = HeapStartCpu.ptr + (idx * HeapHandleIncrement);
		out_gpu_desc_handle->ptr = HeapStartGpu.ptr + (idx * HeapHandleIncrement);
	}
	void Free(D3D12_CPU_DESCRIPTOR_HANDLE out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE out_gpu_desc_handle)
	{
		int cpu_idx = (int)((out_cpu_desc_handle.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
		int gpu_idx = (int)((out_gpu_desc_handle.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
		IM_ASSERT(cpu_idx == gpu_idx);
		FreeIndices.push_back(cpu_idx);
	}
};

// Simple helper function to load an image into a DX12 texture with common settings
// Returns true on success, with the SRV CPU handle having an SRV for the newly-created texture placed in it (srv_cpu_handle must be a handle in a valid descriptor heap)
bool LoadTextureFromMemory(const void* data, size_t data_size, ID3D12Device* d3d_device, D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_handle, ID3D12Resource** out_tex_resource, int* out_width, int* out_height)
{
        DebugLog("LoadTextureFromMemory: start (data_size=%zu)", data_size);
        // Load from disk into a raw RGBA buffer
        int image_width = 0;
        int image_height = 0;
        unsigned char* image_data = stbi_load_from_memory((const unsigned char*)data, (int)data_size, &image_width, &image_height, NULL, 4);
        if (image_data == NULL)
        {
                DebugLog("LoadTextureFromMemory: stbi_load_from_memory failed");
                return false;
        }

        DebugLog("LoadTextureFromMemory: loaded image %dx%d", image_width, image_height);
        // Create texture resource
        D3D12_HEAP_PROPERTIES props;
        memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
	props.Type = D3D12_HEAP_TYPE_DEFAULT;
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	D3D12_RESOURCE_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Alignment = 0;
	desc.Width = image_width;
	desc.Height = image_height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ID3D12Resource* pTexture = NULL;
        d3d_device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COPY_DEST, NULL, IID_PPV_ARGS(&pTexture));

        // Create a temporary upload resource to move the data in
        UINT uploadPitch = (image_width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
        UINT uploadSize = image_height * uploadPitch;
        DebugLog("LoadTextureFromMemory: created texture resource and upload buffer (pitch=%u size=%u)", uploadPitch, uploadSize);
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = 0;
        desc.Width = uploadSize;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	props.Type = D3D12_HEAP_TYPE_UPLOAD;
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	ID3D12Resource* uploadBuffer = NULL;
	HRESULT hr = d3d_device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&uploadBuffer));
	IM_ASSERT(SUCCEEDED(hr));

	// Write pixels into the upload resource
        void* mapped = NULL;
        D3D12_RANGE range = { 0, uploadSize };
        hr = uploadBuffer->Map(0, &range, &mapped);
        IM_ASSERT(SUCCEEDED(hr));
        for (int y = 0; y < image_height; y++)
                memcpy((void*)((uintptr_t)mapped + y * uploadPitch), image_data + y * image_width * 4, image_width * 4);
        uploadBuffer->Unmap(0, &range);

        DebugLog("LoadTextureFromMemory: copied image data to upload buffer");

        // Copy the upload resource content into the real resource
        D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
        srcLocation.pResource = uploadBuffer;
	srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	srcLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srcLocation.PlacedFootprint.Footprint.Width = image_width;
	srcLocation.PlacedFootprint.Footprint.Height = image_height;
	srcLocation.PlacedFootprint.Footprint.Depth = 1;
	srcLocation.PlacedFootprint.Footprint.RowPitch = uploadPitch;

	D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
	dstLocation.pResource = pTexture;
	dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstLocation.SubresourceIndex = 0;

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = pTexture;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	// Create a temporary command queue to do the copy with
	ID3D12Fence* fence = NULL;
	hr = d3d_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	IM_ASSERT(SUCCEEDED(hr));

	HANDLE event = CreateEvent(0, 0, 0, 0);
	IM_ASSERT(event != NULL);

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.NodeMask = 1;

	ID3D12CommandQueue* cmdQueue = NULL;
	hr = d3d_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue));
	IM_ASSERT(SUCCEEDED(hr));

	ID3D12CommandAllocator* cmdAlloc = NULL;
	hr = d3d_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
	IM_ASSERT(SUCCEEDED(hr));

	ID3D12GraphicsCommandList* cmdList = NULL;
        hr = d3d_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, NULL, IID_PPV_ARGS(&cmdList));
        IM_ASSERT(SUCCEEDED(hr));

        cmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, NULL);
        cmdList->ResourceBarrier(1, &barrier);

        DebugLog("LoadTextureFromMemory: command list recording finished");

        hr = cmdList->Close();
        IM_ASSERT(SUCCEEDED(hr));

        // Execute the copy
        cmdQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&cmdList);
        hr = cmdQueue->Signal(fence, 1);
        IM_ASSERT(SUCCEEDED(hr));

        DebugLog("LoadTextureFromMemory: copy submitted to GPU");

        // Wait for everything to complete
        fence->SetEventOnCompletion(1, event);
        WaitForSingleObject(event, INFINITE);

        DebugLog("LoadTextureFromMemory: GPU copy completed");

        // Tear down our temporary command queue and release the upload resource
        cmdList->Release();
        cmdAlloc->Release();
	cmdQueue->Release();
	CloseHandle(event);
	fence->Release();
	uploadBuffer->Release();

	// Create a shader resource view for the texture
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	d3d_device->CreateShaderResourceView(pTexture, &srvDesc, srv_cpu_handle);

	// Return results
        *out_tex_resource = pTexture;
        *out_width = image_width;
        *out_height = image_height;
        stbi_image_free(image_data);

        DebugLog("LoadTextureFromMemory: finished successfully");
        return true;
}

bool LoadTextureFromFile(const char* file_name, ID3D12Device* d3d_device, D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_handle, ID3D12Resource** out_tex_resource, int* out_width, int* out_height)
{
        DebugLog("LoadTextureFromFile: start (%s)", file_name);
        FILE* f = fopen(file_name, "rb");
        if (f == NULL)
        {
                DebugLog("LoadTextureFromFile: failed to open file");
                return false;
        }
        fseek(f, 0, SEEK_END);
        size_t file_size = (size_t)ftell(f);
        if (file_size == -1)
        {
                DebugLog("LoadTextureFromFile: failed to get file size");
                return false;
        }
        fseek(f, 0, SEEK_SET);
        void* file_data = IM_ALLOC(file_size);
        fread(file_data, 1, file_size, f);
        fclose(f);
        bool ret = LoadTextureFromMemory(file_data, file_size, d3d_device, srv_cpu_handle, out_tex_resource, out_width, out_height);
        IM_FREE(file_data);

        DebugLog("LoadTextureFromFile: completed with result=%s", ret ? "true" : "false");
        return ret;
}


// Data
struct DX12LoadingSrv
{
	D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
	D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
};

static std::vector<DX12LoadingSrv> g_dx12LoadingSrvs;
static std::mutex g_dx12SrvMutex;


static FrameContext                 g_frameContext[APP_NUM_FRAMES_IN_FLIGHT] = {};
static UINT                         g_frameIndex = 0;

static ID3D12Device* g_pd3dDevice = nullptr;
static ID3D12DescriptorHeap* g_pd3dRtvDescHeap = nullptr;
static ID3D12DescriptorHeap* g_pd3dSrvDescHeap = nullptr;
ExampleDescriptorHeapAllocator g_pd3dSrvDescHeapAlloc;
static ID3D12CommandQueue* g_pd3dCommandQueue = nullptr;
static ID3D12GraphicsCommandList* g_pd3dCommandList = nullptr;
static ID3D12Fence* g_fence = nullptr;
static HANDLE                       g_fenceEvent = nullptr;
static UINT64                       g_fenceLastSignaledValue = 0;
static IDXGISwapChain3* g_pSwapChain = nullptr;
static bool                         g_SwapChainTearingSupport = false;
static bool                         g_SwapChainOccluded = false;
static HANDLE                       g_hSwapChainWaitableObject = nullptr;
static ID3D12Resource* g_mainRenderTargetResource[APP_NUM_BACK_BUFFERS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE  g_mainRenderTargetDescriptor[APP_NUM_BACK_BUFFERS] = {};

// Dedicated DX12 upload objects (used ONLY for background texture uploads)
static ID3D12CommandAllocator* g_pd3dUploadCmdAlloc = nullptr;
static ID3D12GraphicsCommandList* g_pd3dUploadCmdList = nullptr;
static std::mutex                 g_dx12UploadMutex;

// Keep DX12 texture resources alive (your current code never releases them anyway)
static std::vector<ID3D12Resource*> g_dx12LiveTextures;

static ID3D11Device* g_pd3dDevice11 = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext11 = nullptr;
static IDXGISwapChain* g_pSwapChain11 = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView11 = nullptr;
static GlowPipelineDX12 g_GlowPipeline12;
static GlowPipelineDX11 g_GlowPipeline11;
static GlowSettings g_GlowSettings;

static bool LoadTextureUnified(const wchar_t* path, HVKTexture& outTex)
{
        if (g_App.g_RenderBackend == RenderBackend::DX12)
        {
                return TextureLoader::LoadTexture(
                        path,
                        g_pd3dDevice,
                        g_pd3dCommandList,
                        g_pd3dSrvDescHeapAlloc,
                        outTex
                );
        }
        else
        {
                return TextureLoader::LoadTextureDX11FromFile(
                        g_pd3dDevice11,
                        g_pd3dDeviceContext11,
                        path,
                        outTex
                );
        }
}


// Forward declarations of helper functions
void ApplyUserStyle();
void ApplyRenderSettings();

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void WaitForPendingOperations();
FrameContext* WaitForNextFrameContext();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static std::wstring ToWString(const std::string& s)
{
	if (s.empty())
		return L"";

	int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
	std::wstring result(sizeNeeded, L'\0');

	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &result[0], sizeNeeded);

	return result;
}

static const char* ToConstChar(const std::string& s)
{
	return s.c_str();
}

#include <string>
#include <windows.h>
#include <iostream>
#include <filesystem>

static std::string WStringToUtf8(const std::wstring& w)
{
	if (w.empty())
		return {};

	int size = WideCharToMultiByte(
		CP_UTF8,
		0,
		w.data(),
		(int)w.size(),
		nullptr,
		0,
		nullptr,
		nullptr);

	std::string result(size, 0);

	WideCharToMultiByte(
		CP_UTF8,
		0,
		w.data(),
		(int)w.size(),
		result.data(),
		size,
		nullptr,
		nullptr);

	return result;
}

static float PtrToFloat(const float* value, float fallback = 0.0f)
{
	return value ? *value : fallback;
}



static bool IsFirstRun()
{
	// Check for existence of instance file
	std::wstring base = HVKIO::GetLocalAppDataW();
	std::wstring file = base + L"\\PSHVK\\instance.hvk";
	bool status = std::filesystem::exists(file);


	return !status; // if file does not exist, it's first run
}

void TextureLoader::DeferFreeTexture(const HVKTexture& tex)
{
	g_deferredFrees.push_back({
		tex,
		g_fenceLastSignaledValue
		});
}

void TextureLoader::ProcessDeferredTextureFrees()
{
	UINT64 completed = g_fence->GetCompletedValue();

	for (auto it = g_deferredFrees.begin(); it != g_deferredFrees.end();)
	{
		if (completed >= it->fenceValue)
		{
			TextureLoader::FreeTexture(it->tex, g_App);
			it = g_deferredFrees.erase(it);
		}
		else
		{
			++it;
		}
	}
}

bool TextureLoader::ReloadBackgroundTexture(const std::wstring& newPath)
{
        if (g_App.g_RenderBackend == RenderBackend::DX11)
        {
                if (BgTexture)
                {
                        TextureLoader::FreeTexture(bg, g_App);
                        bg = {};
                        BgTexture = (ImTextureID)nullptr;
                }

                HVKTexture newBg{};
                if (!LoadTextureUnified(newPath.c_str(), newBg))
                        return false;

                bg = newBg;
                BgTexture = bg.id;
                return true;
        }

	// ---------- DX12 ----------

	// Defer old texture free
        if (BgTexture)
        {
                TextureLoader::DeferFreeTexture(bg);
                bg = {};
                BgTexture = (ImTextureID)nullptr;
        }

	// Dedicated upload objects
	ID3D12CommandAllocator* alloc = nullptr;
	ID3D12GraphicsCommandList* list = nullptr;

	g_pd3dDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&alloc)
	);

	g_pd3dDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		alloc,
		nullptr,
		IID_PPV_ARGS(&list)
	);

        HVKTexture newTex{};
        if (!TextureLoader::LoadTexture(
                newPath.c_str(),
                g_pd3dDevice,
                list,
                g_pd3dSrvDescHeapAlloc,
                newTex))
                return false;

	list->Close();
	ID3D12CommandList* lists[] = { list };
	g_pd3dCommandQueue->ExecuteCommandLists(1, lists);

	g_pd3dCommandQueue->Signal(g_fence, ++g_fenceLastSignaledValue);

	// Wait only for upload completion
	g_fence->SetEventOnCompletion(g_fenceLastSignaledValue, g_fenceEvent);
	WaitForSingleObject(g_fenceEvent, INFINITE);

	list->Release();
	alloc->Release();

        bg = newTex;
        BgTexture = newTex.id;
        return true;
}

static void RequestBackgroundReload(const std::wstring& newPath, c_settings* appSettings, c_usersettings* appUser)
{
        DebugLog("RequestBackgroundReload: begin path=%s", WStringToUtf8(newPath).c_str());
        {
                std::lock_guard<std::mutex> lock(g_bgJob.mtx);
                g_bgJob.path = newPath;
                g_bgJob.bytes.clear();
        }

	g_bgJob.requested.store(true);
	g_bgJob.bytes_ready.store(false);
	g_bgJob.upload_submitted.store(false);
	g_bgJob.new_tex = (ImTextureID)nullptr;
	g_bgJob.fence_value = 0;
        g_bgJob.new_texture_res = nullptr;
        g_bgJob.new_upload_res = nullptr;
        g_bgJob.new_cpu = { 0 };
        g_bgJob.new_gpu = { 0 };

	// cache vsync and fps values
	g_App.Lcache.vsync = appSettings->vsync;
        g_App.Lcache.target_fps = appUser->render.target_fps;

	// front-end transition ON immediately
        appSettings->vsync = false;
        appUser->render.target_fps = 60;
        appSettings->isLoading = true;

        DebugLog(
                "RequestBackgroundReload: front-end set to loading (vsync=%d target_fps=%d)",
                appSettings->vsync,
                appUser->render.target_fps);
}


static void BgReloadWorker()
{
        DebugLog("BgReloadWorker: invoked (requested=%d)", g_bgJob.requested.load());
        // Only proceed if someone requested
        if (!g_bgJob.requested.load())
                return;

        std::this_thread::sleep_for(std::chrono::milliseconds(1300));

	std::wstring path;
        {
                std::lock_guard<std::mutex> lock(g_bgJob.mtx);
                path = g_bgJob.path;
        }

        DebugLog("BgReloadWorker: reading path=%s", WStringToUtf8(path).c_str());

	// Read bytes (binary)
	std::string utf8 = WStringToUtf8(path);
        FILE* f = fopen(utf8.c_str(), "rb");
        if (!f)
        {
                DebugLog("BgReloadWorker: fopen failed for %s", utf8.c_str());
                return;
        }

	fseek(f, 0, SEEK_END);
	size_t sz = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);

	std::vector<unsigned char> bytes;
	bytes.resize(sz);
        fread(bytes.data(), 1, sz, f);
        fclose(f);

        DebugLog("BgReloadWorker: read %zu bytes", sz);

        {
                std::lock_guard<std::mutex> lock(g_bgJob.mtx);
                g_bgJob.bytes = std::move(bytes);
        }

        g_bgJob.bytes_ready.store(true);
        DebugLog("BgReloadWorker: bytes ready set=true");
}

static void DX12_FreeSrvByImTextureID(ImTextureID id)
{
	if (!id || !g_pd3dSrvDescHeap)
		return;

	const UINT64 gpuPtr = (UINT64)id;
	const UINT64 heapGpuStart = g_pd3dSrvDescHeapAlloc.HeapStartGpu.ptr;
	const UINT64 heapCpuStart = g_pd3dSrvDescHeapAlloc.HeapStartCpu.ptr;
	const UINT64 inc = g_pd3dSrvDescHeapAlloc.HeapHandleIncrement;

	if (gpuPtr < heapGpuStart)
		return;

	const UINT64 offset = gpuPtr - heapGpuStart;
	if (inc == 0 || (offset % inc) != 0)
		return;

	D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
	D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
	gpu.ptr = gpuPtr;
	cpu.ptr = heapCpuStart + offset;

	g_pd3dSrvDescHeapAlloc.Free(cpu, gpu);
}

static bool DX12_CreateTextureFromImageBytes(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	const unsigned char* fileBytes,
	size_t fileSize,
	D3D12_CPU_DESCRIPTOR_HANDLE srvCpu,
	ID3D12Resource** outTexture,
	ID3D12Resource** outUpload)
{
	*outTexture = nullptr;
	*outUpload = nullptr;

	int width = 0, height = 0;
	unsigned char* rgba = stbi_load_from_memory(
		fileBytes,
		(int)fileSize,
		&width,
		&height,
		nullptr,
		4
	);

	if (!rgba || width <= 0 || height <= 0)
		return false;

	// -------------------------
	// Texture description
	// -------------------------
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = (UINT64)width;
	texDesc.Height = (UINT)height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	// -------------------------
	// Default heap (GPU texture)
	// -------------------------
	D3D12_HEAP_PROPERTIES heapDefault = {};
	heapDefault.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapDefault.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapDefault.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapDefault.CreationNodeMask = 1;
	heapDefault.VisibleNodeMask = 1;

	HRESULT hr = device->CreateCommittedResource(
		&heapDefault,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(outTexture)
	);

	if (FAILED(hr))
	{
		stbi_image_free(rgba);
		return false;
	}

	// -------------------------
	// Upload buffer
	// -------------------------
	UINT64 uploadSize = 0;
	device->GetCopyableFootprints(
		&texDesc,
		0,
		1,
		0,
		nullptr,
		nullptr,
		nullptr,
		&uploadSize
	);

	D3D12_RESOURCE_DESC uploadDesc = {};
	uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	uploadDesc.Alignment = 0;
	uploadDesc.Width = uploadSize;
	uploadDesc.Height = 1;
	uploadDesc.DepthOrArraySize = 1;
	uploadDesc.MipLevels = 1;
	uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
	uploadDesc.SampleDesc.Count = 1;
	uploadDesc.SampleDesc.Quality = 0;
	uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	D3D12_HEAP_PROPERTIES heapUpload = {};
	heapUpload.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapUpload.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapUpload.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapUpload.CreationNodeMask = 1;
	heapUpload.VisibleNodeMask = 1;

	hr = device->CreateCommittedResource(
		&heapUpload,
		D3D12_HEAP_FLAG_NONE,
		&uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(outUpload)
	);

	if (FAILED(hr))
	{
		(*outTexture)->Release();
		*outTexture = nullptr;
		stbi_image_free(rgba);
		return false;
	}

	// -------------------------
	// Copy data into upload buffer
	// -------------------------
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed = {};
	UINT numRows = 0;
	UINT64 rowSize = 0;
	UINT64 totalBytes = 0;

	device->GetCopyableFootprints(
		&texDesc, 0, 1, 0,
		&placed, &numRows, &rowSize, &totalBytes
	);


	void* mapped = nullptr;
	(*outUpload)->Map(0, nullptr, &mapped);

	unsigned char* dst = (unsigned char*)mapped;
	const unsigned char* src = rgba;

	for (UINT y = 0; y < numRows; ++y)
	{
		memcpy(dst + y * placed.Footprint.RowPitch, src + y * width * 4, width * 4);
	}

	(*outUpload)->Unmap(0, nullptr);

	// -------------------------
	// Copy upload → texture
	// -------------------------
	D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
	dstLoc.pResource = *outTexture;
	dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstLoc.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
	srcLoc.pResource = *outUpload;
	srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	srcLoc.PlacedFootprint = placed;


	cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

	// -------------------------
	// Transition to shader-readable
	// -------------------------
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = *outTexture;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	cmdList->ResourceBarrier(1, &barrier);

	// -------------------------
	// SRV
	// -------------------------
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	device->CreateShaderResourceView(*outTexture, &srvDesc, srvCpu);

	stbi_image_free(rgba);
	return true;
}


static bool SubmitBgUploadDX12()
{
        if (g_App.g_RenderBackend != RenderBackend::DX12)
                return false;

        if (!g_bgJob.requested.load() || !g_bgJob.bytes_ready.load())
                return false;

        if (g_bgJob.upload_submitted.load())
                return false;

        DebugLog(
                "SubmitBgUploadDX12: begin (requested=%d bytes_ready=%d upload_submitted=%d)",
                g_bgJob.requested.load(),
                g_bgJob.bytes_ready.load(),
                g_bgJob.upload_submitted.load());

        std::vector<unsigned char> bytes;
        {
                std::lock_guard<std::mutex> lock(g_bgJob.mtx);
                bytes = g_bgJob.bytes;
        }
        if (bytes.empty())
        {
                DebugLog("SubmitBgUploadDX12: abort (empty bytes)");
                return false;
        }

	std::lock_guard<std::mutex> uploadLock(g_dx12UploadMutex);

	// Allocate a descriptor for the new background
	D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
	D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
	g_pd3dSrvDescHeapAlloc.Alloc(&cpu, &gpu);

	// Record upload work on dedicated list
	g_pd3dUploadCmdAlloc->Reset();
	g_pd3dUploadCmdList->Reset(g_pd3dUploadCmdAlloc, nullptr);

	ID3D12Resource* texRes = nullptr;
	ID3D12Resource* uploadRes = nullptr;

        if (!DX12_CreateTextureFromImageBytes(
                g_pd3dDevice,
                g_pd3dUploadCmdList,
                bytes.data(),
                bytes.size(),
                cpu,
                &texRes,
                &uploadRes))
        {
                g_pd3dUploadCmdList->Close();
                g_pd3dSrvDescHeapAlloc.Free(cpu, gpu);
                DebugLog("SubmitBgUploadDX12: DX12_CreateTextureFromImageBytes failed");
                return false;
        }

	g_pd3dUploadCmdList->Close();
	ID3D12CommandList* lists[] = { g_pd3dUploadCmdList };
	g_pd3dCommandQueue->ExecuteCommandLists(1, lists);

	// Signal fence
        const UINT64 fv = ++g_fenceLastSignaledValue;
        g_pd3dCommandQueue->Signal(g_fence, fv);

	// Store job state
	g_bgJob.new_tex = (ImTextureID)gpu.ptr;
	g_bgJob.fence_value = fv;
	g_bgJob.new_texture_res = texRes;     // keep alive
	g_bgJob.new_upload_res = uploadRes;  // release after fence
	g_bgJob.new_cpu = cpu;
        g_bgJob.new_gpu = gpu;
        g_bgJob.upload_submitted.store(true);

        DebugLog(
                "SubmitBgUploadDX12: submitted (bytes=%zu fence=%llu cpu=%p gpu=%llu)",
                bytes.size(),
                (unsigned long long)fv,
                (void*)cpu.ptr,
                (unsigned long long)gpu.ptr);

        return true;
}


static void FinalizeBgUploadIfReady()
{
        if (g_App.g_RenderBackend != RenderBackend::DX12)
                return;

        if (!g_bgJob.upload_submitted.load())
                return;

        if (g_fence->GetCompletedValue() < g_bgJob.fence_value)
        {
                DebugLog(
                        "FinalizeBgUploadIfReady: waiting (completed=%llu target=%llu)",
                        (unsigned long long)g_fence->GetCompletedValue(),
                        (unsigned long long)g_bgJob.fence_value);
                return;
        }

	// Upload buffer no longer needed once GPU finished the copy
        if (g_bgJob.new_upload_res)
        {
                g_bgJob.new_upload_res->Release();
                g_bgJob.new_upload_res = nullptr;
                DebugLog("FinalizeBgUploadIfReady: released upload buffer");
        }

	// Free old SRV slot (prevents heap exhaustion)
        if (BgTexture)
        {
                DX12_FreeSrvByImTextureID(BgTexture);
                BgTexture = (ImTextureID)nullptr;
                DebugLog("FinalizeBgUploadIfReady: freed previous BgTexture SRV");
        }

	// Keep new texture resource alive (matches your current lifetime model)
        if (g_bgJob.new_texture_res)
                g_dx12LiveTextures.push_back(g_bgJob.new_texture_res);

        BgTexture = g_bgJob.new_tex;

        DebugLog(
                "FinalizeBgUploadIfReady: completed (new_tex=%llu cpu=%p gpu=%llu)",
                (unsigned long long)g_bgJob.new_tex,
                (void*)g_bgJob.new_cpu.ptr,
                (unsigned long long)g_bgJob.new_gpu.ptr);

	// Reset job
	g_bgJob.requested.store(false);
	g_bgJob.bytes_ready.store(false);
	g_bgJob.upload_submitted.store(false);

	g_bgJob.new_tex = (ImTextureID)nullptr;
	g_bgJob.fence_value = 0;
	g_bgJob.new_texture_res = nullptr;
	g_bgJob.new_cpu = { 0 };
        g_bgJob.new_gpu = { 0 };

        // Restore
        settings->vsync = g_App.Lcache.vsync;
        user->render.target_fps = g_App.Lcache.target_fps;
        settings->isLoading = false;
        DebugLog("FinalizeBgUploadIfReady: restored settings (vsync=%d target_fps=%d)", settings->vsync, user->render.target_fps);
}


static void ApplyBgReloadDX11IfReady()
{
        if (g_App.g_RenderBackend != RenderBackend::DX11)
                return;

        if (!g_bgJob.requested.load() || !g_bgJob.bytes_ready.load())
                return;

        DebugLog(
                "ApplyBgReloadDX11IfReady: processing (requested=%d bytes_ready=%d)",
                g_bgJob.requested.load(),
                g_bgJob.bytes_ready.load());

	std::wstring path;
	{
		std::lock_guard<std::mutex> lock(g_bgJob.mtx);
		path = g_bgJob.path;
	}

        // Free old
        if (BgTexture)
        {
                TextureLoader::FreeTexture(bg, g_App);
                bg = {};
                BgTexture = (ImTextureID)nullptr;
                DebugLog("ApplyBgReloadDX11IfReady: freed previous texture");
        }

        HVKTexture tex{};
        if (LoadTextureUnified(path.c_str(), tex))
        {
                bg = tex;
                BgTexture = tex.id;
                DebugLog("ApplyBgReloadDX11IfReady: loaded new texture id=%llu", (unsigned long long)BgTexture);
        }

	// reset job
	g_bgJob.requested.store(false);
	g_bgJob.bytes_ready.store(false);
        settings->vsync = g_App.Lcache.vsync;
        user->render.target_fps = g_App.Lcache.target_fps;
        settings->isLoading = false;
        DebugLog(
                "ApplyBgReloadDX11IfReady: restore settings (vsync=%d target_fps=%d)",
                settings->vsync,
                user->render.target_fps);
}

static std::wstring MakeFramePath(const std::wstring& base, int i)
{
	wchar_t buf[64];

	// Try 0001.png style first
	swprintf_s(buf, L"%04d.png", i);
	std::wstring p = base + buf;
	if (std::filesystem::exists(p))
		return p;

	// Fallback to 1.png style
	swprintf_s(buf, L"%d.png", i);
	p = base + buf;
	return p;
}

void SwapLoadingIconTheme(LoadingTheme theme)
{
	// stop current loader thread cleanly
	g_texStop.store(true);
	if (texThread.joinable())
		texThread.join();
	g_texStop.store(false);

	// UI must treat textures as not ready
	g_texturesReady.store(false);

	if (g_App.g_RenderBackend == RenderBackend::DX12)
	{
		std::lock_guard<std::mutex> lock(g_dx12SrvMutex);

		for (auto& s : g_dx12LoadingSrvs)
			g_pd3dSrvDescHeapAlloc.Free(s.cpu, s.gpu);

		g_dx12LoadingSrvs.clear();
	}


	// clear current data
	{
		std::lock_guard<std::mutex> lock(g_texMutex);
		g_LoadingFrames.clear();
		g_pendingFrames.clear();
	}

	const std::wstring base =
		HVKIO::GetLocalAppDataW() +
		((theme == LoadingTheme::LIGHTMODE)
			? L"\\PSHVK\\assets\\LoadingIconLight\\"
			: L"\\PSHVK\\assets\\LoadingIcon\\"); // dark folder

	if (!std::filesystem::exists(base))
		printf("Loading Icon base does not exist: %ws\n", base.c_str());

	const int frameCount = (theme == LoadingTheme::LIGHTMODE) ? 30 : 31;

	texThread = std::thread([base, frameCount]()
		{
			HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

			if (g_App.g_RenderBackend == RenderBackend::DX11)
			{
				std::vector<HVKTexture> local;
				local.reserve(frameCount);

				for (int i = 1; i <= frameCount; ++i)
				{
					if (g_texStop.load())
						break;

					std::wstring fullPath = MakeFramePath(base, i);
					if (!std::filesystem::exists(fullPath))
						continue;

					HVKTexture tex{};
					if (LoadTextureUnified(fullPath.c_str(), tex))
						local.push_back(std::move(tex));
				}

				{
					std::lock_guard<std::mutex> lock(g_texMutex);
					g_LoadingFrames = std::move(local);
				}

				g_texturesReady.store(!g_LoadingFrames.empty());

				if (SUCCEEDED(comHr))
					CoUninitialize();
				return;
			}

			// DX12: load bytes only (GPU upload happens on main thread)
			std::vector<PendingFrame> local;
			local.reserve(frameCount);

			for (int i = 1; i <= frameCount; ++i)
			{
				if (g_texStop.load())
					break;

				std::wstring fullPath = MakeFramePath(base, i);
				if (!std::filesystem::exists(fullPath))
					continue;

				FILE* f = _wfopen(fullPath.c_str(), L"rb");
				if (!f)
					continue;

				fseek(f, 0, SEEK_END);
				size_t sz = (size_t)ftell(f);
				fseek(f, 0, SEEK_SET);

				PendingFrame p;
				p.bytes.resize(sz);
				fread(p.bytes.data(), 1, sz, f);
				fclose(f);

				local.push_back(std::move(p));
			}

			{
				std::lock_guard<std::mutex> lock(g_texMutex);
				g_pendingFrames = std::move(local);
			}

			if (SUCCEEDED(comHr))
				CoUninitialize();
		});
}



void PumpTexturesToGPU()
{
        if (g_App.g_RenderBackend != RenderBackend::DX12)
                return;

        if (g_texturesReady.load())
                return;

        size_t pendingCount = 0;
        std::vector<PendingFrame> work;
        {
                std::lock_guard<std::mutex> lock(g_texMutex);
                if (g_pendingFrames.empty())
                        return;
                work.swap(g_pendingFrames);
                pendingCount = work.size();
        }

        DebugLog(
                "PumpTexturesToGPU: uploading %zu pending frames (texturesReady=%d)",
                pendingCount,
                g_texturesReady.load());

	std::lock_guard<std::mutex> uploadLock(g_dx12UploadMutex);

	g_pd3dUploadCmdAlloc->Reset();
	g_pd3dUploadCmdList->Reset(g_pd3dUploadCmdAlloc, nullptr);


        // Load background ONCE (DX12 path)
        if (!BgTexture)
        {
                HVKTexture loaded{};
                if (TextureLoader::LoadTexture(
                        user->render.bg_image_path.c_str(),
                        g_pd3dDevice,
                        g_pd3dUploadCmdList,
                        g_pd3dSrvDescHeapAlloc,
                        loaded))
                {
                        bg = loaded;
                        BgTexture = loaded.id;
                }
        }

	std::vector<HVKTexture> frames;
	frames.reserve(work.size());

	for (auto& item : work)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
		g_pd3dSrvDescHeapAlloc.Alloc(&cpu, &gpu);

		{
			std::lock_guard<std::mutex> lock(g_dx12SrvMutex);
			g_dx12LoadingSrvs.push_back({ cpu, gpu });
		}


		ID3D12Resource* texRes = nullptr;
		int w = 0, h = 0;

		if (!LoadTextureFromMemory(
			item.bytes.data(),
			item.bytes.size(),
			g_pd3dDevice,
			cpu,
			&texRes,
			&w,
			&h))
		{
			g_pd3dSrvDescHeapAlloc.Free(cpu, gpu);
			continue;
		}

		// NOTE: you leak texRes here unless you track/release it.
		// For now this matches your old behavior.
		HVKTexture t{};
		t.id = (ImTextureID)gpu.ptr;
		t.width = w;
		t.height = h;
		frames.push_back(t);
	}

	g_pd3dUploadCmdList->Close();
	ID3D12CommandList* lists[] = { g_pd3dUploadCmdList };
	g_pd3dCommandQueue->ExecuteCommandLists(1, lists);
	WaitForPendingOperations();

	{
		std::lock_guard<std::mutex> lock(g_texMutex);
		g_LoadingFrames = std::move(frames);
	}

	g_texturesReady.store(true);
}


float GetWatermarkReservedHeight()
{
	ImGuiIO& io = ImGui::GetIO();

	const float padY = 6.0f;
	ImVec2 textSize = ImGui::CalcTextSize("FPS: 999 | CPU: 99.9% | GPU: 99999 / 99999 MB");

	return textSize.y + padY * 2.0f + 8.0f; // extra margin
}

std::wstring GetBgPath(BgTheme bgTheme)
{
	const std::wstring base = HVKIO::GetLocalAppDataW() + L"\\PSHVK\\assets\\";
	static std::wstring path;
	switch (bgTheme)
	{
	case BgTheme::BLACK:
	{
		path = base + L"Galaxy_Black.png";
		break;
	}
	case BgTheme::PURPLE:
	{
		path = base + L"Galaxy_Purple.png";
		break;
	}
	case BgTheme::YELLOW:
	{
		path = base + L"Galaxy_Yellow.png";
		break;
	}
	case BgTheme::BLUE:
	{
		path = base + L"Galaxy_Blue.png";
		break;
	}
	case BgTheme::GREEN:
	{
		path = base + L"Galaxy_Green.png";
		break;
	}
	case BgTheme::RED:
	{
		path = base + L"Galaxy_Red.png";
		break;
	}
	}
	return path;
}

void ApplyUserStyle()
{
	ImGuiStyle& s = ImGui::GetStyle();

	// global alpha
	s.Alpha = user->style.main_opacity;

	// main window
	s.Colors[ImGuiCol_WindowBg] = user->style.main_bg_color;
	s.Colors[ImGuiCol_Text] = user->style.main_text_color;
	s.Colors[ImGuiCol_Border] = user->style.main_border_color;

	// watermark (example — adapt to your actual usage)
	// if watermark uses ImGui draw lists, store these values where you read them
}

void ApplyRenderSettings()
{
	// reload background image if needed
	// this assumes you already have logic like SwapLoadingIconTheme()

	static std::wstring lastBg = user->render.bg_image_path;
	static LoadingTheme lastLoading = user->style.loading_theme;

	if (lastBg != user->render.bg_image_path)
	{
		lastBg = user->render.bg_image_path;

		// whatever function you already use to reload bg textures
		RequestBackgroundReload(user->render.bg_image_path, settings, user);
	}

	if (lastLoading != user->style.loading_theme)
	{
		lastLoading = user->style.loading_theme;
		SwapLoadingIconTheme(user->style.loading_theme);
	}
}

static std::filesystem::file_time_type lastWrite;

void PollSettingsHotReload()
{
	auto now = std::filesystem::last_write_time(HVKIO::GetLocalAppDataW() + L"\\PSHVK\\");
	if (now != lastWrite)
	{
		lastWrite = now;
		ApplyUserStyle();
		ApplyRenderSettings();
	}
}


// Main code
int main(int, char**)
{
	timeBeginPeriod(1);

	settings->is_first_run = IsFirstRun();

	if (settings->is_first_run)
	{
		HVKIO::DownloadPSHVKAssets();
		HVKIO::CreateInstanceFile();
	}

	if (!HVKIO::ValidateInstanceFile() && !settings->metadata.dev_build)
	{
		MessageBoxA(nullptr, "Instance file invalid.", "PSHVK", MB_ICONERROR);
		ExitProcess(0);
	}


	if (HVKSYS::SupportsDX12())
	{
		g_App.g_RenderBackend = RenderBackend::DX12;
	}
	else
	{
		g_App.g_RenderBackend = RenderBackend::DX11;
	}


	// Make process DPI aware and obtain main monitor scale
	ImGui_ImplWin32_EnableDpiAwareness();
	float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

	user->style.dpi_scale = main_scale;    // set once at startup


	// Create application window
	WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"WC_HVK", nullptr };
	::RegisterClassExW(&wc);
	// HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"PSHVK Window", WS_OVERLAPPEDWINDOW, 100, 100, (int)(1280 * main_scale), (int)(800 * main_scale), nullptr, nullptr, wc.hInstance, nullptr); // Use this to display title bar.
	HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"PSHVK Window", WS_POPUPWINDOW, 100, 100, (int)(1280 * main_scale), (int)(800 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);

	// Initialize Direct3D
	bool rendererOk = false;

	// Try DX12 first if preferred
	if (g_App.g_RenderBackend == RenderBackend::DX12)
	{
		rendererOk = HVKSYS::InitDX12(hwnd);
		if (!rendererOk)
			g_App.g_RenderBackend = RenderBackend::DX11; // fallback
	}

	// If DX12 failed or we prefer DX11
	if (!rendererOk && g_App.g_RenderBackend == RenderBackend::DX11)
	{
		rendererOk = HVKSYS::InitDX11(hwnd);
	}

	if (!rendererOk)
	{
		MessageBoxA(
			nullptr,
			"Failed to initialize a D3D11/D3D12 renderer on this system.",
			"Fatal Error",
			MB_ICONERROR
		);

		HVKSYS::CleanupDeviceD3D11();
		CleanupDeviceD3D();
		::UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return 1;
	}


	// Show the window
	::ShowWindow(hwnd, SW_SHOWMAXIMIZED);
	::UpdateWindow(hwnd);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Setup scaling
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

	// Use 13px as base to match ImGui's default font size for consistent spacing
	float baseFontSize = 13.0f;
	// Handle ui_scale being 0.0 (default) by using 1.0 as fallback
	float fontSize = baseFontSize * (user->style.ui_scale > 0.0f ? user->style.ui_scale : 1.0f);

	const std::string base = HVKIO::GetLocalAppData() + "\\PSHVK\\assets\\fonts\\";
	std::string satoshiRegularPath = base + "satoshi\\Satoshi-Regular.otf";
	std::string satoshiMediumPath = base + "satoshi\\Satoshi-Medium.otf";
	std::string satoshiBoldPath = base + "satoshi\\Satoshi-Bold.otf";
	std::string proggyPath = base + "proggy_clean\\ProggyClean.ttf";

	io.Fonts->Clear();
	
	// Load Satoshi Regular font with error handling
	ImFontConfig satoshiRegularConfig;
	satoshiRegularConfig.Flags |= ImFontFlags_NoLoadError;
	user->style.satoshi_regular = io.Fonts->AddFontFromFileTTF(satoshiRegularPath.c_str(), fontSize, &satoshiRegularConfig);
	if (!user->style.satoshi_regular)
	{
		printf("[FONT] Failed to load Satoshi Regular font from: %s\n", satoshiRegularPath.c_str());
		// Fallback to default font if Satoshi Regular fails to load
		ImFontConfig defaultConfig;
		defaultConfig.SizePixels = fontSize;
		user->style.satoshi_regular = io.Fonts->AddFontDefault(&defaultConfig);
		printf("[FONT] Using default font as fallback for Satoshi Regular\n");
	}
	else
	{
		printf("[FONT] Successfully loaded Satoshi Regular font from: %s\n", satoshiRegularPath.c_str());
	}
	
	// Load Satoshi Medium font with error handling
	ImFontConfig satoshiMediumConfig;
	satoshiMediumConfig.Flags |= ImFontFlags_NoLoadError;
	user->style.satoshi_medium = io.Fonts->AddFontFromFileTTF(satoshiMediumPath.c_str(), fontSize, &satoshiMediumConfig);
	if (!user->style.satoshi_medium)
	{
		printf("[FONT] Failed to load Satoshi Medium font from: %s\n", satoshiMediumPath.c_str());
		// Fallback to Satoshi Regular if Medium fails to load
		user->style.satoshi_medium = user->style.satoshi_regular;
		printf("[FONT] Using Satoshi Regular as fallback for Satoshi Medium\n");
	}
	else
	{
		printf("[FONT] Successfully loaded Satoshi Medium font from: %s\n", satoshiMediumPath.c_str());
	}
	
	// Load Satoshi Bold font with error handling
	ImFontConfig satoshiBoldConfig;
	satoshiBoldConfig.Flags |= ImFontFlags_NoLoadError;
	user->style.satoshi_bold = io.Fonts->AddFontFromFileTTF(satoshiBoldPath.c_str(), fontSize, &satoshiBoldConfig);
	if (!user->style.satoshi_bold)
	{
		printf("[FONT] Failed to load Satoshi Bold font from: %s\n", satoshiBoldPath.c_str());
		// Fallback to Satoshi Regular if Bold fails to load
		user->style.satoshi_bold = user->style.satoshi_regular;
		printf("[FONT] Using Satoshi Regular as fallback for Satoshi Bold\n");
	}
	else
	{
		printf("[FONT] Successfully loaded Satoshi Bold font from: %s\n", satoshiBoldPath.c_str());
	}
	
	// Load Proggy font with error handling
	ImFontConfig proggyConfig;
	proggyConfig.Flags |= ImFontFlags_NoLoadError;
	user->style.proggy_clean = io.Fonts->AddFontFromFileTTF(proggyPath.c_str(), fontSize, &proggyConfig);
	if (!user->style.proggy_clean)
	{
		printf("[FONT] Failed to load Proggy font from: %s\n", proggyPath.c_str());
		// Fallback to default font if Proggy fails to load
		ImFontConfig defaultConfig;
		defaultConfig.SizePixels = fontSize;
		user->style.proggy_clean = io.Fonts->AddFontDefault(&defaultConfig);
		printf("[FONT] Using default font as fallback for Proggy\n");
	}
	else
	{
		printf("[FONT] Successfully loaded Proggy font from: %s\n", proggyPath.c_str());
	}
	
	io.Fonts->Build();

	// Set satoshi regular as the default font for the menu
	io.FontDefault = user->style.satoshi_regular;

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);

	if (g_App.g_RenderBackend == RenderBackend::DX12)
	{
		ImGui_ImplDX12_InitInfo init_info = {};
		init_info.Device = g_pd3dDevice;
		init_info.CommandQueue = g_pd3dCommandQueue;
		init_info.NumFramesInFlight = APP_NUM_FRAMES_IN_FLIGHT;
		init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
		init_info.SrvDescriptorHeap = g_pd3dSrvDescHeap;
		init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu)
			{
				return g_pd3dSrvDescHeapAlloc.Alloc(out_cpu, out_gpu);
			};
		init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
			{
				return g_pd3dSrvDescHeapAlloc.Free(cpu, gpu);
			};
		ImGui_ImplDX12_Init(&init_info);
	}
	else
	{
		ImGui_ImplDX11_Init(g_pd3dDevice11, g_pd3dDeviceContext11);
	}


// ----------------------------------------
// Load textures once (NOT every frame)
// ----------------------------------------
	texThread = std::thread([]()
		{
			// COM init is PER THREAD (WIC needs this)
			HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

			static const std::wstring base =
				HVKIO::GetLocalAppDataW() + L"\\PSHVK\\assets\\LoadingIcon\\";

			if (g_App.g_RenderBackend == RenderBackend::DX11)
			{
				std::vector<HVKTexture> local;
				local.reserve(31);

				for (int i = 1; i <= 31; ++i)
				{
					if (g_texStop.load())
						break;

					wchar_t buf[64];
					swprintf_s(buf, L"%04d.png", i);
					std::wstring fullPath = base + buf;

					HVKTexture tex{};
					if (LoadTextureUnified(fullPath.c_str(), tex))
						local.push_back(std::move(tex));
				}

				HVKTexture bgLocal{};
				LoadTextureUnified(user->render.bg_image_path.c_str(), bgLocal);

				{
					std::lock_guard<std::mutex> lock(g_texMutex);
					g_LoadingFrames = std::move(local);
					BgTexture = bgLocal.id;
				}

				// Only mark ready if we actually have something useful
				g_texturesReady.store(BgTexture != (ImTextureID)nullptr || !g_LoadingFrames.empty());

				if (SUCCEEDED(comHr))
					CoUninitialize();

				return;
			}

			// DX12: DO NOT create/upload textures here. Load bytes only.
			std::vector<PendingFrame> local;
			local.reserve(31);

			for (int i = 1; i <= 31; ++i)
			{
				if (g_texStop.load())
					return;

				wchar_t buf[64];
				swprintf_s(buf, L"%04d.png", i);

				std::string path = WStringToUtf8(base + buf);

				FILE* f = fopen(path.c_str(), "rb");
				if (!f)
					continue;

				fseek(f, 0, SEEK_END);
				size_t sz = (size_t)ftell(f);
				fseek(f, 0, SEEK_SET);

				PendingFrame p;
				p.bytes.resize(sz);
				fread(p.bytes.data(), 1, sz, f);
				fclose(f);

				local.push_back(std::move(p));
			}

			//LoadTextureUnified(user->render.bg_image_path.c_str(), bg);

			{
				std::lock_guard<std::mutex> lock(g_texMutex);
				g_pendingFrames = std::move(local);
			}

			/*{
				std::lock_guard<std::mutex> lock(g_texMutex);
				BgTexture = bg.id;
			}*/
		});



	// Our state
	bool show_demo_window = true;
	bool show_another_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	std::vector<int> fps_presets = { 30, 60, 75, 90, 120, 144, 240, 360, 480, 1000 };

	float scale = 0.25f;
	ImVec2 default_btn_size = ImVec2(7, 4);
	static auto last_wm_update = std::chrono::high_resolution_clock::now();

	if (g_ResUI.All.empty())
	{
		g_ResUI.All = Display::EnumerateResolutions();
		g_ResUI.Filtered =
			Display::UniqueResolutions(
				Display::FilterByAspect(
					g_ResUI.All,
					g_ResUI.AspectIndex));

	}


	int base_x = GetSystemMetrics(SM_CXSCREEN);
	int base_y = GetSystemMetrics(SM_CYSCREEN);

	printf("Base Resolution: %d x %d\n\n\n", base_x, base_y);

	UsbSignature trustedUsb{};
	USBHelper:: GetInfoByDriveLetter('E', trustedUsb);

	std::string trustedKey = USBHelper::BuildCompositeKey(trustedUsb);

	try
	{
		UsbSignature sig{};

		bool ok = USBHelper::GetInfoByDriveLetter('E', sig);

		printf("GetInfoByDriveLetter returned: %s\n", ok ? "true" : "false");

		std::cout
			<< "VID=" << sig.vid << "\n"
			<< "PID=" << sig.pid << "\n"
			<< "SER=" << sig.serial << "\n"
			<< "MFG=" << sig.manufacturer << "\n"
			<< "PROD=" << sig.product << "\n"
			<< "RIDH=" << sig.reportedIdHash << "\n\n";

	}
	catch(std::exception e)
	{
		//printf("An error occured whilst trying to fetch USB signature: %s", e.what());
		printf("An error occured. ");
		std::cout << e.what() << std::endl;
	}

        // Main loop
        bool done = false;
        uint64_t frameCounter = 0;
        while (!done)
        {
                const uint64_t frameIndex = frameCounter++;
                DebugLog("Frame %llu: begin", (unsigned long long)frameIndex);

                // Poll and handle messages (inputs, window resize, etc.)
                // See the WndProc() function below for our to dispatch events to the Win32 backend.
                MSG msg;
                while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
                {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
				done = true;
		}
                if (done)
                        break;

                // Handle window screen locked
                if ((g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) || ::IsIconic(hwnd))
                {
                        ::Sleep(10);
                        DebugLog("Frame %llu: skipped due to occlusion/iconic", (unsigned long long)frameIndex);
                        continue;
                }
                g_SwapChainOccluded = false;

		g_fpsLimiter.SetTargetFPS(settings->vsync ? 100 : user->render.target_fps);   // *after* backend init

		if (g_App.NeedsRefresh)
			RefreshDisks();

		static int lastPhysicalIndex = -1;

		if (g_App.Selection.PhysicalIndex != lastPhysicalIndex)
		{
			lastPhysicalIndex = g_App.Selection.PhysicalIndex;
			Disk::RefreshPartitionsForSelectedDisk();
		}

                g_Sys.Update();
                DebugLog("Frame %llu: after g_Sys.Update", (unsigned long long)frameIndex);
                PumpTexturesToGPU();
                DebugLog("Frame %llu: after PumpTexturesToGPU", (unsigned long long)frameIndex);

                // Background reload processing
                ApplyBgReloadDX11IfReady();
                bool submitted = SubmitBgUploadDX12();
                if (submitted)
                        DebugLog("Frame %llu: SubmitBgUploadDX12 returned true", (unsigned long long)frameIndex);
                FinalizeBgUploadIfReady();

                DebugLog("Frame %llu: before ImGui::UpdateStyle", (unsigned long long)frameIndex);
                ImGui::UpdateStyle(*user, style);
                DebugLog("Frame %llu: after ImGui::UpdateStyle", (unsigned long long)frameIndex);
                PollSettingsHotReload();

                DebugLog("Frame %llu: after PollSettingsHotReload", (unsigned long long)frameIndex);

                switch (settings->themecombos.BgThemeIdx)
                {
                case 0:
                {
                        if (user->style.bg_theme != BgTheme::BLACK)
                        {
                                user->style.bg_theme = BgTheme::BLACK;
                                ThemeHelper::UpdateSecondaryColorFromTheme(user);
                                RequestBackgroundReload(GetBgPath(user->style.bg_theme), settings, user);
                                DebugLog("Frame %llu: requested BLACK background reload", (unsigned long long)frameIndex);
                                std::thread(BgReloadWorker).detach();
                        }
                        break;
                }
		case 1:
		{
                        if (user->style.bg_theme != BgTheme::PURPLE)
                        {
                                user->style.bg_theme = BgTheme::PURPLE;
                                ThemeHelper::UpdateSecondaryColorFromTheme(user);
                                RequestBackgroundReload(GetBgPath(user->style.bg_theme), settings, user);
                                DebugLog("Frame %llu: requested PURPLE background reload", (unsigned long long)frameIndex);
                                std::thread(BgReloadWorker).detach();
                        }
                        break;
                }
		case 2:
		{
                        if (user->style.bg_theme != BgTheme::YELLOW)
                        {
                                user->style.bg_theme = BgTheme::YELLOW;
                                ThemeHelper::UpdateSecondaryColorFromTheme(user);
                                RequestBackgroundReload(GetBgPath(user->style.bg_theme), settings, user);
                                DebugLog("Frame %llu: requested YELLOW background reload", (unsigned long long)frameIndex);
                                std::thread(BgReloadWorker).detach();
                        }
                        break;
                }
		case 3:
		{
                        if (user->style.bg_theme != BgTheme::BLUE)
                        {
                                user->style.bg_theme = BgTheme::BLUE;
                                ThemeHelper::UpdateSecondaryColorFromTheme(user);
                                RequestBackgroundReload(GetBgPath(user->style.bg_theme), settings, user);
                                DebugLog("Frame %llu: requested BLUE background reload", (unsigned long long)frameIndex);
                                std::thread(BgReloadWorker).detach();
                        }
                        break;
                }
		case 4:
		{
                        if (user->style.bg_theme != BgTheme::GREEN)
                        {
                                user->style.bg_theme = BgTheme::GREEN;
                                ThemeHelper::UpdateSecondaryColorFromTheme(user);
                                RequestBackgroundReload(GetBgPath(user->style.bg_theme), settings, user);
                                DebugLog("Frame %llu: requested GREEN background reload", (unsigned long long)frameIndex);
                                std::thread(BgReloadWorker).detach();
                        }
                        break;
                }
		case 5:
		{
                        if (user->style.bg_theme != BgTheme::RED)
                        {
                                user->style.bg_theme = BgTheme::RED;
                                ThemeHelper::UpdateSecondaryColorFromTheme(user);
                                RequestBackgroundReload(GetBgPath(user->style.bg_theme), settings, user);
                                DebugLog("Frame %llu: requested RED background reload", (unsigned long long)frameIndex);
                                std::thread(BgReloadWorker).detach();
                        }
                        break;
                }
                }

                DebugLog("Frame %llu: after background theme selection", (unsigned long long)frameIndex);

                switch (settings->themecombos.LoadingThemeIdx)
                {
                case 0:
                {
                        if (user->style.loading_theme != LoadingTheme::DARKMODE)
			{
				user->style.loading_theme = LoadingTheme::DARKMODE;
				SwapLoadingIconTheme(user->style.loading_theme);
			}
			break;
		}
		case 1:
		{
			if (user->style.loading_theme != LoadingTheme::LIGHTMODE)
			{
				user->style.loading_theme = LoadingTheme::LIGHTMODE;
				SwapLoadingIconTheme(user->style.loading_theme);
			}
                        break;
                }
                }

                DebugLog("Frame %llu: after loading theme selection", (unsigned long long)frameIndex);

                // Start the Dear ImGui frame
                DebugLog("Frame %llu: before backend NewFrame", (unsigned long long)frameIndex);
                if (g_App.g_RenderBackend == RenderBackend::DX12)
                        ImGui_ImplDX12_NewFrame();
                else
                        ImGui_ImplDX11_NewFrame();

                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                DebugLog("Frame %llu: after ImGui::NewFrame", (unsigned long long)frameIndex);


		if (GetAsyncKeyState(VK_F7) & 1)
			settings->isLoading = !settings->isLoading;

		ImDrawList* bg = ImGui::GetBackgroundDrawList();

		if (!settings->isLoading)

		{
			if (GetAsyncKeyState(user->binds.toggle_main) & 1)
				settings->visibility.win_main = !settings->visibility.win_main;

			if (GetAsyncKeyState(user->binds.toggle_dev) & 1)
				settings->visibility.win_dev = !settings->visibility.win_dev;

                        if (GetAsyncKeyState(user->binds.shutdown) & 1)
                                done = true;

                        if (BgTexture)
                        {
                                bg->AddImage(
                                        BgTexture,
                                        ImVec2(0, 0),
                                        ImVec2(io.DisplaySize.x, io.DisplaySize.y));
                                DebugLog("Frame %llu: drew background image", (unsigned long long)frameIndex);
                        }
			float cpu = g_Sys.GetCPUUsage();
			uint64_t used = g_Sys.GetGPUMemoryUsed() / (1024 * 1024);
			uint64_t total = g_Sys.GetGPUMemoryTotal() / (1024 * 1024);

			ImGui::Watermark(
				settings->fps,
				&wmStats.cpu,
				&wmStats.gpuUsedMB,
				&wmStats.gpuTotalMB,
				user->style.wm_bg_color, // bg
				user->style.wm_text_color,    // text
				user->style.proggy_clean,     // font (proggy for watermark)
				user->style.wm_opacity,
				10.0f
			);

			auto now = std::chrono::high_resolution_clock::now();
			auto elapsed_ms =
				std::chrono::duration_cast<std::chrono::milliseconds>(
					now - last_wm_update).count();

			if (elapsed_ms >= user->render.wm_render_interval)
			{
				// FPS (from ImGui)
				settings->fps = &io.Framerate;

				// CPU / GPU (from HVKSYS)
				wmStats.cpu = g_Sys.GetCPUUsage();
				wmStats.gpuUsedMB =
					g_Sys.GetGPUMemoryUsed() / (1024 * 1024);
				wmStats.gpuTotalMB =
					g_Sys.GetGPUMemoryTotal() / (1024 * 1024);

				last_wm_update = now;
			}

			// ImGui::PushFont(user->style.satoshi_medium);
			// ImGui::PopFont();

			if (settings->visibility.win_main)
			{
			ImGui::Begin("Main Window", nullptr, ImGuiWindowFlags_NoTitleBar);
			{
				float topOffset = GetWatermarkReservedHeight();
					ImGui::SetWindowPos(
						ImVec2(10.0f, 10.0f + topOffset),
						ImGuiCond_Once
					);
					ImGui::SetWindowSize(ImVec2(600, 900), ImGuiCond_Once);
					
					ImGuiStyle& style = ImGui::GetStyle();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

					// Title: Largest size (1.6x base) - uses Regular
					float window_width = ImGui::GetWindowWidth();
					const char* title = "PSHVK";
					ImVec2 text_size = ImGui::CalcTextSize(title);
					ImGui::SetCursorPosX((window_width - text_size.x) * 0.5f);
					HvkGui::GlowText(
						user->style.satoshi_regular,
						style.FontSizeBase * 1.6f,
						user->style.main_secondary_color,
						title,
						user->style.main_secondary_color,
						12.0f,
						0.3f
					);

					ImGui::Separator();

					// Tab bar: Using HvkGui::CustomTabBar with glow effects
					const char* tabs[] = { "Home", "Format", "Settings", "Visuals" };
					
					// Apply inactive opacity to unselected color
					ImVec4 unselectedColor = user->style.tabbar_text_color;
					unselectedColor.w *= user->style.tabbar_inactive_opacity;
					
					HvkGui::CustomTabBar(
						tabs,
						IM_ARRAYSIZE(tabs),
						settings->g_MainTab,
						user->style.satoshi_regular,  // Selected font
						user->style.satoshi_medium,    // Unselected font
						style.FontSizeBase * 1.8f,     // Selected font size (1.8x base)
						style.FontSizeBase * 1.7f,     // Unselected font size (1.7x base)
						user->style.tabbar_selected_color,  // Selected color
						unselectedColor,                    // Unselected color (with opacity applied)
						user->style.tabbar_selected_color,  // Glow color (same as selected)
						10.0f,                              // Glow size
						0.1f,                               // Glow intensity
						12.0f,                              // Horizontal spacing
						8.0f                                // Vertical padding
					);
					ImGui::Separator();

					ImGui::PopStyleColor();

					// Content: Smallest size (1.0x base, default)
					if (user->style.proggy_clean)
						ImGui::PushFont(user->style.proggy_clean, style.FontSizeBase * 1.0f);

					switch (settings->g_MainTab)
					{
					case 0:
					{
						ImGui::BeginGroup();
						{
							ImGui::Text("List Disk Info");
							ImGui::SameLine();
							ImGui::ModernStyle::ModernCheckbox("##ListDiskInfo", &settings->visibility.disk_info);

							if (settings->visibility.disk_info)
							{
								ImGui::DrawVolumeList(
									g_App.Volumes,
									&g_App.Selection.VolumeIndex
								);
							}

							ImGui::Text("List Partition Info");
							ImGui::SameLine();
							ImGui::ModernStyle::ModernCheckbox("##ListPartitionInfo", &settings->visibility.part_info);

							if (settings->visibility.part_info)
							{
								Disk::RefreshPartitionsForSelectedDisk();

								ImGui::DrawPartitionList(
									g_App.Partitions,
									&g_App.Selection.PartitionIndex
								);
							}

							ImGui::Separator();

							ImGui::Text("List Disk Info With Partitions");
							ImGui::SameLine();
							ImGui::ModernStyle::ModernCheckbox(
								"##ListDiskInfoWithPartitions",
								&settings->visibility.disk_and_part_info
							);

							if (settings->visibility.disk_and_part_info)
							{
								if (g_App.Selection.PhysicalIndex >= 0 &&
									g_App.Selection.PhysicalIndex < (int)g_App.PhysicalDisks.size())
								{
									Disk::RefreshPartitionsForSelectedDisk();

									ImGui::DrawDiskWithPartitions(
										g_App.PhysicalDisks[g_App.Selection.PhysicalIndex],
										g_App.Partitions,
										&g_App.Selection.PartitionIndex
									);
								}
								else
								{
									ImGui::TextDisabled("No physical disk selected");
								}
							}
						}
						ImGui::EndGroup();

						ImGui::Separator();

						ImGui::BeginGroup();
						{
							ImGui::Text("Show Selection Window:");
							ImGui::SameLine();
							ImGui::ModernStyle::ModernCheckbox("##ShowSelectionWindow", &settings->visibility.win_selector);
						}
						ImGui::EndGroup();
						break;
					}
					case 1: ImGui::DrawFormatWidget(g_App); break;
					case 2:
					{
						ImGui::DrawResolutionWidget(g_ResUI);

						ImGui::Spacing(12.0f);
						ImGui::Separator();
						ImGui::Spacing(12.0f);

						std::wstring base = HVKIO::GetLocalAppDataW() + L"\\PSHVK\\";
						if (ImGui::ModernStyle::ModernButton("Export Global Settings"))
							settings->ExportToHvk(base + L"settings.hvk");

						ImGui::ModernStyle::AddSpacing(6.0f);
						if (ImGui::ModernStyle::ModernButton("Export User Settings"))
							user->ExportToHvk(base + L"usersettings.hvk");

						ImGui::Spacing(10.0f);

						if (ImGui::ModernStyle::ModernButton("Import Global Settings"))
							settings->ImportFromHvk(base + L"settings.hvk");

						ImGui::ModernStyle::AddSpacing(6.0f);
						if (ImGui::ModernStyle::ModernButton("Import User Settings"))
							user->ImportFromHvk(base + L"usersettings.hvk");

                                                break;
                                        }
                                        case 3:
                                        {
						ImGui::Text("Themes");
						ImGui::Spacing();
						ImGui::ModernStyle::ModernCombo("Loading Theme", &settings->themecombos.LoadingThemeIdx, "Dark\0Light\0");
						ImGui::ModernStyle::ModernCombo("Background Theme", &settings->themecombos.BgThemeIdx, "Black\0Purple\0Yellow\0Blue\0Green\0Red\0");

						ImGui::Spacing(12.0f);
						ImGui::Separator();
						ImGui::Spacing(12.0f);

						ImGui::Text("Main Window");
						ImGui::Spacing();
						ImGui::ModernStyle::ModernColorEdit3("Background Color##MainWin", (float*)&user->style.main_bg_color);
						ImGui::ModernStyle::ModernColorEdit3("Text Color##MainWin", (float*)&user->style.main_text_color);
						ImGui::ModernStyle::ModernSliderFloat("Opacity##MainWin", &user->style.main_opacity, 0.0f, 1.0f, "%.2f");

						ImGui::Spacing(12.0f);
						ImGui::Separator();
						ImGui::Spacing(12.0f);

						ImGui::Text("Watermark");
						ImGui::Spacing();
						ImGui::ModernStyle::ModernColorEdit3("Background Color##Watermark", (float*)&user->style.wm_bg_color);
						ImGui::ModernStyle::ModernColorEdit3("Text Color##Watermark", (float*)&user->style.wm_text_color);
                                                ImGui::ModernStyle::ModernSliderFloat("Opacity##Watermark", &user->style.wm_opacity, 0.0f, 1.0f, "%.2f");

                                                ImGui::Spacing(12.0f);
						ImGui::Separator();
						ImGui::Spacing(12.0f);

						ImGui::Text("TabBar");
						ImGui::Spacing();
						ImGui::ModernStyle::ModernColorEdit3("Tab Text Color##TabBar", (float*)&user->style.tabbar_text_color);
						ImGui::ModernStyle::ModernColorEdit3("Selected Tab Text Color##TabBar", (float*)&user->style.tabbar_selected_color);
						ImGui::ModernStyle::ModernSliderFloat("Inactive Tab Text Opacity##TabBar", &user->style.tabbar_inactive_opacity, 0.0f, 1.0f, "%.2f");

						ImGui::Spacing(12.0f);
						ImGui::Separator();
						ImGui::Spacing(12.0f);

						ImGui::Text("Button Colors");
						ImGui::Spacing();
						ImGui::ModernStyle::ModernColorEdit3("Button Color##Button", (float*)&user->style.button_color);
						ImGui::ModernStyle::ModernColorEdit3("Button Text Color##Button", (float*)&user->style.button_text_color);
						ImGui::ModernStyle::ModernColorEdit3("Button Hover Color##Button", (float*)&user->style.button_hover_color);
						ImGui::ModernStyle::ModernColorEdit3("Button Hover Text Color##Button", (float*)&user->style.button_hover_text_color);
						ImGui::ModernStyle::ModernColorEdit3("Button Active Color##Button", (float*)&user->style.button_active_color);

						ImGui::Spacing(12.0f);
						ImGui::Separator();
						ImGui::Spacing(12.0f);

						// ImGui::ModernStyle::ModernSliderFloat("Main Scale", &user->style.ui_scale, 1, 100, "%.0f"); // BROKEN: Changing value crashes application currently.

					break;
					}
					}

					// Pop Proggy Clean font for tab content
					if (user->style.proggy_clean)
						ImGui::PopFont();

				}
				ImGui::End();
			}

			if (settings->visibility.win_selector)
			{
				ImGui::DrawDiskSelector(g_App);
			}

			if (settings->visibility.win_dev)
			{
				ImGui::Begin("Developer Window", nullptr, ImGuiWindowFlags_NoTitleBar);
				{
					float topOffset = GetWatermarkReservedHeight();

					ImGui::SetWindowPos(
						ImVec2(610.0f, 10.0f + topOffset),
						ImGuiCond_Once
					);
					ImGui::SetWindowSize(ImVec2(600, 900), ImGuiCond_Once);
					ImGui::PushFont(ImGui::GetFont());
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

					float window_width = ImGui::GetWindowWidth();
					const char* title = "Developer Window";
					ImVec2 text_size = ImGui::CalcTextSize(title);
					ImGui::SetCursorPosX((window_width - text_size.x) * 0.5f);
					ImGui::Text(title);
					ImGui::Separator();

					ImGui::PopStyleColor();
					ImGui::PopFont();

					ImGui::BeginGroup();
					{
						ImGui::Text("Vsync:");
						ImGui::SameLine();
						ImGui::Checkbox("##Vsync", &settings->vsync);
						if (!settings->vsync)
						{
							ImGui::Text("Tearing support: %s", g_SwapChainTearingSupport ? "Yes" : "No");
							ImGui::Separator();
							ImGui::SnapSlider(fps_presets, &user->render.target_fps, "Target FPS:");
							ImGui::Text("Watermark Update Interval:");
							ImGui::SameLine();
							ImGui::IntSliderWithEdit("##RenderFPSInterval", &user->render.wm_render_interval, 15, 5000, "%d ms");

						}
					}
					ImGui::EndGroup();

					ImGui::Text(settings->is_first_run ? "First Run: Yes" : "First Run: No");
					ImGui::Text(HVKSYS::SupportsDX12() ? "Device Supports DX12: Yes" : "Device Supports DX12: No");
					ImGui::Text(g_App.g_RenderBackend == RenderBackend::DX12 ? "Rendering Engine Used: DX12" : "Rendering Engine Used: DX11");
					if (g_App.g_RenderBackend == RenderBackend::DX11)
					{
						ImGui::Text("DX11 textures: frames=%d bg=%p",
							(int)g_LoadingFrames.size(),
							BgTexture);
					}
					else
					{
						ImGui::Text("DX12 textures: frames=%d bg=%p",
							(int)g_LoadingFrames.size(),
							BgTexture);
					}

					ImGui::Spacing(12.0f);
					ImGui::Separator();
					ImGui::Spacing(12.0f);

					static std::wstring bgPath = user->render.bg_image_path;
					std::wstring base = HVKIO::GetLocalAppDataW() + L"\\PSHVK\\";
					std::wstring autopath = HVKIO::GetLocalAppDataW() + L"assets\\";

					if (ImGui::ImGui_FilePicker(
						"Background Image",
						bgPath,
						autopath.c_str(),
						L"Images (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0"))
					{
						user->render.bg_image_path = bgPath;
						RequestBackgroundReload(bgPath, settings, user);
						std::thread(BgReloadWorker).detach();
						}

					ImGui::Spacing(20.0f);

					std::string dbg_img_path = "Stored Variable Value : " + WStringToUtf8(user->render.bg_image_path);
					const char* dbg_img_path_cstr = dbg_img_path.c_str();

					ImGui::Text(dbg_img_path_cstr);

					ImGui::Spacing(12.0f);
					ImGui::Separator();
					ImGui::Spacing(12.0f);

					ImGui::Text(HVKIO::ValidateInstanceFile() ? "Valid Signature: Yes" : "Valid Signature: No");

				}
                                ImGui::End();
                        }
                }

                DebugLog("Frame %llu: finished non-loading UI branch", (unsigned long long)frameIndex);

                DebugLog("Frame %llu: before loading screen branch", (unsigned long long)frameIndex);
                if (settings->isLoading)
                {
                        ImTextureID tex = (ImTextureID)nullptr;
                        if (g_texturesReady.load() && !g_LoadingFrames.empty())
                        {
				const int endIdx = (int)g_LoadingFrames.size() - 1;
				tex = TextureLoader::CycleFrames(g_LoadingFrames, 0, endIdx);
			}



			const bool isLight = (user->style.loading_theme == LoadingTheme::LIGHTMODE);

                        bg->AddRectFilled(
                                ImVec2(0, 0),
                                ImVec2(io.DisplaySize.x, io.DisplaySize.y),
                                isLight ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 255)
                        );
                        DebugLog("Frame %llu: drew loading screen background", (unsigned long long)frameIndex);



			if (GetAsyncKeyState(VK_UP) & 1)
				scale += 0.05f;

			if (GetAsyncKeyState(VK_DOWN) & 1)
				scale -= 0.05f;

			float w = 1920.0f * scale;
			float h = 1080.0f * scale;

			float cx = io.DisplaySize.x * 0.5f;
			float cy = io.DisplaySize.y * 0.5f;


                        if (tex)
                        {
                                bg->AddImage(
                                        tex,
                                        ImVec2(cx - w * 0.5f, cy - h * 0.5f),
                                        ImVec2(cx + w * 0.5f, cy + h * 0.5f)
                                );
                                DebugLog("Frame %llu: drew loading animation frame", (unsigned long long)frameIndex);
                        }

                        // printf("Scale: %.2f\n", scale);
                }

                DebugLog("Frame %llu: before ImGui::Render", (unsigned long long)frameIndex);

                // Rendering
                ImGui::Render();
                DebugLog("Frame %llu: after ImGui::Render", (unsigned long long)frameIndex);

                if (g_App.g_RenderBackend == RenderBackend::DX12)
                {
                        DebugLog("Frame %llu: entering DX12 render path", (unsigned long long)frameIndex);
                        g_GlowPipeline12.Resize(g_pd3dDevice, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
                        FrameContext* frameCtx = WaitForNextFrameContext();
                        TextureLoader::ProcessDeferredTextureFrees();
                        UINT backBufferIdx = g_pSwapChain->GetCurrentBackBufferIndex();
                        frameCtx->CommandAllocator->Reset();

			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = g_mainRenderTargetResource[backBufferIdx];
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

			g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);
                        g_pd3dCommandList->ResourceBarrier(1, &barrier);

                        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
                        g_pd3dCommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);
                        D3D12_VIEWPORT mainViewport{ 0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y, 0.0f, 1.0f };
                        D3D12_RECT scissor{ 0, 0, (LONG)io.DisplaySize.x, (LONG)io.DisplaySize.y };
                        g_GlowPipeline12.Render(g_pd3dCommandList, ImGui::GetDrawData(), g_pd3dSrvDescHeap, g_mainRenderTargetDescriptor[backBufferIdx], mainViewport, scissor, g_GlowSettings);

                        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                        g_pd3dCommandList->ResourceBarrier(1, &barrier);
			g_pd3dCommandList->Close();

			g_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_pd3dCommandList);
			g_pd3dCommandQueue->Signal(g_fence, ++g_fenceLastSignaledValue);
			frameCtx->FenceValue = g_fenceLastSignaledValue;

                        HRESULT hr;
                        if (settings->vsync)
                                hr = g_pSwapChain->Present(1, 0);
                        else
                                hr = g_pSwapChain->Present(0, g_SwapChainTearingSupport ? DXGI_PRESENT_ALLOW_TEARING : 0);

                        DebugLog("Frame %llu: DX12 Present hr=0x%08lx", (unsigned long long)frameIndex, (unsigned long)hr);

                        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
                        g_frameIndex++;
                }
                else
                {
                        DebugLog("Frame %llu: entering DX11 render path", (unsigned long long)frameIndex);
                        g_GlowPipeline11.Resize(g_pd3dDevice11, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
                        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
                        g_pd3dDeviceContext11->OMSetRenderTargets(1, &g_mainRenderTargetView11, nullptr);
                        g_pd3dDeviceContext11->ClearRenderTargetView(g_mainRenderTargetView11, clear_color_with_alpha);

                        g_GlowPipeline11.Render(g_pd3dDeviceContext11, ImGui::GetDrawData(), g_mainRenderTargetView11, io.DisplaySize, g_GlowSettings);

                        g_pSwapChain11->Present(settings->vsync ? 1 : 0, 0);
                        DebugLog("Frame %llu: DX11 Present completed", (unsigned long long)frameIndex);
                }

		if (!settings->vsync)
			g_fpsLimiter.Limit();
	}

	if (g_App.g_RenderBackend == RenderBackend::DX12)
		WaitForPendingOperations();

	g_texStop.store(true);
	if (texThread.joinable())
		texThread.join();

	Display::RestoreResolution();

	// Realease Textures 
	{
		std::lock_guard<std::mutex> lock(g_texMutex);

		for (auto& t : g_LoadingFrames)
			TextureLoader::FreeTexture(t, g_App);

                g_LoadingFrames.clear();

                if (BgTexture)
                {
                        TextureLoader::FreeTexture(bg, g_App);
                        bg = {};
                        BgTexture = (ImTextureID)nullptr;
                }
        }


	// Cleanup
	if (g_App.g_RenderBackend == RenderBackend::DX12)
		ImGui_ImplDX12_Shutdown();
	else
		ImGui_ImplDX11_Shutdown();

	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	if (g_App.g_RenderBackend == RenderBackend::DX12)
		CleanupDeviceD3D();
	else
		HVKSYS::CleanupDeviceD3D11();


	CleanupDeviceD3D();
	::DestroyWindow(hwnd);
	::UnregisterClassW(wc.lpszClassName, wc.hInstance);

	timeEndPeriod(1);

	return 0;
}

// Helper functions

bool HVKSYS::InitDX12(HWND hwnd)
{
        DebugLog("InitDX12: initializing device and swap chain");
        return CreateDeviceD3D(hwnd);
}

bool HVKSYS::InitDX11(HWND hwnd)
{
        DebugLog("InitDX11: initializing D3D11 device and swap chain");
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hwnd;
	sd.SampleDesc.Count = 1;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
		createDeviceFlags,
		featureLevels,
		1,
		D3D11_SDK_VERSION,
		&sd,
		&g_pSwapChain11,
		&g_pd3dDevice11,
		&featureLevel,
                &g_pd3dDeviceContext11
        );

        if (FAILED(hr))
        {
                DebugLog("InitDX11: D3D11CreateDeviceAndSwapChain failed (hr=0x%lx)", hr);
                return false;
        }

        CreateRenderTargetDX11();
        g_GlowPipeline11.Initialize(g_pd3dDevice11);
        DebugLog("InitDX11: completed successfully");
        return true;
}



bool CreateDeviceD3D(HWND hWnd)
{
        DebugLog("CreateDeviceD3D: starting device creation");
        // Setup swap chain
        // This is a basic setup. Optimally could handle fullscreen mode differently. See #8979 for suggestions.
        DXGI_SWAP_CHAIN_DESC1 sd;
	{
		ZeroMemory(&sd, sizeof(sd));
		sd.BufferCount = APP_NUM_BACK_BUFFERS;
		sd.Width = 0;
		sd.Height = 0;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		sd.Scaling = DXGI_SCALING_STRETCH;
		sd.Stereo = FALSE;
	}

	// [DEBUG] Enable debug interface
#ifdef DX12_ENABLE_DEBUG_LAYER
        ID3D12Debug* pdx12Debug = nullptr;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug))))
        {
                DebugLog("CreateDeviceD3D: enabling DX12 debug layer");
                pdx12Debug->EnableDebugLayer();
        }
#endif

        // Create device
        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
        if (D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&g_pd3dDevice)) != S_OK)
        {
                DebugLog("CreateDeviceD3D: D3D12CreateDevice failed");
                return false;
        }

        // [DEBUG] Setup debug interface to break on any warnings/errors
#ifdef DX12_ENABLE_DEBUG_LAYER
	if (pdx12Debug != nullptr)
	{
		ID3D12InfoQueue* pInfoQueue = nullptr;
		g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		// Disable breaking on this warning because of a suspected bug in the D3D12 SDK layer, see #9084 for details.
		const int D3D12_MESSAGE_ID_FENCE_ZERO_WAIT_ = 1424; // not in all copies of d3d12sdklayers.h
		D3D12_MESSAGE_ID disabledMessages[] = { (D3D12_MESSAGE_ID)D3D12_MESSAGE_ID_FENCE_ZERO_WAIT_ };
		D3D12_INFO_QUEUE_FILTER filter = {};
		filter.DenyList.NumIDs = 1;
		filter.DenyList.pIDList = disabledMessages;
		pInfoQueue->AddStorageFilterEntries(&filter);

		pInfoQueue->Release();
		pdx12Debug->Release();
	}
#endif

        {
                D3D12_DESCRIPTOR_HEAP_DESC desc = {};
                desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                desc.NumDescriptors = APP_NUM_BACK_BUFFERS;
                desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                desc.NodeMask = 1;
                if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dRtvDescHeap)) != S_OK)
                {
                        DebugLog("CreateDeviceD3D: failed to create RTV descriptor heap");
                        return false;
                }

                SIZE_T rtvDescriptorSize = g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
                D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
		for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
		{
			g_mainRenderTargetDescriptor[i] = rtvHandle;
			rtvHandle.ptr += rtvDescriptorSize;
		}
	}

        {
                D3D12_DESCRIPTOR_HEAP_DESC desc = {};
                desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                desc.NumDescriptors = APP_SRV_HEAP_SIZE;
                desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap)) != S_OK)
                {
                        DebugLog("CreateDeviceD3D: failed to create SRV descriptor heap");
                        return false;
                }
                g_pd3dSrvDescHeapAlloc.Create(g_pd3dDevice, g_pd3dSrvDescHeap);
        }

        {
                D3D12_COMMAND_QUEUE_DESC desc = {};
                desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
                desc.NodeMask = 1;
                if (g_pd3dDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_pd3dCommandQueue)) != S_OK)
                {
                        DebugLog("CreateDeviceD3D: failed to create command queue");
                        return false;
                }
        }

        for (UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++)
                if (g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frameContext[i].CommandAllocator)) != S_OK)
                {
                        DebugLog("CreateDeviceD3D: failed to create command allocator %u", i);
                        return false;
                }

        if (g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frameContext[0].CommandAllocator, nullptr, IID_PPV_ARGS(&g_pd3dCommandList)) != S_OK ||
                g_pd3dCommandList->Close() != S_OK)
        {
                DebugLog("CreateDeviceD3D: failed to create or close primary command list");
                return false;
        }

        // Dedicated upload allocator/list (kept separate from per-frame allocators)
        if (g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_pd3dUploadCmdAlloc)) != S_OK)
        {
                DebugLog("CreateDeviceD3D: failed to create upload command allocator");
                return false;
        }

        if (g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_pd3dUploadCmdAlloc, nullptr, IID_PPV_ARGS(&g_pd3dUploadCmdList)) != S_OK ||
                g_pd3dUploadCmdList->Close() != S_OK)
        {
                DebugLog("CreateDeviceD3D: failed to create or close upload command list");
                return false;
        }

        if (g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)) != S_OK)
        {
                DebugLog("CreateDeviceD3D: failed to create fence");
                return false;
        }

        g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (g_fenceEvent == nullptr)
        {
                DebugLog("CreateDeviceD3D: failed to create fence event");
                return false;
        }

        {
                IDXGIFactory5* dxgiFactory = nullptr;
                IDXGISwapChain1* swapChain1 = nullptr;
                if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK)
                {
                        DebugLog("CreateDeviceD3D: CreateDXGIFactory1 failed");
                        return false;
                }

                BOOL allow_tearing = FALSE;
                dxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing));
                g_SwapChainTearingSupport = (allow_tearing == TRUE);
                if (g_SwapChainTearingSupport)
                {
                        DebugLog("CreateDeviceD3D: tearing supported, enabling flag");
                        sd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
                }

                if (dxgiFactory->CreateSwapChainForHwnd(g_pd3dCommandQueue, hWnd, &sd, nullptr, nullptr, &swapChain1) != S_OK)
                {
                        DebugLog("CreateDeviceD3D: CreateSwapChainForHwnd failed");
                        return false;
                }
                if (swapChain1->QueryInterface(IID_PPV_ARGS(&g_pSwapChain)) != S_OK)
                {
                        DebugLog("CreateDeviceD3D: failed to query IDXGISwapChain3");
                        return false;
                }
                if (g_SwapChainTearingSupport)
                        dxgiFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

                swapChain1->Release();
                dxgiFactory->Release();
                g_pSwapChain->SetMaximumFrameLatency(APP_NUM_BACK_BUFFERS);
                g_hSwapChainWaitableObject = g_pSwapChain->GetFrameLatencyWaitableObject();
        }

        g_GlowPipeline12.Initialize(g_pd3dDevice);
        DebugLog("CreateDeviceD3D: glow pipeline initialized");
        CreateRenderTarget();
        DebugLog("CreateDeviceD3D: completed successfully");
        return true;
}

void CleanupDeviceD3D()
{
        DebugLog("CleanupDeviceD3D: releasing resources");
        CleanupRenderTarget();
        if (g_pSwapChain) { g_pSwapChain->SetFullscreenState(false, nullptr); g_pSwapChain->Release(); g_pSwapChain = nullptr; }
        if (g_hSwapChainWaitableObject != nullptr) { CloseHandle(g_hSwapChainWaitableObject); }
        for (UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++)
                if (g_frameContext[i].CommandAllocator) { g_frameContext[i].CommandAllocator->Release(); g_frameContext[i].CommandAllocator = nullptr; }
	if (g_pd3dCommandQueue) { g_pd3dCommandQueue->Release(); g_pd3dCommandQueue = nullptr; }
	if (g_pd3dCommandList) { g_pd3dCommandList->Release(); g_pd3dCommandList = nullptr; }
	if (g_pd3dUploadCmdList) { g_pd3dUploadCmdList->Release();  g_pd3dUploadCmdList = nullptr; }
	if (g_pd3dUploadCmdAlloc) { g_pd3dUploadCmdAlloc->Release(); g_pd3dUploadCmdAlloc = nullptr; }
	if (g_pd3dRtvDescHeap) { g_pd3dRtvDescHeap->Release(); g_pd3dRtvDescHeap = nullptr; }
	if (g_pd3dSrvDescHeap) { g_pd3dSrvDescHeap->Release(); g_pd3dSrvDescHeap = nullptr; }
        if (g_fence) { g_fence->Release(); g_fence = nullptr; }
        if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }
        if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }

        DebugLog("CleanupDeviceD3D: completed");

#ifdef DX12_ENABLE_DEBUG_LAYER
        IDXGIDebug1* pDebug = nullptr;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
        {
		pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
		pDebug->Release();
	}
#endif
}

void CreateRenderTarget()
{
        DebugLog("CreateRenderTarget: creating %d back buffers", APP_NUM_BACK_BUFFERS);
        for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
        {
                ID3D12Resource* pBackBuffer = nullptr;
                g_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
                g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, g_mainRenderTargetDescriptor[i]);
                g_mainRenderTargetResource[i] = pBackBuffer;
        }
        DebugLog("CreateRenderTarget: completed");
}

void CleanupRenderTarget()
{
        DebugLog("CleanupRenderTarget: waiting for GPU and releasing targets");
        WaitForPendingOperations();

        for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
                if (g_mainRenderTargetResource[i]) { g_mainRenderTargetResource[i]->Release(); g_mainRenderTargetResource[i] = nullptr; }
}

void HVKSYS::CreateRenderTargetDX11()
{
        DebugLog("CreateRenderTargetDX11: creating render target view");
        ID3D11Texture2D* pBackBuffer = nullptr;
        g_pSwapChain11->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
        if (pBackBuffer)
        {
                g_pd3dDevice11->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView11);
                pBackBuffer->Release();
        }
        DebugLog("CreateRenderTargetDX11: completed");
}

void HVKSYS::CleanupRenderTargetDX11()
{
        DebugLog("CleanupRenderTargetDX11: releasing render target");
        if (g_mainRenderTargetView11) { g_mainRenderTargetView11->Release(); g_mainRenderTargetView11 = nullptr; }
}

void HVKSYS::CleanupDeviceD3D11()
{
        DebugLog("CleanupDeviceD3D11: releasing D3D11 resources");
        HVKSYS::CleanupRenderTargetDX11();
        if (g_pSwapChain11) { g_pSwapChain11->Release(); g_pSwapChain11 = nullptr; }
        if (g_pd3dDeviceContext11) { g_pd3dDeviceContext11->Release(); g_pd3dDeviceContext11 = nullptr; }
        if (g_pd3dDevice11) { g_pd3dDevice11->Release(); g_pd3dDevice11 = nullptr; }
        DebugLog("CleanupDeviceD3D11: completed");
}


void WaitForPendingOperations()
{
        DebugLog("WaitForPendingOperations: signaling fence value=%llu", g_fenceLastSignaledValue + 1);
        g_pd3dCommandQueue->Signal(g_fence, ++g_fenceLastSignaledValue);

        g_fence->SetEventOnCompletion(g_fenceLastSignaledValue, g_fenceEvent);
        ::WaitForSingleObject(g_fenceEvent, INFINITE);
        DebugLog("WaitForPendingOperations: completed");
}

FrameContext* WaitForNextFrameContext()
{
        DebugLog("WaitForNextFrameContext: waiting for frame %u", g_frameIndex % APP_NUM_FRAMES_IN_FLIGHT);
        FrameContext* frame_context = &g_frameContext[g_frameIndex % APP_NUM_FRAMES_IN_FLIGHT];
        if (g_fence->GetCompletedValue() < frame_context->FenceValue)
        {
                g_fence->SetEventOnCompletion(frame_context->FenceValue, g_fenceEvent);
                HANDLE waitableObjects[] = { g_hSwapChainWaitableObject, g_fenceEvent };
                ::WaitForMultipleObjects(2, waitableObjects, TRUE, INFINITE);
        }
        else
                ::WaitForSingleObject(g_hSwapChainWaitableObject, INFINITE);

        DebugLog("WaitForNextFrameContext: frame %u ready", g_frameIndex % APP_NUM_FRAMES_IN_FLIGHT);
        return frame_context;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			return 0;

		if (g_App.g_RenderBackend == RenderBackend::DX12)
		{
			if (g_pd3dDevice != nullptr)
			{
				CleanupRenderTarget();
                                DXGI_SWAP_CHAIN_DESC1 desc = {};
                                g_pSwapChain->GetDesc1(&desc);
                                HRESULT result = g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), desc.Format, desc.Flags);
                                IM_ASSERT(SUCCEEDED(result) && "Failed to resize DX12 swapchain.");
                                CreateRenderTarget();
                                g_GlowPipeline12.Resize(g_pd3dDevice, (int)LOWORD(lParam), (int)HIWORD(lParam));
                        }
                }
                else
                {
                        if (g_pSwapChain11 != nullptr)
                        {
                                HVKSYS::CleanupRenderTargetDX11();
                                g_pSwapChain11->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                                HVKSYS::CreateRenderTargetDX11();
                                g_GlowPipeline11.Resize(g_pd3dDevice11, (int)LOWORD(lParam), (int)HIWORD(lParam));
                        }
                }
                return 0;

	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
