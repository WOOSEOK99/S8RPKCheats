#include "TextureLoader.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include "helper.h"

#pragma comment(lib, "Windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace DX11Base {

    bool LoadTextureFromFile(ID3D11Device* d3d_device, const wchar_t* filename, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height) {
        static bool com_init = false;
        if (!com_init) {
            CoInitializeEx(NULL, COINIT_MULTITHREADED);
            com_init = true;
        }

        ComPtr<IWICImagingFactory> wic_factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_factory));
        if (FAILED(hr)) return false;

        ComPtr<IWICBitmapDecoder> decoder;
        hr = wic_factory->CreateDecoderFromFilename(filename, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr)) return false;

        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) return false;

        UINT width, height;
        hr = frame->GetSize(&width, &height);
        if (FAILED(hr)) return false;

        ComPtr<IWICFormatConverter> converter;
        hr = wic_factory->CreateFormatConverter(&converter);
        if (FAILED(hr)) return false;

        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) return false;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        ID3D11Texture2D* texture = nullptr;
        D3D11_SUBRESOURCE_DATA subresource = {};
        
        std::vector<unsigned char> pixels(width * height * 4);
        hr = converter->CopyPixels(nullptr, width * 4, (UINT)pixels.size(), pixels.data());
        if (FAILED(hr)) return false;

        subresource.pSysMem = pixels.data();
        subresource.SysMemPitch = width * 4;
        subresource.SysMemSlicePitch = 0;

        hr = d3d_device->CreateTexture2D(&desc, &subresource, &texture);
        if (FAILED(hr)) return false;

        hr = d3d_device->CreateShaderResourceView(texture, nullptr, out_srv);
        texture->Release();

        if (FAILED(hr)) return false;

        *out_width = width;
        *out_height = height;

        return true;
    }

    bool LoadTextureFromResource(ID3D11Device* d3d_device, int resource_id, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height) {
        static bool com_init = false;
        if (!com_init) {
            CoInitializeEx(NULL, COINIT_MULTITHREADED);
            com_init = true;
        }

        HRSRC hRes = FindResourceA(DX11Base::g_hModule, MAKEINTRESOURCEA(resource_id), (LPCSTR)10); // RT_RCDATA
        if (!hRes) return false;

        DWORD resSize = SizeofResource(DX11Base::g_hModule, hRes);
        HGLOBAL hResData = LoadResource(DX11Base::g_hModule, hRes);
        if (!hResData) return false;

        void* pResData = LockResource(hResData);
        if (!pResData) return false;

        ComPtr<IWICImagingFactory> wic_factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_factory));
        if (FAILED(hr)) return false;

        ComPtr<IWICStream> stream;
        hr = wic_factory->CreateStream(&stream);
        if (FAILED(hr)) return false;

        hr = stream->InitializeFromMemory(static_cast<BYTE*>(pResData), resSize);
        if (FAILED(hr)) return false;

        ComPtr<IWICBitmapDecoder> decoder;
        hr = wic_factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr)) return false;

        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) return false;

        UINT width, height;
        hr = frame->GetSize(&width, &height);
        if (FAILED(hr)) return false;

        ComPtr<IWICFormatConverter> converter;
        hr = wic_factory->CreateFormatConverter(&converter);
        if (FAILED(hr)) return false;

        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) return false;

        *out_width = static_cast<int>(width);
        *out_height = static_cast<int>(height);

        std::vector<unsigned char> pixels(width * height * 4);
        hr = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
        if (FAILED(hr)) return false;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA subResource = {};
        subResource.pSysMem = pixels.data();
        subResource.SysMemPitch = width * 4;

        ComPtr<ID3D11Texture2D> texture;
        hr = d3d_device->CreateTexture2D(&desc, &subResource, &texture);
        if (FAILED(hr)) return false;

        hr = d3d_device->CreateShaderResourceView(texture.Get(), nullptr, out_srv);
        return SUCCEEDED(hr);
    }
}
