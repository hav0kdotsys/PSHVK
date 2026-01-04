#include "glow_pipeline.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_dx12.h"
#include <d3dcompiler.h>
#include <cstring>
#include <vector>

static const char* kGlowShaderHLSL = R"HLSL(
cbuffer GlowConstants : register(b0)
{
    float2 Direction;
    float Radius;
    float Intensity;
};

struct VSOut
{
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

VSOut FullscreenVS(uint vid : SV_VertexID)
{
    float2 pos = float2((vid == 2) ? 3.0 : -1.0, (vid == 1) ? 3.0 : -1.0);
    float2 uv = float2((vid == 2) ? 2.0 : 0.0, (vid == 1) ? -1.0 : 1.0);
    VSOut o;
    o.Pos = float4(pos, 0.0, 1.0);
    o.UV = uv * 0.5f;
    return o;
}

Texture2D SceneTex : register(t0);
SamplerState LinearSamp : register(s0);

float4 BlurPS(VSOut input) : SV_TARGET
{
    float2 texel = Direction / max(Radius, 0.0001);
    float3 color = SceneTex.Sample(LinearSamp, input.UV).rgb * 0.28;
    color += SceneTex.Sample(LinearSamp, input.UV + texel * 1.5) .rgb * 0.24;
    color += SceneTex.Sample(LinearSamp, input.UV - texel * 1.5) .rgb * 0.24;
    color += SceneTex.Sample(LinearSamp, input.UV + texel * 3.0) .rgb * 0.12;
    color += SceneTex.Sample(LinearSamp, input.UV - texel * 3.0) .rgb * 0.12;
    return float4(color, 1.0);
}

float4 CompositePS(VSOut input) : SV_TARGET
{
    float3 bloom = SceneTex.Sample(LinearSamp, input.UV).rgb * Intensity;
    return float4(bloom, 1.0);
}
)HLSL";

struct GlowConstants
{
    float Direction[2];
    float Radius;
    float Intensity;
};

// DX11 implementation
void GlowPipelineDX11::Initialize(ID3D11Device* device)
{
        CreateShaders(device);
}

void GlowPipelineDX11::Shutdown()
{
        if (EmissiveRT) { EmissiveRT->Release(); EmissiveRT = nullptr; }
        if (EmissiveRTV) { EmissiveRTV->Release(); EmissiveRTV = nullptr; }
        if (EmissiveSRV) { EmissiveSRV->Release(); EmissiveSRV = nullptr; }
        if (BlurATex) { BlurATex->Release(); BlurATex = nullptr; }
        if (BlurARTV) { BlurARTV->Release(); BlurARTV = nullptr; }
        if (BlurASRV) { BlurASRV->Release(); BlurASRV = nullptr; }
        if (BlurBTex) { BlurBTex->Release(); BlurBTex = nullptr; }
        if (BlurBRTV) { BlurBRTV->Release(); BlurBRTV = nullptr; }
        if (BlurBSRV) { BlurBSRV->Release(); BlurBSRV = nullptr; }
        if (FullscreenVS) { FullscreenVS->Release(); FullscreenVS = nullptr; }
        if (BlurPS) { BlurPS->Release(); BlurPS = nullptr; }
        if (CompositePS) { CompositePS->Release(); CompositePS = nullptr; }
        if (LinearSampler) { LinearSampler->Release(); LinearSampler = nullptr; }
        if (AdditiveBlend) { AdditiveBlend->Release(); AdditiveBlend = nullptr; }
        if (ConstantBuffer) { ConstantBuffer->Release(); ConstantBuffer = nullptr; }
        Width = Height = 0;
}

void GlowPipelineDX11::Resize(ID3D11Device* device, int width, int height)
{
        if (width == Width && height == Height && EmissiveRT)
                return;
        Shutdown();
        Width = width;
        Height = height;
        CreateTargets(device, width, height);
        CreateShaders(device);
}

