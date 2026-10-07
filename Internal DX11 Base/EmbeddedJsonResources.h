#pragma once

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "resource.h"

namespace DX11Base {

extern HMODULE g_hModule;

// 기본 기재 JSON은 DLL에 포함하지 않고 외부 파일에서 읽습니다.
bool LoadDefaultTraitJsonFromFile(std::string &out);

inline bool LoadEmbeddedJsonResource(int resourceId, std::string &out) {
    if (resourceId == IDR_JSON_TRAITS_DEFAULT)
        return LoadDefaultTraitJsonFromFile(out);

    out.clear();
    if (!g_hModule)
        return false;

    HRSRC resource = FindResourceW(
        g_hModule,
        MAKEINTRESOURCEW(resourceId),
        RT_RCDATA);
    if (!resource)
        return false;

    const DWORD size = SizeofResource(g_hModule, resource);
    if (size == 0)
        return false;

    HGLOBAL loaded = LoadResource(g_hModule, resource);
    if (!loaded)
        return false;

    const void *data = LockResource(loaded);
    if (!data)
        return false;

    out.assign(
        static_cast<const char *>(data),
        static_cast<std::size_t>(size));

    if (out.size() >= 3 &&
        static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    return true;
}

inline bool ReadUtf8TextFile(
    const std::filesystem::path &path,
    std::string &out) {
    out.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;

    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();

    if (out.size() >= 3 &&
        static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    return true;
}

} // namespace DX11Base
