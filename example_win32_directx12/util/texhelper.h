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
	ImTextureID id = (ImTextureID)nullptr;
	int width = 0;
	int height = 0;
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
	static ImTextureID LoadTexture(
		const wchar_t* filePath,
		ID3D12Device* device,
		ID3D12GraphicsCommandList* cmdList,
		class ExampleDescriptorHeapAllocator& alloc
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
