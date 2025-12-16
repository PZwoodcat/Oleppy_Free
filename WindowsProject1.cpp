#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <d3dcompiler.h>
#include <iostream>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfobjects.h>
#include <mftransform.h>
#include <vector>
#include <chrono>
#include <thread>

#include <dxgi.h>

#pragma comment(lib, "d3d11.lib")    // for D3D11CreateDeviceAndSwapChain
#pragma comment(lib, "d3dcompiler.lib") // for D3DCompile
#pragma comment(lib, "dxgi.lib")     // for DXGI functions (optional, but common)
#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mf.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "Mfuuid.lib")

#pragma once
#include <memory>

#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

//#include <vpl/mfxvideo.h>
//#include <vpl/mfxdispatcher.h>
#define CHECK_MFX(st) do { if ((st) != MFX_ERR_NONE && (st) != MFX_ERR_MORE_DATA && (st) != MFX_ERR_MORE_SURFACE) { return E_FAIL; } } while(0)


using Microsoft::WRL::ComPtr;

HWND g_hWnd = nullptr;
ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGISwapChain> g_swapchain;
ComPtr<ID3D11RenderTargetView> g_rtv;
ComPtr<IDXGIOutputDuplication> g_duplication;
ComPtr<ID3D11InputLayout> g_inputLayout;
ComPtr<ID3D11Buffer> g_vertexBuffer;
ComPtr<ID3D11VertexShader> g_vertexShader;
ComPtr<ID3D11PixelShader> g_pixelShader;
ComPtr<ID3D11ShaderResourceView> g_srv;
ComPtr<ID3D11SamplerState> g_sampler;

enum class LogFileType {
    General,
    Encoder,
    Consumer,
    Muxer
};

static std::mutex g_logMutex;

static void LogAssertion(LogFileType type, const char* msg)
{
    const char* filename = nullptr;
    switch (type) {
    case LogFileType::General:  filename = "log_general.txt"; break;
    case LogFileType::Encoder:  filename = "log_encoder.txt"; break;
    case LogFileType::Consumer: filename = "log_consumer.txt"; break;
    case LogFileType::Muxer:    filename = "log_muxer.txt"; break;
    default: return;
    }

    std::lock_guard<std::mutex> lock(g_logMutex);

    FILE* f = nullptr;
    // ✅ Safe replacement for fopen
    if (fopen_s(&f, filename, "a") != 0 || !f)
        return;

    std::time_t now = std::time(nullptr);
    std::tm tmBuf;
    // ✅ Safe, thread-safe version of localtime
    localtime_s(&tmBuf, &now);

    char timebuf[64];
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tmBuf);

    std::fprintf(f, "[%s] Log assertion: %s\n", timebuf, msg);
    std::fclose(f);
}

struct Vertex {
    float x, y, z;
    float u, v;
};

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

bool InitWindow(HINSTANCE hInstance) {
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"CaptureWindow";
    RegisterClass(&wc);

    g_hWnd = CreateWindow(wc.lpszClassName, L"OBS Mini", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(g_hWnd, SW_SHOW);
    return true;
}

static constexpr UINT BUFFER_COUNT = 3;
ComPtr<ID3D11Texture2D> g_desktopCopies[BUFFER_COUNT];
ComPtr<ID3D11ShaderResourceView> g_srvs[BUFFER_COUNT];
UINT g_frameIndex = 0;
ComPtr<ID3D11Texture2D> g_cpuReadbacks[BUFFER_COUNT];
UINT g_cpuIndex = 0;

bool InitD3D() {
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 3;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = g_hWnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // REQUIRED

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &scd, &g_swapchain, &g_device, nullptr, &g_context);
    if (FAILED(hr)) return false;

    ComPtr<ID3D11Texture2D> backBuffer;
    g_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    g_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &g_rtv);
    return true;
}

bool InitDuplication() {
    ComPtr<IDXGIDevice> dxgiDevice;
    g_device.As(&dxgiDevice);

    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);

    ComPtr<IDXGIOutput> output;
    adapter->EnumOutputs(0, &output);

    ComPtr<IDXGIOutput1> output1;
    output.As(&output1);

    HRESULT hr = output1->DuplicateOutput(g_device.Get(), &g_duplication);
    if (FAILED(hr)) return false;

    // Create GPU-resident texture for desktop copy
    DXGI_OUTDUPL_DESC duplDesc;
    g_duplication->GetDesc(&duplDesc);
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = duplDesc.ModeDesc.Width;
    desc.Height = duplDesc.ModeDesc.Height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // match desktop
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    
    for (UINT i = 0; i < BUFFER_COUNT; ++i)
    {
        g_device->CreateTexture2D(&desc, nullptr, &g_desktopCopies[i]);
        g_device->CreateShaderResourceView(
            g_desktopCopies[i].Get(), &srvDesc, &g_srvs[i]);
    }

    D3D11_TEXTURE2D_DESC desc_staging = {};
    desc_staging.Width = duplDesc.ModeDesc.Width;
    desc_staging.Height = duplDesc.ModeDesc.Height;
    desc_staging.MipLevels = 1;
    desc_staging.ArraySize = 1;
    desc_staging.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // matches DXGI duplication
    desc_staging.SampleDesc.Count = 1;
    desc_staging.Usage = D3D11_USAGE_STAGING;
    desc_staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc_staging.BindFlags = 0;
    desc_staging.MiscFlags = 0;

    for (UINT i = 0; i < BUFFER_COUNT; ++i)
    {
        HRESULT hr = g_device->CreateTexture2D(&desc_staging, nullptr, &g_cpuReadbacks[i]);
        if (FAILED(hr))
            return false;
    }

    g_cpuIndex = 0;

    return true;
}

