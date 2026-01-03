#pragma once


#include <windows.h>
#include <directx/d3d12.h>
#include <wincodec.h>
#include <vector>
#include <mutex>
#include "imgui.h"
#include "stb_image.h"
#include "../settings.h"

#include <d3d11.h>
#pragma comment(lib, "windowscodecs.lib")


struct HVKTexture
{
        HVKTexture() = default;
        explicit HVKTexture(ImTextureID baseId) : id(baseId) {}

        ImTextureID id = (ImTextureID)nullptr;
        ImTextureID emissiveId = (ImTextureID)nullptr;
        int width = 0;
        int height = 0;

        // DX11 resources
        ID3D11ShaderResourceView* baseSrv = nullptr;
        ID3D11ShaderResourceView* emissiveSrv = nullptr;

        // DX12 descriptors/resources
        D3D12_CPU_DESCRIPTOR_HANDLE baseCpu = { 0 };
        D3D12_GPU_DESCRIPTOR_HANDLE baseGpu = { 0 };
        ID3D12Resource* baseResource = nullptr;
        ID3D12Resource* baseUpload = nullptr;

        D3D12_CPU_DESCRIPTOR_HANDLE emissiveCpu = { 0 };
        D3D12_GPU_DESCRIPTOR_HANDLE emissiveGpu = { 0 };
        ID3D12Resource* emissiveResource = nullptr;
        ID3D12Resource* emissiveUpload = nullptr;
};

struct DeferredTextureFree
{
	HVKTexture tex;
	UINT64 fenceValue;
};

static std::vector<DeferredTextureFree> g_deferredFrees;


class TextureLoader
{
public:
	static void ProcessDeferredTextureFrees();
	static void DeferFreeTexture(const HVKTexture& tex);
	static bool ReloadBackgroundTexture(const std::wstring& newPath);

	// Loads a texture file and returns an ImGui texture ID
        static bool LoadTexture(
                const wchar_t* filePath,
                ID3D12Device* device,
                ID3D12GraphicsCommandList* cmdList,
                class ExampleDescriptorHeapAllocator& alloc,
                HVKTexture& outTex
        );

	// Pass in a vector of frames and it will return the next frame in the cycle as an ImTextureID
	static ImTextureID CycleFrames(
		const std::vector<HVKTexture>& frames,
		int startFrameIdx,
		int endFrameIdx);

	// DX11
	static bool LoadTextureDX11FromFile(
		ID3D11Device* device,
		ID3D11DeviceContext* context,
		const wchar_t* filename,
		HVKTexture& outTex);

	static void FreeTexture(HVKTexture& tex, AppState& g_App);
};
