#include "texhelper.h"
#include <directx/d3dx12.h>
#include "descriptor_alloc.h"


#pragma comment(lib, "ole32.lib")

void TextureLoader::FreeTexture(HVKTexture& tex, AppState& g_App)
{
    if (!tex.id)
        return;

    if (g_App.g_RenderBackend == RenderBackend::DX11)
    {
        ((ID3D11ShaderResourceView*)tex.id)->Release();
    }
    // DX12 textures are released via descriptor heap allocator
    tex.id = (ImTextureID)nullptr;
}


bool TextureLoader::LoadTextureDX11FromFile(
    ID3D11Device* device,
    ID3D11DeviceContext*,
    const wchar_t* filename,
    HVKTexture& outTex)
{
    outTex = {};

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
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

    hr = device->CreateShaderResourceView(texture, nullptr, &srv);
    if (FAILED(hr))
        goto cleanup;

    outTex.id = (ImTextureID)srv;
    outTex.width = (int)width;
    outTex.height = (int)height;

cleanup:
    if (texture) texture->Release();
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();

    return outTex.id != (ImTextureID)nullptr;
}




ImTextureID TextureLoader::LoadTexture(
    const wchar_t* filePath,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    ExampleDescriptorHeapAllocator& alloc)
{
    HRESULT hr;

    // Init COM
    hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
        return (ImTextureID)nullptr;

    IWICImagingFactory* factory = nullptr;
    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );
    if (FAILED(hr))
        return (ImTextureID)nullptr;

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromFilename(
        filePath,
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder
    );
    if (FAILED(hr))
    {
        factory->Release();
        return (ImTextureID)nullptr;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    decoder->GetFrame(0, &frame);

    UINT width = 0, height = 0;
    frame->GetSize(&width, &height);

    // Convert to RGBA8
    IWICFormatConverter* converter = nullptr;
    factory->CreateFormatConverter(&converter);

    converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom
    );

    UINT stride = width * 4;
    UINT size = stride * height;
    BYTE* pixels = new BYTE[size];
    converter->CopyPixels(nullptr, stride, size, pixels);

    // Create GPU texture
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ID3D12Resource* texture = nullptr;

    auto heapDefault = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    device->CreateCommittedResource(
        &heapDefault,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture)
    );

    if (!texture)
    {
        delete[] pixels;
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return (ImTextureID)nullptr;
    }

    // Upload heap
    UINT64 uploadSize = GetRequiredIntermediateSize(texture, 0, 1);

    ID3D12Resource* uploadBuffer = nullptr;
    auto heapUpload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

    device->CreateCommittedResource(
        &heapUpload,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)
    );

    if (!uploadBuffer)
    {
        texture->Release();
        delete[] pixels;
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return (ImTextureID)nullptr;
    }

    // Copy pixel data → GPU
    D3D12_SUBRESOURCE_DATA subData = {};
    subData.pData = pixels;
    subData.RowPitch = stride;
    subData.SlicePitch = size;

    UpdateSubresources(cmdList, texture, uploadBuffer, 0, 0, 1, &subData);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        texture,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    cmdList->ResourceBarrier(1, &barrier);

    // Create SRV
    D3D12_CPU_DESCRIPTOR_HANDLE srvCPU;
    D3D12_GPU_DESCRIPTOR_HANDLE srvGPU;
    alloc.Alloc(&srvCPU, &srvGPU);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    device->CreateShaderResourceView(texture, &srvDesc, srvCPU);

    // Cleanup WIC
    delete[] pixels;
    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();

    return (ImTextureID)srvGPU.ptr;
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