bool InitShaders() {
    const char* vsCode =
        "struct VS_IN { float3 pos : POSITION; float2 uv : TEXCOORD; };"
        "struct PS_IN { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };"
        "PS_IN main(VS_IN input) { PS_IN o; o.pos=float4(input.pos,1); o.uv=input.uv; return o; }";

    const char* psCode =
        "Texture2D tex : register(t0); SamplerState samp : register(s0);"
        "float4 main(float4 pos:SV_POSITION,float2 uv:TEXCOORD):SV_Target{"
        "float4 color=tex.Sample(samp,uv);"
        "color.rgb=lerp(color.rgb,float3(1,0,0),0.2);" // red overlay
        "return color;}";

    ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
    if (FAILED(D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errBlob))) {
        if (errBlob) std::cerr << (char*)errBlob->GetBufferPointer() << "\n";
        return false;
    }
    if (FAILED(D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errBlob))) {
        if (errBlob) std::cerr << (char*)errBlob->GetBufferPointer() << "\n";
        return false;
    }

    g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_vertexShader);
    g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_pixelShader);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}
    };
    g_device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_inputLayout);

    // Fullscreen quad
    Vertex quad[6] = {
        {-1,-1,0,0,1}, {-1,1,0,0,0}, {1,1,0,1,0},
        {-1,-1,0,0,1}, {1,1,0,1,0}, {1,-1,0,1,1}
    };
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(quad);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = quad;
    g_device->CreateBuffer(&bd, &initData, &g_vertexBuffer);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    g_device->CreateSamplerState(&sampDesc, &g_sampler);

    return true;
}

void RenderFrame() {
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ComPtr<IDXGIResource> desktopResource;

    if (SUCCEEDED(g_duplication->AcquireNextFrame(16, &frameInfo, &desktopResource))) {
        ComPtr<ID3D11Texture2D> frameTex;
        desktopResource.As(&frameTex);
        UINT idx = g_frameIndex % BUFFER_COUNT;

        g_context->CopyResource(
            g_desktopCopies[idx].Get(),
            frameTex.Get()
        );

        g_srv = g_srvs[idx];
        g_frameIndex++;
        g_duplication->ReleaseFrame();
    }

    g_context->OMSetRenderTargets(1, g_rtv.GetAddressOf(), nullptr);
    D3D11_VIEWPORT vp = {};
    vp.Width = 1280; vp.Height = 720; vp.MinDepth = 0; vp.MaxDepth = 1;
    g_context->RSSetViewports(1, &vp);

    FLOAT clearColor[4] = { 0.1f,0.1f,0.1f,1.0f };
    g_context->ClearRenderTargetView(g_rtv.Get(), clearColor);

    g_context->IASetInputLayout(g_inputLayout.Get());
    UINT stride = sizeof(Vertex), offset = 0;
    g_context->IASetVertexBuffers(0, 1, g_vertexBuffer.GetAddressOf(), &stride, &offset);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    g_context->VSSetShader(g_vertexShader.Get(), nullptr, 0);
    g_context->PSSetShader(g_pixelShader.Get(), nullptr, 0);
    g_context->PSSetShaderResources(0, 1, g_srv.GetAddressOf());
    g_context->PSSetSamplers(0, 1, g_sampler.GetAddressOf());

    g_context->Draw(6, 0);
    g_swapchain->Present(1, 0);
}

#define HR(x) { HRESULT hr__ = (x); if (FAILED(hr__)) { std::cerr << "Error: " << std::hex << hr__ << std::endl; return hr__; } }

static const LONGLONG HNS_PER_SEC = 10000000LL;

// width, height, fps are your frame info
HRESULT EncodeCpuFramesToMp4(
    const uint8_t* const* frameData, // array of frame pointers, each BGRA
    UINT32 frameCount,
    UINT32 width,
    UINT32 height,
    UINT32 fps,
    LPCWSTR filename)
{
    HR(MFStartup(MF_VERSION));

    ComPtr<IMFSinkWriter> writer;
    HR(MFCreateSinkWriterFromURL(filename, nullptr, nullptr, &writer));

    // ---- Configure output (H.264 MP4) ----
    ComPtr<IMFMediaType> outType;
    HR(MFCreateMediaType(&outType));
    HR(outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    HR(outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
    HR(outType->SetUINT32(MF_MT_AVG_BITRATE, 8000000)); // ~8 Mbps
    HR(outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    HR(MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, width, height));
    HR(MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, fps, 1));
    HR(MFSetAttributeRatio(outType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));

    DWORD streamIndex;
    HR(writer->AddStream(outType.Get(), &streamIndex));

    // ---- Configure input (software encoder can take RGB32) ----
    ComPtr<IMFMediaType> inType;
    HR(MFCreateMediaType(&inType));
    HR(inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    HR(inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)); // RGB32 (BGRA)
    HR(inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    HR(MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, width, height));
    HR(MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, fps, 1));
    HR(MFSetAttributeRatio(inType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));

    HR(writer->SetInputMediaType(streamIndex, inType.Get(), nullptr));
    HR(writer->BeginWriting());

    // Frame timing
    LONGLONG frameDuration = HNS_PER_SEC / fps;
    LONGLONG timestamp = 0;

    const UINT32 bytesPerPixel = 4;
    const UINT32 frameSize = width * height * bytesPerPixel;

    for (UINT32 i = 0; i < frameCount; ++i)
    {
        const uint8_t* src = frameData[i];

        ComPtr<IMFMediaBuffer> buffer;
        HR(MFCreateMemoryBuffer(frameSize, &buffer));

        BYTE* dst = nullptr;
        DWORD maxLen = 0, curLen = 0;
        HR(buffer->Lock(&dst, &maxLen, &curLen));
        memcpy(dst, src, frameSize);
        HR(buffer->Unlock());
        HR(buffer->SetCurrentLength(frameSize));

        ComPtr<IMFSample> sample;
        HR(MFCreateSample(&sample));
        HR(sample->AddBuffer(buffer.Get()));
        HR(sample->SetSampleTime(timestamp));
        HR(sample->SetSampleDuration(frameDuration));

        HR(writer->WriteSample(streamIndex, sample.Get()));

        timestamp += frameDuration;
    }

    HR(writer->Finalize());
    MFShutdown();

    return S_OK;
}