void GlowPipelineDX11::CreateTargets(ID3D11Device* device, int width, int height)
{
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        device->CreateTexture2D(&desc, nullptr, &EmissiveRT);
        device->CreateTexture2D(&desc, nullptr, &BlurATex);
        device->CreateTexture2D(&desc, nullptr, &BlurBTex);

        device->CreateRenderTargetView(EmissiveRT, nullptr, &EmissiveRTV);
        device->CreateRenderTargetView(BlurATex, nullptr, &BlurARTV);
        device->CreateRenderTargetView(BlurBTex, nullptr, &BlurBRTV);

        device->CreateShaderResourceView(EmissiveRT, nullptr, &EmissiveSRV);
        device->CreateShaderResourceView(BlurATex, nullptr, &BlurASRV);
        device->CreateShaderResourceView(BlurBTex, nullptr, &BlurBSRV);
}

void GlowPipelineDX11::CreateShaders(ID3D11Device* device)
{
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG;
#endif
        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* blurBlob = nullptr;
        ID3DBlob* compositeBlob = nullptr;
        ID3DBlob* errors = nullptr;

        D3DCompile(kGlowShaderHLSL, strlen(kGlowShaderHLSL), nullptr, nullptr, nullptr, "FullscreenVS", "vs_5_0", flags, 0, &vsBlob, &errors);
        if (errors) errors->Release();
        D3DCompile(kGlowShaderHLSL, strlen(kGlowShaderHLSL), nullptr, nullptr, nullptr, "BlurPS", "ps_5_0", flags, 0, &blurBlob, &errors);
        if (errors) errors->Release();
        D3DCompile(kGlowShaderHLSL, strlen(kGlowShaderHLSL), nullptr, nullptr, nullptr, "CompositePS", "ps_5_0", flags, 0, &compositeBlob, &errors);
        if (errors) errors->Release();

        if (vsBlob)
        {
                device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &FullscreenVS);
                vsBlob->Release();
        }
        if (blurBlob)
        {
                device->CreatePixelShader(blurBlob->GetBufferPointer(), blurBlob->GetBufferSize(), nullptr, &BlurPS);
                blurBlob->Release();
        }
        if (compositeBlob)
        {
                device->CreatePixelShader(compositeBlob->GetBufferPointer(), compositeBlob->GetBufferSize(), nullptr, &CompositePS);
                compositeBlob->Release();
        }

        if (!LinearSampler)
        {
                D3D11_SAMPLER_DESC samp{};
                samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
                device->CreateSamplerState(&samp, &LinearSampler);
        }
        if (!AdditiveBlend)
        {
                D3D11_BLEND_DESC blend{};
                blend.RenderTarget[0].BlendEnable = TRUE;
                blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
                blend.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
                blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
                blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
                blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
                blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
                blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
                device->CreateBlendState(&blend, &AdditiveBlend);
        }
        if (!ConstantBuffer)
        {
                D3D11_BUFFER_DESC cb{};
                cb.ByteWidth = sizeof(GlowConstants);
                cb.Usage = D3D11_USAGE_DYNAMIC;
                cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                device->CreateBuffer(&cb, nullptr, &ConstantBuffer);
        }
}

void GlowPipelineDX11::RunBlurPass(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* input_srv, ID3D11RenderTargetView* output_rtv, const ImVec2& direction, const GlowSettings& settings)
{
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
                auto* data = reinterpret_cast<GlowConstants*>(mapped.pData);
                data->Direction[0] = direction.x;
                data->Direction[1] = direction.y;
                data->Radius = settings.Radius;
                data->Intensity = settings.Intensity;
                ctx->Unmap(ConstantBuffer, 0);
        }

        ID3D11ShaderResourceView* srvs[] = { input_srv };
        ctx->OMSetRenderTargets(1, &output_rtv, nullptr);
        ctx->VSSetShader(FullscreenVS, nullptr, 0);
        ctx->PSSetShader(BlurPS, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, srvs);
        ctx->PSSetSamplers(0, 1, &LinearSampler);
        ctx->PSSetConstantBuffers(0, 1, &ConstantBuffer);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->Draw(3, 0);
        ID3D11ShaderResourceView* nullSrv[] = { nullptr };
        ctx->PSSetShaderResources(0, 1, nullSrv);
}

