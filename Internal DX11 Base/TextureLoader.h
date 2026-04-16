#pragma once
#include <d3d11.h>

namespace DX11Base {
    bool LoadTextureFromFile(ID3D11Device* d3d_device, const wchar_t* filename, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height);
    bool LoadTextureFromResource(ID3D11Device* d3d_device, int resource_id, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height);
}