bool CaptureNextDXGIFrameToCpu(uint8_t* dstBuffer, UINT width, UINT height)
{
    DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
    ComPtr<IDXGIResource> desktopResource;

    HRESULT hr = g_duplication->AcquireNextFrame(16, &frameInfo, &desktopResource);
    if (FAILED(hr))
        return false;

    ComPtr<ID3D11Texture2D> frameTex;
    desktopResource.As(&frameTex);

    UINT idx = g_cpuIndex % BUFFER_COUNT;
    g_context->CopyResource(g_cpuReadbacks[idx].Get(), frameTex.Get());

    UINT mapIdx = (g_cpuIndex + BUFFER_COUNT - 1) % BUFFER_COUNT;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(g_context->Map(g_cpuReadbacks[mapIdx].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped)))
    {
        const UINT bytesPerPixel = 4;
        uint8_t* dstRow = dstBuffer;
        uint8_t* srcRow = (uint8_t*)mapped.pData + (height - 1) * mapped.RowPitch; // start from last row

        for (UINT y = 0; y < height; ++y)
        {
            memcpy(dstRow, srcRow, width * bytesPerPixel);
            dstRow += width * bytesPerPixel;
            srcRow -= mapped.RowPitch; // move up one row
        }

        g_context->Unmap(g_cpuReadbacks[mapIdx].Get(), 0);
    }

    g_cpuIndex++;
    g_duplication->ReleaseFrame();
    return SUCCEEDED(hr);
}