void GlowPipelineDX11::Composite(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* target, ID3D11ShaderResourceView* bloom_srv, const GlowSettings& settings)
{
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            auto* data = reinterpret_cast<GlowConstants*>(mapped.pData);
            data->Direction[0] = 0.0f;
            data->Direction[1] = 0.0f;
            data->Radius = settings.Radius;
            data->Intensity = settings.Intensity;
            ctx->Unmap(ConstantBuffer, 0);
        }

        float blendFactor[4] = { 0,0,0,0 };
        ctx->OMSetBlendState(AdditiveBlend, blendFactor, 0xffffffff);
        ctx->OMSetRenderTargets(1, &target, nullptr);
        ctx->VSSetShader(FullscreenVS, nullptr, 0);
        ctx->PSSetShader(CompositePS, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &bloom_srv);
        ctx->PSSetSamplers(0, 1, &LinearSampler);
        ctx->PSSetConstantBuffers(0, 1, &ConstantBuffer);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->Draw(3, 0);
        ID3D11ShaderResourceView* nullSrv[] = { nullptr };
        ctx->PSSetShaderResources(0, 1, nullSrv);
        ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
}

void GlowPipelineDX11::Render(ID3D11DeviceContext* ctx, ImDrawData* draw_data, ID3D11RenderTargetView* main_rtv, const ImVec2& viewport, const GlowSettings& settings)
{
        if (!EmissiveRTV)
                return;

        D3D11_VIEWPORT vp{};
        vp.Width = viewport.x;
        vp.Height = viewport.y;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = vp.TopLeftY = 0.0f;
        ctx->RSSetViewports(1, &vp);

        // Render emissive only
        const float black[4] = { 0,0,0,0 };
        ctx->OMSetRenderTargets(1, &EmissiveRTV, nullptr);
        ctx->ClearRenderTargetView(EmissiveRTV, black);
        ImGui_ImplDX11_RenderDrawData(draw_data);

        // Blur horizontally then vertically
        RunBlurPass(ctx, EmissiveSRV, BlurARTV, ImVec2(1.0f, 0.0f), settings);
        RunBlurPass(ctx, BlurASRV, BlurBRTV, ImVec2(0.0f, 1.0f), settings);

        // Render base UI
        ctx->OMSetRenderTargets(1, &main_rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(draw_data);

        // Composite glow
        Composite(ctx, main_rtv, BlurBSRV, settings);
}

// DX12 implementation helper static HLSL compiled per backend
static void CompileShader(const char* entry, const char* target, ID3DBlob** blob)
{
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG;
#endif
        ID3DBlob* errors = nullptr;
        D3DCompile(kGlowShaderHLSL, strlen(kGlowShaderHLSL), nullptr, nullptr, nullptr, entry, target, flags, 0, blob, &errors);
        if (errors)
                errors->Release();
}

void GlowPipelineDX12::Initialize(ID3D12Device* device)
{
        Width = Height = 0;
        CreatePipeline(device);
}

void GlowPipelineDX12::Shutdown()
{
        if (EmissiveRT) { EmissiveRT->Release(); EmissiveRT = nullptr; }
        if (BlurATex) { BlurATex->Release(); BlurATex = nullptr; }
        if (BlurBTex) { BlurBTex->Release(); BlurBTex = nullptr; }
        if (RTVHeap) { RTVHeap->Release(); RTVHeap = nullptr; }
        if (SRVHeap) { SRVHeap->Release(); SRVHeap = nullptr; }
        if (RootSignature) { RootSignature->Release(); RootSignature = nullptr; }
        if (BlurPSO) { BlurPSO->Release(); BlurPSO = nullptr; }
        if (CompositePSO) { CompositePSO->Release(); CompositePSO = nullptr; }
        Width = Height = 0;
}

void GlowPipelineDX12::Resize(ID3D12Device* device, int width, int height)
{
        if (width == Width && height == Height && EmissiveRT)
                return;
        Shutdown();
        Width = width;
        Height = height;
        CreatePipeline(device);
        CreateTargets(device, width, height);
        CreateDescriptors(device);
}

void GlowPipelineDX12::CreateTargets(ID3D12Device* device, int width, int height)
{
        if (width <= 0 || height <= 0)
                return;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        FLOAT clearValue[4] = { 0,0,0,0 };
        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        memcpy(clear.Color, clearValue, sizeof(clearValue));

        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&EmissiveRT));
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&BlurATex));
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&BlurBTex));

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.NumDescriptors = 3;
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&RTVHeap));

        UINT rtvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        EmissiveRTV = RTVHeap->GetCPUDescriptorHandleForHeapStart();
        BlurARTV = { EmissiveRTV.ptr + rtvInc };
        BlurBRTV = { EmissiveRTV.ptr + rtvInc * 2 };

        device->CreateRenderTargetView(EmissiveRT, nullptr, EmissiveRTV);
        device->CreateRenderTargetView(BlurATex, nullptr, BlurARTV);
        device->CreateRenderTargetView(BlurBTex, nullptr, BlurBRTV);
}

