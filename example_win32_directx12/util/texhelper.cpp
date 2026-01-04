#include "texhelper.h"
#include <directx/d3dx12.h>
#include "descriptor_alloc.h"
#include <string>


#pragma comment(lib, "ole32.lib")

extern ExampleDescriptorHeapAllocator g_pd3dSrvDescHeapAlloc;

void TextureLoader::FreeTexture(HVKTexture& tex, AppState& g_App)
{
    if (!tex.id && !tex.emissiveId)
        return;

    if (g_App.g_RenderBackend == RenderBackend::DX11)
    {
        if (tex.baseSrv)
            tex.baseSrv->Release();
        if (tex.emissiveSrv)
            tex.emissiveSrv->Release();
    }
    else
    {
        auto freeHandle = [](D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
        {
            if (cpu.ptr || gpu.ptr)
                g_pd3dSrvDescHeapAlloc.Free(cpu, gpu);
        };

        freeHandle(tex.baseCpu, tex.baseGpu);
        freeHandle(tex.emissiveCpu, tex.emissiveGpu);

        if (tex.baseResource)
            tex.baseResource->Release();
        if (tex.emissiveResource)
            tex.emissiveResource->Release();
        if (tex.baseUpload)
            tex.baseUpload->Release();
        if (tex.emissiveUpload)
            tex.emissiveUpload->Release();
    }

    tex = {};
}


static std::wstring BuildEmissivePath(const std::wstring& basePath)
{
    const size_t dot = basePath.find_last_of(L'.');
    if (dot == std::wstring::npos)
        return basePath + L"_emissive";

    return basePath.substr(0, dot) + L"_emissive" + basePath.substr(dot);
}

static bool CreateSrvFromFile(
    ID3D11Device* device,
    const wchar_t* filename,
    ID3D11ShaderResourceView** outSrv,
    int* outWidth,
    int* outHeight)
{
    if (outSrv)
        *outSrv = nullptr;

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    ID3D11Texture2D* texture = nullptr;
    D3D11_TEXTURE2D_DESC desc{};
    UINT width = 0, height = 0;
    std::vector<uint8_t> pixels;
    D3D11_SUBRESOURCE_DATA sub{};

    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));

    if (FAILED(hr))
        goto cleanup;

    hr = factory->CreateDecoderFromFilename(
        filename,
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder);

    if (FAILED(hr))
        goto cleanup;

    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr))
        goto cleanup;

    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr))
        goto cleanup;

    hr = converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);

    if (FAILED(hr))
        goto cleanup;

    converter->GetSize(&width, &height);
    pixels.resize((size_t)width * height * 4);

    hr = converter->CopyPixels(
        nullptr,
        width * 4,
        (UINT)pixels.size(),
        pixels.data());

    if (FAILED(hr))
        goto cleanup;

    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    sub.pSysMem = pixels.data();
    sub.SysMemPitch = width * 4;

    hr = device->CreateTexture2D(&desc, &sub, &texture);
    if (FAILED(hr))
        goto cleanup;

    hr = device->CreateShaderResourceView(texture, nullptr, outSrv);
    if (FAILED(hr))
        goto cleanup;

    if (outWidth)
        *outWidth = (int)width;
    if (outHeight)
        *outHeight = (int)height;

cleanup:
    if (texture) texture->Release();
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();

    return *outSrv != nullptr;
}

bool TextureLoader::LoadTextureDX11FromFile(
    ID3D11Device* device,
    ID3D11DeviceContext*,
    const wchar_t* filename,
    HVKTexture& outTex)
{
    outTex = {};

    if (!CreateSrvFromFile(device, filename, &outTex.baseSrv, &outTex.width, &outTex.height))
        return false;

    outTex.id = (ImTextureID)outTex.baseSrv;

    const std::wstring emissivePath = BuildEmissivePath(filename);
    CreateSrvFromFile(device, emissivePath.c_str(), &outTex.emissiveSrv, nullptr, nullptr);

    if (outTex.emissiveSrv)
        outTex.emissiveId = (ImTextureID)outTex.emissiveSrv;

    return true;
}