class CpuMp4Encoder
{
public:
    HRESULT Begin(UINT width, UINT height, UINT fps, const wchar_t* filename);
    HRESULT WriteFrame(const uint8_t* rgbaData, LONGLONG timestampHns);
    HRESULT End();

private:
    ComPtr<IMFSinkWriter> m_writer;
    DWORD m_streamIndex = 0;
    UINT m_width = 0, m_height = 0;
};
HRESULT CpuMp4Encoder::Begin(UINT width, UINT height, UINT fps, const wchar_t* filename)
{
    m_width = width;
    m_height = height;

    HR(MFStartup(MF_VERSION));

    // Create the sink writer
    HR(MFCreateSinkWriterFromURL(filename, nullptr, nullptr, &m_writer));

    // Output (encoded) media type
    ComPtr<IMFMediaType> outType;
    HR(MFCreateMediaType(&outType));
    HR(outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    HR(outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
    HR(outType->SetUINT32(MF_MT_AVG_BITRATE, 8000000)); // 8 Mbps
    HR(outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    HR(MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, width, height));
    HR(MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, fps, 1));
    HR(MFSetAttributeRatio(outType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    HR(m_writer->AddStream(outType.Get(), &m_streamIndex));

    // Input (uncompressed) media type
    ComPtr<IMFMediaType> inType;
    HR(MFCreateMediaType(&inType));
    HR(inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    HR(inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32));
    HR(inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    HR(MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, width, height));
    HR(MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, fps, 1));
    HR(MFSetAttributeRatio(inType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    HR(m_writer->SetInputMediaType(m_streamIndex, inType.Get(), nullptr));

    HR(m_writer->BeginWriting());
    return S_OK;
}
HRESULT CpuMp4Encoder::WriteFrame(const uint8_t* bgraData, LONGLONG timestampHns)
{
    const UINT32 frameSize = m_width * m_height * 4; // BGRA

    ComPtr<IMFMediaBuffer> buffer;
    // Create MF memory buffer of correct size
    HR(MFCreateMemoryBuffer(frameSize, &buffer));

    BYTE* dst = nullptr;
    DWORD maxLen = 0, curLen = 0;

    // Lock buffer once
    HR(buffer->Lock(&dst, &maxLen, &curLen));
    // Direct copy from DXGI frame (no conversion)
    memcpy(dst, bgraData, frameSize);
    HR(buffer->Unlock());
    HR(buffer->SetCurrentLength(frameSize));

    // Create sample and attach buffer
    ComPtr<IMFSample> sample;
    HR(MFCreateSample(&sample));
    HR(sample->AddBuffer(buffer.Get()));
    HR(sample->SetSampleTime(timestampHns));
    HR(sample->SetSampleDuration(0));

    HR(m_writer->WriteSample(m_streamIndex, sample.Get()));
    return S_OK;
}
HRESULT CpuMp4Encoder::End()
{
    HR(m_writer->Finalize());
    MFShutdown();
    return S_OK;
}

HRESULT EncodeCpuFramesWrapper()
{
    CpuMp4Encoder encoder;
    encoder.Begin(1920, 1080, 80, L"hour_capture.mp4");

    std::vector<uint8_t> frame(1920 * 1080 * 4);

    auto start = std::chrono::steady_clock::now();
    auto end = start + std::chrono::minutes(1);

    while (std::chrono::steady_clock::now() < end)
    {
        auto now = std::chrono::steady_clock::now();
        LONGLONG ts =
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count() / 100;

        if (CaptureNextDXGIFrameToCpu(frame.data(), 1920, 1080))
            encoder.WriteFrame(frame.data(), ts);
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    encoder.End();

    return S_OK;
}

//bool CaptureNextDXGIFrameToGpu(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D** outTex)
//{
//    DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
//    ComPtr<IDXGIResource> desktopResource;
//
//    HRESULT hr = g_duplication->AcquireNextFrame(16, &frameInfo, &desktopResource);
//    if (FAILED(hr))
//        return false;
//
//    ComPtr<ID3D11Texture2D> frameTex;
//    desktopResource.As(&frameTex);
//
//    // Get the description of the acquired desktop texture
//    D3D11_TEXTURE2D_DESC srcDesc = {};
//    frameTex->GetDesc(&srcDesc);
//
//    // Create a GPU texture that can be shared with Media Foundation
//    D3D11_TEXTURE2D_DESC desc = srcDesc;
//    desc.Usage = D3D11_USAGE_DEFAULT;                 // GPU-accessible
//    desc.CPUAccessFlags = 0;                          // no CPU access
//    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;      // just basic GPU binding
//    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;      // required for MFCreateDXGISurfaceBuffer
//
//    ComPtr<ID3D11Texture2D> sharedTex;
//    hr = device->CreateTexture2D(&desc, nullptr, &sharedTex);
//    if (FAILED(hr))
//    {
//        LogAssertion(LogFileType::Encoder, "Failed to create tex");
//        g_duplication->ReleaseFrame();
//        return false;
//    }
//
//    // Copy from desktop duplication surface into our shared texture
//    context->CopyResource(sharedTex.Get(), frameTex.Get());
//
//    // Release the desktop duplication frame
//    g_duplication->ReleaseFrame();
//
//    // Return the GPU texture pointer
//    *outTex = sharedTex.Detach();
//    return true;
//}
//
//mfxSession g_session = nullptr;
//mfxVideoParam g_vppParams = {};
//bool g_vppInitialized = false;
//
//HRESULT InitVPP(UINT width, UINT height)
//{
//    mfxLoader loader = MFXLoad();
//    if (!loader) return E_FAIL;
//
//    mfxConfig cfg = MFXCreateConfig(loader);
//    mfxVariant implType = {};
//    implType.Type = MFX_VARIANT_TYPE_U32;
//    implType.Data.U32 = MFX_IMPL_TYPE_HARDWARE;
//    MFXSetConfigFilterProperty(cfg, (mfxU8*)"mfxImplDescription.Impl", implType);
//
//    if (MFXCreateSession(loader, 0, &g_session) != MFX_ERR_NONE) {
//        LogAssertion(LogFileType::Encoder, "Failed to create session");
//        return E_FAIL;
//    }
//
//    PrepareFrameInfo(&g_vppParams.vpp.In, MFX_FOURCC_BGRA, width, height);
//    PrepareFrameInfo(&g_vppParams.vpp.Out, MFX_FOURCC_NV12, width, height);
//    g_vppParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;
//
//    if (MFXVideoVPP_Init(g_session, &g_vppParams) != MFX_ERR_NONE) {
//        LogAssertion(LogFileType::Encoder, "Failed to create encoder");
//        return E_FAIL;
//    }
//
//    g_vppInitialized = true;
//    return S_OK;
//}
//
//HRESULT ConvertBGRAtoNV12(ID3D11Texture2D* srcBGRA, ID3D11Texture2D** outNV12)
//{
//    if (!g_vppInitialized)
//        return E_FAIL;
//
//    mfxFrameSurface1* inSurface = nullptr;
//    mfxFrameSurface1* outSurface = nullptr;
//
//    MFXMemory_GetSurfaceForVPPIn(g_session, &inSurface);
//    MFXMemory_GetSurfaceForVPPOut(g_session, &outSurface);
//
//    // Attach D3D11 textures to surfaces
//    inSurface->Data.MemId = srcBGRA;
//    outSurface->Data.MemId = *outNV12;
//
//    mfxSyncPoint syncp = {};
//    if (MFXVideoVPP_RunFrameVPPAsync(g_session, inSurface, outSurface, nullptr, &syncp) != MFX_ERR_NONE) {
//        LogAssertion(LogFileType::Encoder, "Failed to create encoder");
//        return E_FAIL;
//    }
//
//    MFXVideoCORE_SyncOperation(g_session, syncp, 1000);
//
//    // Now outSurface contains NV12 GPU texture
//    return S_OK;
//}
//
//class GpuMp4Encoder
//{
//public:
//    HRESULT Begin(ID3D11Device* device, ID3D11DeviceContext* context, UINT width, UINT height, UINT fps, const wchar_t* filename);
//    HRESULT WriteFrame(ID3D11Texture2D* texture);
//    HRESULT End();
//
//private:
//    ComPtr<IMFSinkWriter> m_writer;
//    ComPtr<IMFDXGIDeviceManager> m_dxgiManager;
//    DWORD m_streamIndex = 0;
//    LONGLONG m_rtStart = 0;
//    LONGLONG m_frameDuration = 0;
//    UINT m_width = 0, m_height = 0;
//    ComPtr<ID3D11Device> m_device;
//    ComPtr<ID3D11DeviceContext> m_context;
//};
//
//HRESULT GpuMp4Encoder::Begin(ID3D11Device* device, ID3D11DeviceContext* context, UINT width, UINT height, UINT fps, const wchar_t* filename)
//{
//    m_device = device;
//    m_context = context;
//    m_width = width;
//    m_height = height;
//    m_frameDuration = HNS_PER_SEC / fps;
//    m_rtStart = 0;
//
//    HR(MFStartup(MF_VERSION));
//
//    // 1) Create DXGI Device Manager and link to D3D device
//    UINT resetToken = 0;
//    HR(MFCreateDXGIDeviceManager(&resetToken, &m_dxgiManager));
//    HR(m_dxgiManager->ResetDevice(m_device.Get(), resetToken));
//
//    // 2) Create attributes for sink writer to use the DXGI manager
//    ComPtr<IMFAttributes> attr;
//    HR(MFCreateAttributes(&attr, 1));
//    HR(attr->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, m_dxgiManager.Get()));
//
//    // 3) Create sink writer
//    HR(MFCreateSinkWriterFromURL(filename, nullptr, attr.Get(), &m_writer));
//
//    // 4) Output (encoded) type: H.264
//    ComPtr<IMFMediaType> outType;
//    HR(MFCreateMediaType(&outType));
//    HR(outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
//    HR(outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
//    HR(outType->SetUINT32(MF_MT_AVG_BITRATE, 8000000)); // 8 Mbps
//    HR(outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
//    HR(MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, width, height));
//    HR(MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, fps, 1));
//    HR(MFSetAttributeRatio(outType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
//
//    HR(m_writer->AddStream(outType.Get(), &m_streamIndex));
//
//    // 5) Input (uncompressed) type: NV12 (typical for GPU path)
//    ComPtr<IMFMediaType> inType;
//    HR(MFCreateMediaType(&inType));
//    HR(inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
//    HR(inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12)); // GPU encoder prefers NV12
//    HR(inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
//    HR(MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, width, height));
//    HR(MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, fps, 1));
//    HR(MFSetAttributeRatio(inType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
//
//    HR(m_writer->SetInputMediaType(m_streamIndex, inType.Get(), nullptr));
//
//    HR(m_writer->BeginWriting());
//    HR(InitVPP(width, height));
//    return S_OK;
//}
//
//HRESULT GpuMp4Encoder::WriteFrame(ID3D11Texture2D* texture)
//{
//    if (!texture) return E_POINTER;
//
//    // 1) Convert BGRA -> NV12 on the same D3D device
//    Microsoft::WRL::ComPtr<ID3D11Texture2D> nv12Tex;
//    HRESULT hr = ConvertBGRAtoNV12(texture, &nv12Tex);
//    if (FAILED(hr)) return hr;
//
//    // 2) Ensure GPU finished any work writing into nv12Tex
//    m_context->Flush();
//
//    // 3) Create MF DXGI buffer from nv12Tex (which must be created on the device registered with m_dxgiManager)
//    ComPtr<IMFMediaBuffer> buffer;
//    hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), nv12Tex.Get(), 0, FALSE, &buffer);
//    if (FAILED(hr)) return hr;
//
//    ComPtr<IMFSample> sample;
//    hr = MFCreateSample(&sample);
//    if (FAILED(hr)) return hr;
//    hr = sample->AddBuffer(buffer.Get());
//    if (FAILED(hr)) return hr;
//    hr = sample->SetSampleTime(m_rtStart);
//    if (FAILED(hr)) return hr;
//    hr = sample->SetSampleDuration(m_frameDuration);
//    if (FAILED(hr)) return hr;
//
//    hr = m_writer->WriteSample(m_streamIndex, sample.Get());
//    if (FAILED(hr)) return hr;
//
//    m_rtStart += m_frameDuration;
//    return S_OK;
//}
//
//HRESULT GpuMp4Encoder::End()
//{
//    HR(m_writer->Finalize());
//    MFShutdown();
//    return S_OK;
//}
//
//HRESULT EncodeD3D11FramesWrapper()
//{
//    GpuMp4Encoder encoder;
//    HR(encoder.Begin(g_device.Get(), g_context.Get(), 1920, 1080, 30, L"hour_capture_gpu.mp4"));
//
//    auto start = std::chrono::steady_clock::now();
//    auto end = start + std::chrono::minutes(1);
//
//    ComPtr<ID3D11Texture2D> frameTex;
//
//    while (std::chrono::steady_clock::now() < end)
//    {
//        if (CaptureNextDXGIFrameToGpu(g_device.Get(), g_context.Get(), &frameTex)) // you must implement this
//        {
//            encoder.WriteFrame(frameTex.Get());
//        }
//        else
//        {
//            std::this_thread::sleep_for(std::chrono::milliseconds(5));
//        }
//    }
//
//    encoder.End();
//    return S_OK;
//}
//
//// Nvidia D3D11 path
//// Replace previous EncFrame that used CUdeviceptr
//struct EncFrame {
//    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex; // the captured GPU texture (held by queue until processed)
//    int64_t pts;
//};
//
//struct EncodedPacket {
//    std::vector<uint8_t> data;
//    int64_t pts; // optional timestamp
//};
//
//class NvGpuEncoder
//{
//public:
//    HRESULT Begin(
//        ID3D11Device* d3dDevice,
//        ID3D11DeviceContext* d3dContext,
//        UINT width,
//        UINT height,
//        UINT fps,
//        const wchar_t* filename);
//
//    HRESULT WriteFrame(ID3D11Texture2D* pD3DTexture);
//    HRESULT End();
//
//private:
//    void ConsumerLoop();
//
//    // Device and context
//    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
//    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
//
//    // NVENC handle (D3D11 flavor)
//    std::unique_ptr<NvEncoderD3D11> m_encoder;

//    // Output file (raw H.264)
//    FILE* m_outputFile = nullptr;
//
//    // Encoding parameters
//    UINT m_width = 0, m_height = 0;
//    LONGLONG m_rtStart = 0;
//    LONGLONG m_frameDuration = 0;
//
//    // Producer-consumer queue (holds D3D textures)
//    std::queue<EncFrame> m_queue;
//    std::mutex m_mutex;
//    std::condition_variable m_cv;
//    std::atomic<bool> m_running = false;
//    std::thread m_consumerThread;
//
//    std::queue<EncodedPacket> m_encodedQueue;
//    std::mutex m_encodedMutex;
//    std::condition_variable m_encodedCV;
//    std::atomic<bool> m_muxRunning = false;
//    std::thread m_muxThread;
//
//    int64_t frameCounter = 0;
//
//    // FFmpeg handles
//    AVFormatContext* m_fmt = nullptr;
//    AVStream* m_stream = nullptr;
//
//    void MuxThread();
//};

//HRESULT NvGpuEncoder::Begin(
//    ID3D11Device* d3dDevice,
//    ID3D11DeviceContext* d3dContext,
//    UINT width,
//    UINT height,
//    UINT fps,
//    const wchar_t* filename)
//{
//    LogAssertion(LogFileType::Encoder, "Begin() called - initializing encoder");
//
//    if (!d3dDevice || !d3dContext) return E_POINTER;
//
//    m_device = d3dDevice;
//    m_context = d3dContext;
//    m_width = width;
//    m_height = height;
//    if (fps == 0) return E_INVALIDARG;
//    m_frameDuration = (LONGLONG)(10000000LL / fps); // HNS units
//    m_rtStart = 0;
//
//    // Create NVENC encoder for D3D11
//    try {
//        // Construct D3D11 encoder - format ARGB (BGRA) as in sample
//        m_encoder = std::make_unique<NvEncoderD3D11>(m_device.Get(), width, height, NV_ENC_BUFFER_FORMAT_ARGB);
//
//        // Initialize encoder params (use defaults then tweak)
//        NV_ENC_INITIALIZE_PARAMS initParams = { NV_ENC_INITIALIZE_PARAMS_VER };
//        NV_ENC_CONFIG encodeConfig = { NV_ENC_CONFIG_VER };
//        initParams.encodeConfig = &encodeConfig;
//
//        m_encoder->CreateDefaultEncoderParams(
//            &initParams,
//            NV_ENC_CODEC_H264_GUID,
//            NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID);
//
//        initParams.frameRateNum = fps;
//        initParams.frameRateDen = 1;
//        initParams.encodeWidth = width;
//        initParams.encodeHeight = height;
//
//        HRESULT hr = m_encoder->CreateEncoder(&initParams);
//        if (FAILED(hr)) {
//            LogAssertion(LogFileType::Encoder, "Failed to create encoder");
//            return hr;
//        }
//    }
//    catch (NVENCODEAPI_ERROR& e) {
//        // NvEncoderD3D11 throws via NVENC_THROW_ERROR in SDK wrappers; translate to HRESULT
//        LogAssertion(LogFileType::Encoder, "Failed to create encoder");
//        return E_FAIL;
//    }
//    catch (...) {
//        LogAssertion(LogFileType::Encoder, "Failed to create encoder");
//        return E_FAIL;
//    }
//
//    // Open output file (raw H264)
//    if (_wfopen_s(&m_outputFile, filename, L"wb") != 0) {
//        m_outputFile = nullptr; // failed to open
//    }
//    if (!m_outputFile) return E_FAIL;
//
//    // ---------- FFmpeg init ----------
//    avformat_network_init();
//
//    int ret = avformat_alloc_output_context2(&m_fmt, nullptr, "flv", nullptr);
//    if (ret < 0 || !m_fmt) {
//        return E_FAIL;
//    }
//
//    m_stream = avformat_new_stream(m_fmt, nullptr);
//    if (!m_stream) {
//        avformat_free_context(m_fmt);
//        m_fmt = nullptr;
//        return E_FAIL;
//    }
//
//    // Store fps as timebase {1, fps}
//    m_stream->time_base = AVRational{ 1, static_cast<int>(fps) };
//
//    m_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
//    m_stream->codecpar->codec_id = AV_CODEC_ID_H264;
//    m_stream->codecpar->width = m_width;
//    m_stream->codecpar->height = m_height;
//    m_stream->codecpar->format = AV_PIX_FMT_NONE; // encoding packets only
//
//    if (!(m_fmt->oformat->flags & AVFMT_NOFILE)) {
//        // Convert filename to UTF-8 for avio_open if needed; for brevity, use a plain narrow name fallback
//        // NOTE: in production convert wide filename -> UTF-8 properly
//        ret = avio_open(&m_fmt->pb, "output.flv", AVIO_FLAG_WRITE);
//        if (ret < 0) {
//            avformat_free_context(m_fmt);
//            m_fmt = nullptr;
//            return E_FAIL;
//        }
//    }
//
//    ret = avformat_write_header(m_fmt, nullptr);
//    if (ret < 0) {
//        if (m_fmt && m_fmt->pb) avio_closep(&m_fmt->pb);
//        avformat_free_context(m_fmt);
//        m_fmt = nullptr;
//        return E_FAIL;
//    }
//    // ---------- end FFmpeg init ----------
//
//    // Start consumer + mux threads
//    m_running = true;
//    m_consumerThread = std::thread(&NvGpuEncoder::ConsumerLoop, this);
//
//    m_muxRunning = true;
//    m_muxThread = std::thread(&NvGpuEncoder::MuxThread, this);
//
//    LogAssertion(LogFileType::Encoder, "Begin successful");
//
//    return S_OK;
//}
//HRESULT NvGpuEncoder::WriteFrame(ID3D11Texture2D* pD3DTexture)
//{
//    if (!pD3DTexture || !m_encoder) return E_POINTER;
//
//    // Hold a reference to the captured texture and push to queue (consumer will CopyResource into encoder input)
//    EncFrame frame;
//    frame.tex = pD3DTexture; // ComPtr assignment increments refcount
//    frame.pts = frameCounter++;
//
//    {
//        std::lock_guard<std::mutex> lock(m_mutex);
//        m_queue.push(std::move(frame));
//    }
//    m_cv.notify_one();
//
//    return S_OK;
//}
//void NvGpuEncoder::ConsumerLoop()
//{
//    while (m_running)
//    {
//        EncFrame frame;
//        {
//            std::unique_lock<std::mutex> lock(m_mutex);
//            m_cv.wait(lock, [&] { return !m_queue.empty() || !m_running; });
//            if (!m_running && m_queue.empty())
//                break;
//            frame = std::move(m_queue.front());
//            m_queue.pop();
//        }
//
//        // Get encoder input texture pointer from encoder
//        const NvEncInputFrame* inputFrame = m_encoder->GetNextInputFrame();
//
//        // The encoder's inputPtr for D3D11 is an ID3D11Texture2D* (per NVENC sample).
//        ID3D11Texture2D* pEncTex = reinterpret_cast<ID3D11Texture2D*>(inputFrame->inputPtr);
//        if (!pEncTex) {
//            // skip / or log
//            continue;
//        }
//
//        // Copy captured texture into encoder input texture on GPU (fast GPU->GPU copy)
//        // Make sure contexts are the same device; we used same device in Begin()
//        m_context->CopyResource(pEncTex, frame.tex.Get());
//
//        // Optionally flush to ensure copy completes before encoding. Encoder may handle synchronization,
//        // but flush reduces latency when cross-thread handoff happens.
//        m_context->Flush();
//
//        // Encode
//        std::vector<std::vector<uint8_t>> packets;
//        try {
//            m_encoder->EncodeFrame(packets);
//        }
//        catch (...) {
//            // handle errors (skip frame or stop)
//            continue;
//        }
//
//        // Push encoded packets into encoded queue / write file
//        for (auto& pkt : packets)
//        {
//            EncodedPacket outPkt;
//            outPkt.data = std::move(pkt);
//            outPkt.pts = frame.pts;
//
//            {
//                std::lock_guard<std::mutex> lock(m_encodedMutex);
//                m_encodedQueue.push(std::move(outPkt));
//            }
//            m_encodedCV.notify_one();
//
//            // Also write raw H264 to file if desired (you had this behavior before)
//            fwrite(outPkt.data.data(), 1, outPkt.data.size(), m_outputFile);
//        }
//    }
//}
//void NvGpuEncoder::MuxThread()
//{
//    LogAssertion(LogFileType::Muxer, "Mux thread started");
//
//    while (m_muxRunning)
//    {
//        EncodedPacket pkt;
//        {
//            LogAssertion(LogFileType::Muxer, "Waiting for encoded packet...");
//
//            std::unique_lock<std::mutex> lock(m_encodedMutex);
//            m_encodedCV.wait(lock, [&] { return !m_encodedQueue.empty() || !m_muxRunning; });
//            if (!m_muxRunning && m_encodedQueue.empty()) break;
//
//            pkt = std::move(m_encodedQueue.front());
//            m_encodedQueue.pop();
//        }
//
//        // Wrap encoded H.264 data into an AVPacket
//        AVPacket* avpkt = av_packet_alloc();
//        if (!avpkt) {
//            LogAssertion(LogFileType::Muxer, "Failed to allocate AVPacket");
//            return;
//        }
//
//        avpkt->data = pkt.data.data();
//        avpkt->size = static_cast<int>(pkt.data.size());
//        avpkt->pts = pkt.pts;
//        avpkt->dts = pkt.pts;
//        avpkt->stream_index = m_stream->index;
//
//        // Log packet info before writing
//        char buf[128];
//        std::snprintf(buf, sizeof(buf), "Writing packet with PTS %llu, size %zu", pkt.pts, pkt.data.size());
//        LogAssertion(LogFileType::Muxer, buf);
//
//        // Write packet to container
//        int ret = av_interleaved_write_frame(m_fmt, avpkt);
//        if (ret < 0) {
//            LogAssertion(LogFileType::Muxer, "Error writing packet to output");
//        }
//
//        // Free the packet after writing
//        av_packet_free(&avpkt);
//    }
//
//    LogAssertion(LogFileType::Muxer, "Mux thread stopped");
//}
//HRESULT NvGpuEncoder::End()
//{
//    if (!m_encoder) return S_FALSE;

//    // Signal consumer to stop
//    {
//        std::lock_guard<std::mutex> lock(m_mutex);
//        m_running = false;
//    }
//    m_cv.notify_one();
//
//    if (m_consumerThread.joinable())
//        m_consumerThread.join();
//
//    // Stop mux thread
//    m_muxRunning = false;
//    m_encodedCV.notify_one();
//    if (m_muxThread.joinable()) m_muxThread.join();
//
//    // Flush encoder (get remaining packets)
//    std::vector<std::vector<uint8_t>> packets;
//    m_encoder->EndEncode(packets);
//
//    for (auto& pkt : packets)
//        fwrite(pkt.data(), 1, pkt.size(), m_outputFile);
//
//    // Write trailer + close ffmpeg IO
//    if (m_fmt) {
//        av_write_trailer(m_fmt);
//        if (m_fmt->pb) avio_closep(&m_fmt->pb);
//        avformat_free_context(m_fmt);
//        m_fmt = nullptr;
//        m_stream = nullptr;
//    }
//
//    if (m_outputFile) {
//        fclose(m_outputFile);
//        m_outputFile = nullptr;
//    }
//
//    // Destroy encoder (D3D11 encoder cleanup)
//    try {
//        m_encoder->DestroyEncoder();
//        m_encoder.reset();
//    }
//    catch (...) {
//        // swallow
//    }
//
//    // Release device context / device refs (ComPtr will auto-release)
//    m_context.Reset();
//    m_device.Reset();
//
//    return S_OK;
//}
//
//ComPtr<ID3D11Device> nv_device;
//ComPtr<ID3D11DeviceContext> nv_context;
//ComPtr<IDXGIAdapter> nv_adapter;
//bool CreateNvidiaDevice()
//{
//    ComPtr<IDXGIFactory1> factory;
//    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
//    if (FAILED(hr)) {
//        std::cerr << "Failed to create DXGIFactory: 0x" << std::hex << hr << std::endl;
//        return false;
//    }
//
//    UINT adapterIndex = 0;
//    ComPtr<IDXGIAdapter> adapter;
//    DXGI_ADAPTER_DESC desc;
//
//    while (factory->EnumAdapters(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND)
//    {
//        adapter->GetDesc(&desc);
//        std::wcout << L"Found adapter: " << desc.Description << std::endl;
//
//        // Check if this is the NVIDIA GPU
//        if (wcsstr(desc.Description, L"NVIDIA") != nullptr)
//        {
//            std::wcout << L"Using NVIDIA adapter: " << desc.Description << std::endl;
//            nv_adapter = adapter;
//            break;
//        }
//
//        adapterIndex++;
//        adapter.Reset();
//    }
//
//    if (!nv_adapter) {
//        std::cerr << "No NVIDIA adapter found!" << std::endl;
//        return false;
//    }
//
//    UINT createFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
//#if defined(_DEBUG)
//    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
//#endif
//
//    D3D_FEATURE_LEVEL featureLevels[] = {
//        D3D_FEATURE_LEVEL_12_1,
//        D3D_FEATURE_LEVEL_12_0,
//        D3D_FEATURE_LEVEL_11_1,
//        D3D_FEATURE_LEVEL_11_0
//    };
//    D3D_FEATURE_LEVEL featureLevelOut;
//
//    hr = D3D11CreateDevice(
//        nv_adapter.Get(),
//        D3D_DRIVER_TYPE_UNKNOWN,  // Must be UNKNOWN when using an explicit adapter
//        nullptr,
//        createFlags,
//        featureLevels,
//        ARRAYSIZE(featureLevels),
//        D3D11_SDK_VERSION,
//        &nv_device,
//        &featureLevelOut,
//        &nv_context
//    );
//
//    if (FAILED(hr)) {
//        std::cerr << "Failed to create D3D11 device on NVIDIA GPU: 0x" << std::hex << hr << std::endl;
//        return false;
//    }
//
//    std::cout << "Successfully created D3D11 device on NVIDIA GPU." << std::endl;
//    return true;
//}
//// Capture frame from Intel GPU, copy to CPU, then upload to NVIDIA GPU.
//bool CaptureNextDXGIFrameToCpuAndNvidia(
//    uint8_t* dstBuffer,
//    ID3D11Device* nvidiaDevice,
//    ID3D11DeviceContext* nvidiaContext,
//    ID3D11Texture2D** outNvTexture,  // receives a D3D11 texture on NVIDIA GPU
//    UINT width,
//    UINT height)
//{
//    DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
//    ComPtr<IDXGIResource> desktopResource;
//
//    HRESULT hr = g_duplication->AcquireNextFrame(16, &frameInfo, &desktopResource);
//    if (FAILED(hr))
//        return false;
//
//    ComPtr<ID3D11Texture2D> intelFrameTex;
//    desktopResource.As(&intelFrameTex);
//
//    // Create a CPU-readable staging texture on the Intel device
//    D3D11_TEXTURE2D_DESC desc = {};
//    intelFrameTex->GetDesc(&desc);
//    desc.Usage = D3D11_USAGE_STAGING;
//    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
//    desc.BindFlags = 0;
//    desc.MiscFlags = 0;
//
//    ComPtr<ID3D11Texture2D> cpuTex;
//    hr = nv_device->CreateTexture2D(&desc, nullptr, &cpuTex);
//    if (FAILED(hr)) {
//        g_duplication->ReleaseFrame();
//        return false;
//    }
//
//    // Copy from Intel GPU frame to CPU staging texture
//    g_context->CopyResource(cpuTex.Get(), intelFrameTex.Get());
//
//    // Map staging texture to CPU memory
//    D3D11_MAPPED_SUBRESOURCE mapped = {};
//    hr = g_context->Map(cpuTex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
//    if (FAILED(hr)) {
//        g_duplication->ReleaseFrame();
//        return false;
//    }
//
//    const UINT bytesPerPixel = 4;
//    const UINT rowSize = width * bytesPerPixel;
//
//    // Copy pixels into dstBuffer
//    for (UINT y = 0; y < height; ++y) {
//        memcpy(dstBuffer + y * rowSize,
//            (uint8_t*)mapped.pData + y * mapped.RowPitch,
//            rowSize);
//    }
//
//    g_context->Unmap(cpuTex.Get(), 0);
//
//    LogAssertion(LogFileType::Encoder, "Moved tex to cpu");
//
//    // Now create an upload texture on the NVIDIA device
//    D3D11_TEXTURE2D_DESC nvDesc = {};
//    nvDesc.Width = width;
//    nvDesc.Height = height;
//    nvDesc.MipLevels = 1;
//    nvDesc.ArraySize = 1;
//    nvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
//    nvDesc.SampleDesc.Count = 1;
//    nvDesc.Usage = D3D11_USAGE_DEFAULT;
//    nvDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
//    nvDesc.CPUAccessFlags = 0;
//    nvDesc.MiscFlags = 0;
//
//    // Create the texture on NVIDIA GPU
//    hr = nvidiaDevice->CreateTexture2D(&nvDesc, nullptr, outNvTexture);
//    if (FAILED(hr)) {
//        g_duplication->ReleaseFrame();
//        LogAssertion(LogFileType::Encoder, "Failed to init gpu texture");
//        return false;
//    }
//
//    // Upload CPU buffer into NVIDIA GPU texture
//    nvidiaContext->UpdateSubresource(
//        *outNvTexture,
//        0,
//        nullptr,
//        dstBuffer,
//        width * bytesPerPixel,
//        0
//    );
//
//    LogAssertion(LogFileType::Encoder, "subresources probably updated, its doesn't return status??");
//
//    g_duplication->ReleaseFrame();
//    return true;
//}
//
//HRESULT EncodeD3D11FramesWrapper_NVENC()
//{
//    if (!CreateNvidiaDevice()) {
//        LogAssertion(LogFileType::Encoder, "Failed to create nvidia device");
//        return E_FAIL;
//    }
//
//    NvGpuEncoder encoder;
//    HRESULT hr = encoder.Begin(nv_device.Get(), nv_context.Get(), 1920, 1080, 30, L"hour_capture_nvenc.h264");
//    if (FAILED(hr)) return hr;
//
//    auto start = std::chrono::steady_clock::now();
//    auto end = start + std::chrono::minutes(1);
//
//    Microsoft::WRL::ComPtr<ID3D11Texture2D> frameTex;
//
//    std::vector<uint8_t> frame(1920 * 1080 * 4);
//
//    while (std::chrono::steady_clock::now() < end)
//    {
//        // CaptureNextDXGIFrameToGpu must produce an ID3D11Texture2D* that lives on the same device
//        if (CaptureNextDXGIFrameToCpuAndNvidia(frame.data(), nv_device.Get(), nv_context.Get(), &frameTex, 1920, 1080))
//        {
//            // Pass the captured GPU texture to encoder; encoder will CopyResource into its input texture
//            encoder.WriteFrame(frameTex.Get());
//        }
//        else
//        {
//            std::this_thread::sleep_for(std::chrono::milliseconds(5));
//        }
//    }
//
//    encoder.End();
//    return S_OK;
//}


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) return -1;
    if (!InitWindow(hInstance)) return -1;
    if (!InitD3D()) return -1;
    if (!InitDuplication()) return -1;
    if (!InitShaders()) return -1;
	if (FAILED(EncodeCpuFramesWrapper())) return -1;
	//if (FAILED(EncodeD3D11FramesWrapper())) return -1;  //to-do implement triple buffer fallback for high core case
    //if (FAILED(EncodeD3D11FramesWrapper_NVENC())) return -1;

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            RenderFrame();
        }
    }
    CoUninitialize();
    return 0;
}