void GlowPipelineDX12::CreateDescriptors(ID3D12Device* device)
{
        if (!EmissiveRT || !BlurATex || !BlurBTex)
                return;
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 3;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&SRVHeap));

        UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        EmissiveCPU = SRVHeap->GetCPUDescriptorHandleForHeapStart();
        BlurACPU = { EmissiveCPU.ptr + descriptorSize };
        BlurBCPU = { EmissiveCPU.ptr + descriptorSize * 2 };
        EmissiveGPU = SRVHeap->GetGPUDescriptorHandleForHeapStart();
        BlurAGPU = { EmissiveGPU.ptr + descriptorSize };
        BlurBGPU = { EmissiveGPU.ptr + descriptorSize * 2 };

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;

        device->CreateShaderResourceView(EmissiveRT, &srv, EmissiveCPU);
        device->CreateShaderResourceView(BlurATex, &srv, BlurACPU);
        device->CreateShaderResourceView(BlurBTex, &srv, BlurBCPU);
}

void GlowPipelineDX12::CreatePipeline(ID3D12Device* device)
{
        if (RootSignature)
                return;
        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* blurBlob = nullptr;
        ID3DBlob* compositeBlob = nullptr;
        CompileShader("FullscreenVS", "vs_5_0", &vsBlob);
        CompileShader("BlurPS", "ps_5_0", &blurBlob);
        CompileShader("CompositePS", "ps_5_0", &compositeBlob);

        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 1;
        params[0].DescriptorTable.pDescriptorRanges = &range;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants.Num32BitValues = 4;
        params[1].Constants.RegisterSpace = 0;
        params[1].Constants.ShaderRegister = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters = 2;
        rsDesc.pParameters = params;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers = &sampler;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* rsBlob = nullptr;
        ID3DBlob* rsError = nullptr;
        D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError);
        if (rsError) rsError->Release();
        device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&RootSignature));
        if (rsBlob) rsBlob->Release();

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = RootSignature;
        pso.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        pso.PS = { blurBlob->GetBufferPointer(), blurBlob->GetBufferSize() };
        pso.BlendState.RenderTarget[0].BlendEnable = FALSE;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.FrontCounterClockwise = FALSE;
        pso.RasterizerState.DepthClipEnable = TRUE;
        pso.DepthStencilState.DepthEnable = FALSE;
        pso.DepthStencilState.StencilEnable = FALSE;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        pso.SampleDesc.Count = 1;
        pso.SampleMask = UINT_MAX;

        device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&BlurPSO));

        pso.PS = { compositeBlob->GetBufferPointer(), compositeBlob->GetBufferSize() };
        pso.BlendState.RenderTarget[0].BlendEnable = TRUE;
        pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        pso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

        device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&CompositePSO));

        if (vsBlob) vsBlob->Release();
        if (blurBlob) blurBlob->Release();
        if (compositeBlob) compositeBlob->Release();
}