bool TextureLoader::LoadTexture(
    const wchar_t* filePath,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    ExampleDescriptorHeapAllocator& alloc,
    HVKTexture& outTex)
{
    outTex = {};

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
        return false;

    HRESULT hr;
    IWICImagingFactory* factory = nullptr;
    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );
    if (FAILED(hr))
        return false;

    auto loadSingle = [&](const wchar_t* path,
        D3D12_CPU_DESCRIPTOR_HANDLE& cpu,
        D3D12_GPU_DESCRIPTOR_HANDLE& gpu,
        ID3D12Resource*& texture,
        ID3D12Resource*& uploadStorage,
        int& texWidth,
        int& texHeight) -> bool
    {
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;

        HRESULT localHr = factory->CreateDecoderFromFilename(
            path,
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            &decoder
        );

        if (FAILED(localHr))
            goto cleanup;

        localHr = decoder->GetFrame(0, &frame);
        if (FAILED(localHr))
            goto cleanup;

        localHr = factory->CreateFormatConverter(&converter);
        if (FAILED(localHr))
            goto cleanup;

        localHr = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom
        );

        if (FAILED(localHr))
            goto cleanup;

        UINT width = 0, height = 0;
        converter->GetSize(&width, &height);

        const UINT stride = width * 4;
        const UINT size = stride * height;

        std::vector<BYTE> pixels;
        pixels.resize(size);

        localHr = converter->CopyPixels(nullptr, stride, size, pixels.data());
        if (FAILED(localHr))
            goto cleanup;

        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        auto heapDefault = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        localHr = device->CreateCommittedResource(
            &heapDefault,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&texture)
        );

        if (FAILED(localHr) || !texture)
            goto cleanup;

        UINT64 uploadSize = GetRequiredIntermediateSize(texture, 0, 1);

        ID3D12Resource* uploadBuffer = nullptr;
        auto heapUpload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

        localHr = device->CreateCommittedResource(
            &heapUpload,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&uploadBuffer)
        );

        if (FAILED(localHr) || !uploadBuffer)
            goto cleanup;

        D3D12_SUBRESOURCE_DATA subData = {};
        subData.pData = pixels.data();
        subData.RowPitch = stride;
        subData.SlicePitch = size;

        UpdateSubresources(cmdList, texture, uploadBuffer, 0, 0, 1, &subData);

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            texture,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        cmdList->ResourceBarrier(1, &barrier);

        alloc.Alloc(&cpu, &gpu);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        device->CreateShaderResourceView(texture, &srvDesc, cpu);

        texWidth = (int)width;
        texHeight = (int)height;
        uploadStorage = uploadBuffer;

    cleanup:
        if (FAILED(localHr))
        {
            if (uploadBuffer)
                uploadBuffer->Release();
            if (texture)
            {
                texture->Release();
                texture = nullptr;
            }
        }
        if (converter) converter->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();

        return texture != nullptr;
    };

    if (!loadSingle(filePath, outTex.baseCpu, outTex.baseGpu, outTex.baseResource, outTex.baseUpload, outTex.width, outTex.height))
    {
        factory->Release();
        return false;
    }

    outTex.id = (ImTextureID)outTex.baseGpu.ptr;

    const std::wstring emissivePath = BuildEmissivePath(filePath);
    int emissiveWidth = 0, emissiveHeight = 0;
    loadSingle(emissivePath.c_str(), outTex.emissiveCpu, outTex.emissiveGpu, outTex.emissiveResource, outTex.emissiveUpload, emissiveWidth, emissiveHeight);

    if (outTex.emissiveGpu.ptr)
    {
        outTex.emissiveId = (ImTextureID)outTex.emissiveGpu.ptr;
        if (outTex.width == 0 && emissiveWidth > 0)
            outTex.width = emissiveWidth;
        if (outTex.height == 0 && emissiveHeight > 0)
            outTex.height = emissiveHeight;
    }

    factory->Release();
    return true;
}


// Pass the *loaded textures*, not file paths.
// Example: std::vector<ImTextureID> frames;

ImTextureID TextureLoader::CycleFrames(
    const std::vector<HVKTexture>& frames,
    int startFrameIdx,
    int endFrameIdx)
{
    static int currentFrame = -1;

    if (frames.empty())
        return (ImTextureID)nullptr;

    if (currentFrame < startFrameIdx || currentFrame > endFrameIdx)
        currentFrame = startFrameIdx;
    else
        currentFrame++;

    if (currentFrame > endFrameIdx)
        currentFrame = startFrameIdx;

    if (currentFrame < 0 || currentFrame >= (int)frames.size())
        return (ImTextureID)nullptr;

    return frames[currentFrame].id;
}


