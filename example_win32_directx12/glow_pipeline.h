#pragma once

#include "imgui.h"
#include <d3d11.h>
#include <directx/d3d12.h>

struct GlowSettings
{
        float Radius = 4.0f;
        float Intensity = 1.0f;
};

class GlowPipelineDX11
{
public:
        void Initialize(ID3D11Device* device);
        void Shutdown();
        void Resize(ID3D11Device* device, int width, int height);
        void Render(ID3D11DeviceContext* ctx, ImDrawData* draw_data, ID3D11RenderTargetView* main_rtv, const ImVec2& viewport, const GlowSettings& settings);

private:
        void CreateTargets(ID3D11Device* device, int width, int height);
        void CreateShaders(ID3D11Device* device);
        void RunBlurPass(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* input_srv, ID3D11RenderTargetView* output_rtv, const ImVec2& direction, const GlowSettings& settings);
        void Composite(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* target, ID3D11ShaderResourceView* bloom_srv, const GlowSettings& settings);

        int Width = 0;
        int Height = 0;

        ID3D11Texture2D* EmissiveRT = nullptr;
        ID3D11RenderTargetView* EmissiveRTV = nullptr;
        ID3D11ShaderResourceView* EmissiveSRV = nullptr;

        ID3D11Texture2D* BlurATex = nullptr;
        ID3D11RenderTargetView* BlurARTV = nullptr;
        ID3D11ShaderResourceView* BlurASRV = nullptr;

        ID3D11Texture2D* BlurBTex = nullptr;
        ID3D11RenderTargetView* BlurBRTV = nullptr;
        ID3D11ShaderResourceView* BlurBSRV = nullptr;

        ID3D11VertexShader* FullscreenVS = nullptr;
        ID3D11PixelShader* BlurPS = nullptr;
        ID3D11PixelShader* CompositePS = nullptr;
        ID3D11SamplerState* LinearSampler = nullptr;
        ID3D11BlendState* AdditiveBlend = nullptr;
        ID3D11Buffer* ConstantBuffer = nullptr;
};

class GlowPipelineDX12
{
public:
        void Initialize(ID3D12Device* device);
        void Shutdown();
        void Resize(ID3D12Device* device, int width, int height);
        void Render(ID3D12GraphicsCommandList* cmd, ImDrawData* draw_data, ID3D12DescriptorHeap* imguiSrvHeap, D3D12_CPU_DESCRIPTOR_HANDLE main_rtv, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor, const GlowSettings& settings);

private:
        void CreateTargets(ID3D12Device* device, int width, int height);
        void CreateDescriptors(ID3D12Device* device);
        void CreatePipeline(ID3D12Device* device);
        void RunBlurPass(ID3D12GraphicsCommandList* cmd, ID3D12Resource* input, D3D12_CPU_DESCRIPTOR_HANDLE output_rtv, D3D12_GPU_DESCRIPTOR_HANDLE input_gpu_srv, const ImVec2& direction, const GlowSettings& settings);
        void Composite(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE target_rtv, D3D12_GPU_DESCRIPTOR_HANDLE bloom_srv, const GlowSettings& settings);

        int Width = 0;
        int Height = 0;

        ID3D12Resource* EmissiveRT = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE EmissiveRTV{};
        D3D12_CPU_DESCRIPTOR_HANDLE BlurARTV{};
        D3D12_CPU_DESCRIPTOR_HANDLE BlurBRTV{};

        D3D12_CPU_DESCRIPTOR_HANDLE EmissiveCPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE EmissiveGPU{};
        D3D12_CPU_DESCRIPTOR_HANDLE BlurACPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE BlurAGPU{};
        D3D12_CPU_DESCRIPTOR_HANDLE BlurBCPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE BlurBGPU{};

        ID3D12Resource* BlurATex = nullptr;
        ID3D12Resource* BlurBTex = nullptr;

        ID3D12DescriptorHeap* RTVHeap = nullptr;
        ID3D12DescriptorHeap* SRVHeap = nullptr;

        ID3D12RootSignature* RootSignature = nullptr;
        ID3D12PipelineState* BlurPSO = nullptr;
        ID3D12PipelineState* CompositePSO = nullptr;
};