void GlowPipelineDX12::RunBlurPass(ID3D12GraphicsCommandList* cmd, ID3D12Resource* input, D3D12_CPU_DESCRIPTOR_HANDLE output_rtv, D3D12_GPU_DESCRIPTOR_HANDLE input_gpu_srv, const ImVec2& direction, const GlowSettings& settings)
{
        if (!input || !BlurPSO) return;
        cmd->SetPipelineState(BlurPSO);
        cmd->SetGraphicsRootSignature(RootSignature);
        D3D12_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f, 1.0f };
        D3D12_RECT rect{ 0,0,Width,Height };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &rect);
        cmd->OMSetRenderTargets(1, &output_rtv, FALSE, nullptr);

        GlowConstants constants{};
        constants.Direction[0] = direction.x;
        constants.Direction[1] = direction.y;
        constants.Radius = settings.Radius;
        constants.Intensity = settings.Intensity;
        cmd->SetGraphicsRoot32BitConstants(1, 4, &constants, 0);
        cmd->SetGraphicsRootDescriptorTable(0, input_gpu_srv);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
}

void GlowPipelineDX12::Composite(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE target_rtv, D3D12_GPU_DESCRIPTOR_HANDLE bloom_srv, const GlowSettings& settings)
{
        if (!CompositePSO) return;
        cmd->SetPipelineState(CompositePSO);
        cmd->SetGraphicsRootSignature(RootSignature);
        D3D12_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f, 1.0f };
        D3D12_RECT rect{ 0,0,Width,Height };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &rect);
        cmd->OMSetRenderTargets(1, &target_rtv, FALSE, nullptr);

        GlowConstants constants{};
        constants.Radius = settings.Radius;
        constants.Intensity = settings.Intensity;
        cmd->SetGraphicsRoot32BitConstants(1, 4, &constants, 0);
        cmd->SetGraphicsRootDescriptorTable(0, bloom_srv);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
}

void GlowPipelineDX12::Render(ID3D12GraphicsCommandList* cmd, ImDrawData* draw_data, ID3D12DescriptorHeap* imguiSrvHeap, D3D12_CPU_DESCRIPTOR_HANDLE main_rtv, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor, const GlowSettings& settings)
{
        if (!EmissiveRT || !draw_data || !SRVHeap)
                return;

        // Transition resources to render target
        D3D12_RESOURCE_BARRIER barriers[2]{};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = EmissiveRT;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        barriers[1] = barriers[0];
        barriers[1].Transition.pResource = BlurATex;

        cmd->ResourceBarrier(2, barriers);

        const float clear[4] = {0,0,0,0};
        cmd->OMSetRenderTargets(1, &EmissiveRTV, FALSE, nullptr);
        cmd->ClearRenderTargetView(EmissiveRTV, clear, 0, nullptr);
        cmd->SetDescriptorHeaps(1, &imguiSrvHeap);
        ImGui_ImplDX12_RenderDrawData(draw_data, cmd);

        // Blur passes
        barriers[0].Transition.pResource = EmissiveRT;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[1].Transition.pResource = BlurATex;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cmd->ResourceBarrier(2, barriers);
        cmd->SetDescriptorHeaps(1, &SRVHeap);
        RunBlurPass(cmd, EmissiveRT, BlurARTV, EmissiveGPU, ImVec2(1.0f, 0.0f), settings);

        barriers[0].Transition.pResource = BlurATex;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[1].Transition.pResource = BlurBTex;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cmd->ResourceBarrier(2, barriers);
        cmd->SetDescriptorHeaps(1, &SRVHeap);
        RunBlurPass(cmd, BlurATex, BlurBRTV, BlurAGPU, ImVec2(0.0f, 1.0f), settings);

        // Render base UI to backbuffer
        D3D12_RESOURCE_BARRIER blurBToSrv{};
        blurBToSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        blurBToSrv.Transition.pResource = BlurBTex;
        blurBToSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        blurBToSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        blurBToSrv.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        cmd->ResourceBarrier(1, &blurBToSrv);
        cmd->OMSetRenderTargets(1, &main_rtv, FALSE, nullptr);
        cmd->RSSetViewports(1, &viewport);
        cmd->RSSetScissorRects(1, &scissor);
        cmd->SetDescriptorHeaps(1, &imguiSrvHeap);
        ImGui_ImplDX12_RenderDrawData(draw_data, cmd);

        // Composite bloom
        cmd->SetDescriptorHeaps(1, &SRVHeap);
        Composite(cmd, main_rtv, BlurBGPU, settings);
}

