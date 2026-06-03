#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <util/util.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <climits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "include/main.h"
#include "include/config.h"
#include "include/game_data_archive.h"
#include "include/interface.h"
#include "include/shaiya/CCharacter.h"
#include "include/shaiya/CMonster.h"
#include "include/shaiya/CSheetStatus.h"
#include "include/shaiya/CStaticText.h"
#include "include/shaiya/CTexture.h"
#include "include/shaiya/CWindow.h"
#include "include/shaiya/CWorldMgr.h"
#include "include/shaiya/Static.h"
#include "include/shaiya/TargetType.h"
#include <external/stb/stb_image.h>

using namespace shaiya;

namespace
{
    // -----------------------------------------------------------------------
    // Miscellaneous patch constants
    // -----------------------------------------------------------------------
    constexpr std::uint8_t kBuffSpacing = 0x24;
    constexpr std::uint8_t kBuffCountPerRow = 0x09;
    constexpr std::int32_t kFastTransitionDelay = 0x00000064;
    constexpr std::uint32_t kLoginSplashSkipSplashSleepDelay = 0x00000000;
    constexpr std::uint32_t kLoginSplashSkipPostInitDelay = 0x00000000;
    constexpr char kLoginSplashSkipHiddenCopyrightMessage[100] = "";
    constexpr std::int32_t kClientRessLeaderVisualSeconds = 5;
    constexpr std::uint8_t kForceDeathDialogNonUltimate[] = { 0xE9, 0x84, 0x00, 0x00, 0x00, 0x90 };
    float gLogoutGameOverVisualSeconds = 2.0f;
    constexpr std::uint32_t kHiddenLevelUpMessageSize = 0x00000000;
    constexpr char kScreenshotPngFilenameFormats[] =
        "%s000%d.PNG\0"
        "%s00%d.PNG\0\0"
        "%s0%d.PNG\0\0\0"
        "%s%d.PNG\0\0\0\0"
        "%s000%d.png\0"
        "%s00%d.png\0\0"
        "%s0%d.png\0\0\0"
        "%s%d.png\0\0\0\0";
    static_assert(sizeof(kScreenshotPngFilenameFormats) == 97);

    using TextureCreateFromFileEx = int(__cdecl*)(
        void* device,
        const char* fileName,
        int width,
        int height,
        int mipLevels,
        int usage,
        int format,
        int pool,
        int filter,
        int mipFilter,
        int colorKey,
        void* srcInfo,
        void* palette,
        void* texture);

    TextureCreateFromFileEx gNativeTextureCreateFromFileEx = nullptr;
    constexpr unsigned char kTextureCreateFromFileExOriginalBytes[] = { 0x80, 0x3D, 0x78, 0x1F, 0x2F, 0x02, 0x00 };

    namespace movable_buffs
    {
        constexpr DWORD kMouseXAddr = 0x7C3C0C;
        constexpr DWORD kMouseYAddr = 0x7C3C10;
        constexpr DWORD kMouseTrampolineReturn = 0x004D73F8;
        constexpr DWORD kNewLocationTrampolineReturn = 0x004D7509;
        constexpr const char* kIniSection = "BUFF";
        constexpr const char* kIniKeyX = "LOCATION_X";
        constexpr const char* kIniKeyY = "LOCATION_Y";

        volatile LONG g_locationX = 0;
        volatile LONG g_locationY = 0;
        int g_savedX = INT_MIN;
        int g_savedY = INT_MIN;

        const std::string& get_config_ini_path()
        {
            static std::string path = game_data::relative_path("CONFIG.ini");
            return path;
        }

        void load_ini()
        {
            auto iniPath = get_config_ini_path();
            g_locationX = GetPrivateProfileIntA(kIniSection, kIniKeyX, 0, iniPath.c_str());
            g_locationY = GetPrivateProfileIntA(kIniSection, kIniKeyY, 0, iniPath.c_str());
            g_savedX = static_cast<int>(g_locationX);
            g_savedY = static_cast<int>(g_locationY);
        }

        void write_ini_int(const char* key, int value)
        {
            char buffer[16]{};
            std::snprintf(buffer, sizeof(buffer), "%d", value);
            auto iniPath = get_config_ini_path();
            WritePrivateProfileStringA(kIniSection, key, buffer, iniPath.c_str());
        }

        void save_if_changed()
        {
            auto x = static_cast<int>(g_locationX);
            auto y = static_cast<int>(g_locationY);
            if (x == g_savedX && y == g_savedY)
                return;

            write_ini_int(kIniKeyX, x);
            write_ini_int(kIniKeyY, y);
            g_savedX = x;
            g_savedY = y;
        }

        DWORD WINAPI worker(LPVOID)
        {
            for (;;)
            {
                const auto f6 = GetAsyncKeyState(VK_F6);
                const auto leftButton = GetAsyncKeyState(VK_LBUTTON);
                if ((f6 & 0x8000) && (leftButton & 0x8000))
                {
                    g_locationX = *reinterpret_cast<int*>(kMouseXAddr);
                    g_locationY = *reinterpret_cast<int*>(kMouseYAddr);
                    save_if_changed();
                    Sleep(150);
                }
                else
                {
                    Sleep(15);
                }
            }
        }

        __declspec(naked) void mouse_trampoline()
        {
            __asm
            {
                mov edi, dword ptr[g_locationX]
                mov ebx, dword ptr[g_locationY]
                jmp kMouseTrampolineReturn
            }
        }

        __declspec(naked) void new_location_trampoline()
        {
            __asm
            {
                mov edi, dword ptr[g_locationX]
                jmp kNewLocationTrampolineReturn
            }
        }

        void install()
        {
            load_ini();
            util::detour((void*)0x004D73ED, mouse_trampoline, 11);
            util::detour((void*)0x004D7503, new_location_trampoline, 6);
            CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
        }
    }

    void release_texture_slots(shaiya::CTexture* texture)
    {
        auto slots = reinterpret_cast<IUnknown**>(texture);
        for (auto index = 0; index < 2; ++index)
        {
            if (slots[index])
            {
                slots[index]->Release();
                slots[index] = nullptr;
            }
        }
    }

    const char* find_replaced_texture_extension(const char* fileName)
    {
        if (!fileName)
            return nullptr;

        auto length = std::strlen(fileName);
        if (length >= 4
            && (_stricmp(fileName + length - 4, ".tga") == 0
                || _stricmp(fileName + length - 4, ".jpg") == 0))
        {
            return fileName + length - 4;
        }

        if (length >= 5 && _stricmp(fileName + length - 5, ".jpeg") == 0)
            return fileName + length - 5;

        return nullptr;
    }

    bool build_joined_path(char* output, std::size_t outputSize, const char* path, const char* fileName)
    {
        if (!output || outputSize == 0 || !path || !fileName)
            return false;

        auto written = std::snprintf(output, outputSize, "%s/%s", path, fileName);
        return written > 0 && static_cast<std::size_t>(written) < outputSize;
    }

    bool build_png_file_name(char* output, std::size_t outputSize, const char* fileName, bool uppercaseExtension)
    {
        auto* extension = find_replaced_texture_extension(fileName);
        if (!output || outputSize == 0 || !extension)
            return false;

        auto prefixLength = static_cast<std::size_t>(extension - fileName);
        if (prefixLength + 4 >= outputSize)
            return false;

        std::memcpy(output, fileName, prefixLength);
        std::memcpy(output + prefixLength, uppercaseExtension ? ".PNG" : ".png", 5);
        return true;
    }

    std::string normalize_archive_path(std::string path)
    {
        path = game_data::lower_ascii(std::move(path));
        for (auto& ch : path)
        {
            if (ch == '/')
                ch = '\\';
        }

        while (path.rfind(".\\", 0) == 0)
            path.erase(0, 2);

        return path;
    }

    std::string strip_data_prefix(std::string path)
    {
        path = normalize_archive_path(std::move(path));
        constexpr const char* prefix = "data\\";
        if (path.rfind(prefix, 0) == 0)
            path.erase(0, std::strlen(prefix));

        return path;
    }

    std::string strip_interface_prefix(std::string path)
    {
        path = strip_data_prefix(std::move(path));
        constexpr const char* prefix = "interface\\";
        if (path.rfind(prefix, 0) == 0)
            path.erase(0, std::strlen(prefix));

        return path;
    }

    void add_archive_path_variants(
        std::unordered_set<std::string>& paths,
        std::unordered_map<std::string, game_data::SahFileEntry>& entries,
        std::string path,
        const game_data::SahFileEntry& entry)
    {
        path = normalize_archive_path(std::move(path));
        if (path.empty())
            return;

        paths.insert(path);
        entries.emplace(path, entry);

        auto withoutData = strip_data_prefix(path);
        paths.insert(withoutData);
        entries.emplace(withoutData, entry);

        if (withoutData == path)
        {
            paths.insert(std::string("data\\") + path);
            entries.emplace(std::string("data\\") + path, entry);
        }

        auto withoutInterface = strip_interface_prefix(path);
        paths.insert(withoutInterface);
        paths.insert(std::string("interface\\") + withoutInterface);
        paths.insert(std::string("data\\interface\\") + withoutInterface);
        entries.emplace(withoutInterface, entry);
        entries.emplace(std::string("interface\\") + withoutInterface, entry);
        entries.emplace(std::string("data\\interface\\") + withoutInterface, entry);
    }

    struct ArchiveTextureIndex
    {
        std::unordered_set<std::string> paths;
        std::unordered_map<std::string, game_data::SahFileEntry> entries;
    };

    const ArchiveTextureIndex& archive_texture_index()
    {
        static ArchiveTextureIndex index = [] {
            ArchiveTextureIndex result;
            game_data::scan_sah_files([&](const game_data::SahFileEntry& entry) {
                if (entry.lowerPath.empty())
                    add_archive_path_variants(result.paths, result.entries, entry.lowerFileName, entry);
                else
                    add_archive_path_variants(result.paths, result.entries, entry.lowerPath + "\\" + entry.lowerFileName, entry);
            });
            return result;
        }();
        return index;
    }

    bool archive_texture_file_exists(const char* relativePath)
    {
        if (!relativePath || !relativePath[0])
            return false;

        auto normalized = normalize_archive_path(relativePath);
        auto& archivePaths = archive_texture_index().paths;
        if (archivePaths.find(normalized) != archivePaths.end())
            return true;

        auto withoutData = strip_data_prefix(normalized);
        if (archivePaths.find(withoutData) != archivePaths.end())
            return true;

        auto withoutInterface = strip_interface_prefix(normalized);
        if (archivePaths.find(withoutInterface) != archivePaths.end()
            || archivePaths.find(std::string("interface\\") + withoutInterface) != archivePaths.end()
            || archivePaths.find(std::string("data\\interface\\") + withoutInterface) != archivePaths.end())
        {
            return true;
        }

        if (withoutData == normalized)
            return archivePaths.find(std::string("data\\") + normalized) != archivePaths.end();

        return false;
    }

    const game_data::SahFileEntry* find_archive_texture_entry(const char* relativePath)
    {
        if (!relativePath || !relativePath[0])
            return nullptr;

        auto& entries = archive_texture_index().entries;
        auto normalized = normalize_archive_path(relativePath);
        auto found = entries.find(normalized);
        if (found != entries.end())
            return &found->second;

        auto withoutData = strip_data_prefix(normalized);
        found = entries.find(withoutData);
        if (found != entries.end())
            return &found->second;

        auto withoutInterface = strip_interface_prefix(normalized);
        found = entries.find(withoutInterface);
        if (found != entries.end())
            return &found->second;

        found = entries.find(std::string("interface\\") + withoutInterface);
        if (found != entries.end())
            return &found->second;

        found = entries.find(std::string("data\\interface\\") + withoutInterface);
        if (found != entries.end())
            return &found->second;

        if (withoutData == normalized)
        {
            found = entries.find(std::string("data\\") + normalized);
            if (found != entries.end())
                return &found->second;
        }

        return nullptr;
    }

    int load_texture_file(shaiya::CTexture* texture, const char* relativePath)
    {
        auto device = *reinterpret_cast<void**>(0x22B69A8);
        auto nativeLoad = gNativeTextureCreateFromFileEx
            ? gNativeTextureCreateFromFileEx
            : reinterpret_cast<TextureCreateFromFileEx>(0x40DB00);
        return nativeLoad(device, relativePath, -1, -1, 1, 0, 0, 1, -1, -1, 0, nullptr, nullptr, texture);
    }

    LPDIRECT3DTEXTURE9 create_texture_from_image_memory(const void* data, std::size_t dataSize)
    {
        auto device = *reinterpret_cast<LPDIRECT3DDEVICE9*>(0x22B69A8);
        if (!device || !data || dataSize == 0 || dataSize > INT_MAX)
            return nullptr;

        int width = 0;
        int height = 0;
        int channels = 0;
        auto* pixels = stbi_load_from_memory(
            static_cast<const stbi_uc*>(data),
            static_cast<int>(dataSize),
            &width,
            &height,
            &channels,
            4);
        if (!pixels || width <= 0 || height <= 0)
        {
            if (pixels)
                stbi_image_free(pixels);
            return nullptr;
        }

        LPDIRECT3DTEXTURE9 d3dTexture = nullptr;
        if (FAILED(device->CreateTexture(
            static_cast<UINT>(width),
            static_cast<UINT>(height),
            1,
            0,
            D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED,
            &d3dTexture,
            nullptr)) || !d3dTexture)
        {
            stbi_image_free(pixels);
            return nullptr;
        }

        D3DLOCKED_RECT locked{};
        if (FAILED(d3dTexture->LockRect(0, &locked, nullptr, 0)))
        {
            d3dTexture->Release();
            stbi_image_free(pixels);
            return nullptr;
        }

        for (int y = 0; y < height; ++y)
        {
            auto* src = pixels + y * width * 4;
            auto* dst = static_cast<BYTE*>(locked.pBits) + y * locked.Pitch;
            for (int x = 0; x < width; ++x)
            {
                dst[x * 4 + 0] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = src[x * 4 + 3];
            }
        }

        d3dTexture->UnlockRect(0);
        stbi_image_free(pixels);
        return d3dTexture;
    }

    bool assign_texture_size(shaiya::CTexture* texture, int width, int height)
    {
        if (!texture)
            return false;

        if (width == 0 && height == 0)
        {
            if (!texture->texture)
                return false;

            D3DSURFACE_DESC desc{};
            if (FAILED(texture->texture->GetLevelDesc(0, &desc)))
                return false;

            texture->size.width = static_cast<float>(desc.Width);
            texture->size.height = static_cast<float>(desc.Height);
            return true;
        }

        texture->size.width = static_cast<float>(width);
        texture->size.height = static_cast<float>(height);
        return true;
    }

    int try_load_texture(shaiya::CTexture* texture, const char* relativePath, int width, int height)
    {
        if (load_texture_file(texture, relativePath) < 0)
            return 0;

        return assign_texture_size(texture, width, height) ? 1 : 0;
    }

    int try_load_png_texture_pointer(void* textureOut, const char* relativePath)
    {
        auto* entry = find_archive_texture_entry(relativePath);
        if (!entry || !textureOut)
            return 0;

        std::vector<char> fileData;
        if (!game_data::read_saf_file(entry->offset, entry->size, fileData))
            return 0;

        auto* d3dTexture = create_texture_from_image_memory(fileData.data(), fileData.size());
        if (!d3dTexture)
            return 0;

        *static_cast<LPDIRECT3DTEXTURE9*>(textureOut) = d3dTexture;
        return 1;
    }

    int try_load_png_texture(shaiya::CTexture* texture, const char* relativePath, int width, int height)
    {
        if (!try_load_png_texture_pointer(texture ? &texture->texture : nullptr, relativePath))
            return 0;

        if (assign_texture_size(texture, width, height))
            return 1;

        release_texture_slots(texture);
        return 0;
    }

    // Rewrites "data/interface" paths to the active UI folder (EP6/EP7)
    // when the INI selects a non-default interface pack.
    const char* resolve_ui_path(const char* path)
    {
        if (!path)
            return path;

        if (_stricmp(path, "data/interface") == 0 || _stricmp(path, "data\\interface") == 0)
            return config::ui_interface_path();

        return path;
    }

    // Rewrites a full file path (e.g. "data/interface/foo.tga") replacing
    // the interface folder prefix with the active UI folder.
    bool resolve_ui_full_path(char* output, std::size_t outputSize, const char* fullPath)
    {
        if (!fullPath || !output || outputSize == 0)
            return false;

        if (config::ui_mode() == config::UiMode::EP4)
            return false;  // no rewrite needed

        // Match "data/interface/" or "data\interface\"
        constexpr const char* kPrefixFwd = "data/interface/";
        constexpr const char* kPrefixBck = "data\\interface\\";
        constexpr auto kPrefixLen = 15;  // strlen("data/interface/")

        const char* suffix = nullptr;
        if (_strnicmp(fullPath, kPrefixFwd, kPrefixLen) == 0)
            suffix = fullPath + kPrefixLen;
        else if (_strnicmp(fullPath, kPrefixBck, kPrefixLen) == 0)
            suffix = fullPath + kPrefixLen;

        if (!suffix)
            return false;

        auto written = std::snprintf(output, outputSize, "%s/%s",
            config::ui_interface_path(), suffix);
        return written > 0 && static_cast<std::size_t>(written) < outputSize;
    }

    int __cdecl texture_create_from_file_ex_png_hook(
        void* device,
        const char* fileName,
        int width,
        int height,
        int mipLevels,
        int usage,
        int format,
        int pool,
        int filter,
        int mipFilter,
        int colorKey,
        void* srcInfo,
        void* palette,
        void* textureOut)
    {
        // Rewrite interface path for EP6/EP7 UI modes
        char resolvedPath[MAX_PATH]{};
        if (resolve_ui_full_path(resolvedPath, sizeof(resolvedPath), fileName))
            fileName = resolvedPath;

        if (find_replaced_texture_extension(fileName))
        {
            char pngPath[MAX_PATH]{};
            if (build_png_file_name(pngPath, sizeof(pngPath), fileName, false)
                && try_load_png_texture_pointer(textureOut, pngPath))
            {
                return 0;
            }

            if (build_png_file_name(pngPath, sizeof(pngPath), fileName, true)
                && try_load_png_texture_pointer(textureOut, pngPath))
            {
                return 0;
            }
        }

        return gNativeTextureCreateFromFileEx
            ? gNativeTextureCreateFromFileEx(
                device,
                fileName,
                width,
                height,
                mipLevels,
                usage,
                format,
                pool,
                filter,
                mipFilter,
                colorKey,
                srcInfo,
                palette,
                textureOut)
            : -1;
    }

    int __fastcall create_texture_from_file_png(
        shaiya::CTexture* texture,
        void*,
        const char* path,
        const char* fileName,
        int width,
        int height)
    {
        if (!texture)
            return 0;

        path = resolve_ui_path(path);
        release_texture_slots(texture);

        char originalPath[MAX_PATH]{};
        if (!build_joined_path(originalPath, sizeof(originalPath), path, fileName))
            return 0;

        if (find_replaced_texture_extension(fileName))
        {
            char pngFileName[MAX_PATH]{};
            char pngPath[MAX_PATH]{};
            if (build_png_file_name(pngFileName, sizeof(pngFileName), fileName, false)
                && build_joined_path(pngPath, sizeof(pngPath), path, pngFileName)
                && archive_texture_file_exists(pngPath)
                && (try_load_png_texture(texture, pngPath, width, height)
                    || try_load_texture(texture, pngPath, width, height)))
            {
                return 1;
            }

            release_texture_slots(texture);
            if (build_png_file_name(pngFileName, sizeof(pngFileName), fileName, true)
                && build_joined_path(pngPath, sizeof(pngPath), path, pngFileName)
                && archive_texture_file_exists(pngPath)
                && (try_load_png_texture(texture, pngPath, width, height)
                    || try_load_texture(texture, pngPath, width, height)))
            {
                return 1;
            }

            release_texture_slots(texture);
        }

        return try_load_texture(texture, originalPath, width, height);
    }

    TextureCreateFromFileEx create_texture_loader_trampoline()
    {
        constexpr auto kJumpSize = 5;
        auto size = sizeof(kTextureCreateFromFileExOriginalBytes) + kJumpSize;
        auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!code)
            return nullptr;

        std::memcpy(code, kTextureCreateFromFileExOriginalBytes, sizeof(kTextureCreateFromFileExOriginalBytes));
        code[sizeof(kTextureCreateFromFileExOriginalBytes)] = 0xE9;

        auto jumpFrom = reinterpret_cast<std::uintptr_t>(code + sizeof(kTextureCreateFromFileExOriginalBytes));
        auto jumpTo = std::uintptr_t{ 0x40DB07 };
        auto relative = static_cast<std::uint32_t>(jumpTo - jumpFrom - kJumpSize);
        std::memcpy(code + sizeof(kTextureCreateFromFileExOriginalBytes) + 1, &relative, sizeof(relative));

        FlushInstructionCache(GetCurrentProcess(), code, size);
        return reinterpret_cast<TextureCreateFromFileEx>(code);
    }

}

// ===========================================================================
// Consolidated detour functions (from former standalone files)
// ===========================================================================

namespace
{
    // -- Camera limit --
    unsigned u0x442BD3 = 0x442BD3;
    void __declspec(naked) naked_0x442BCD()
    {
        __asm
        {
            fld dword ptr[g_cameraLimit]
            jmp u0x442BD3
        }
    }

    // -- Blacksmith rune filter --
    unsigned u0x4C4426 = 0x4C4426;
    unsigned u0x4C4440 = 0x4C4440;
    void __declspec(naked) naked_0x4C4421()
    {
        __asm
        {
            mov al,[eax+0x2C]
            cmp al,0xDC
            jl _original
            cmp al,0xF0
            jg _original
            jmp u0x4C4440
        _original:
            cmp al,0x3E
            jmp u0x4C4426
        }
    }

    // -- Equipment: two-hand weapon detection --
    void __declspec(naked) naked_0x4E83CA()
    {
        __asm
        {
            movzx eax,al
            cmp eax,0x2
            je return_1
            cmp eax,0x4
            je return_1
            cmp eax,0x5
            je return_1
            cmp eax,0x6
            je return_1
            cmp eax,0x8
            je return_1
            xor eax,eax
            retn 0x4
        return_1:
            mov eax,0x1
            retn 0x4
        }
    }

    // -- Weapon step: exact values --
    unsigned u0x4E753B = 0x4E753B;
    void __declspec(naked) naked_0x4E7506()
    {
        __asm
        {
            movzx ecx,word ptr[ecx+edx*0x2+0x6268]
            mov eax,ecx
            jmp u0x4E753B
        }
    }

    // -- Input: hold-click stats x10 --
    void apply_status_points_by_key_state(shaiya::CSheetStatus* sheetStatus, int statusIndex, int addPoints)
    {
        auto leftButtonDown = GetAsyncKeyState(VK_LBUTTON) & 0x8000;
        if (leftButtonDown && sheetStatus->usablePoints >= addPoints)
        {
            sheetStatus->usablePoints -= addPoints;
            sheetStatus->addPoints[statusIndex] += addPoints;
        }
        else
        {
            sheetStatus->usablePoints -= 1;
            sheetStatus->addPoints[statusIndex] += 1;
        }
    }

    unsigned u0x528D96 = 0x528D96;
    void __declspec(naked) naked_0x528D8D()
    {
        __asm
        {
            pushad
            push 0xA
            push eax
            push esi
            call apply_status_points_by_key_state
            add esp,0xC
            popad
            jmp u0x528D96
        }
    }

    // -- Main stats draw: welcome message tick --
    void on_main_stats_draw(shaiya::CWindow*)
    {
        tick_client_welcome_sysmsg();
    }

    unsigned u0x532A2A = 0x532A2A;
    void __declspec(naked) naked_0x532A23()
    {
        __asm
        {
            pushad
            push esi
            call on_main_stats_draw
            add esp,0x4
            popad
            mov ecx,[esp+0x108]
            jmp u0x532A2A
        }
    }

    // -- Character allocation and custom members --
    namespace character
    {
        void init(CCharacter* user) { user->title = {}; }

        void reset(CCharacter* user)
        {
            if (user->title.text)
            {
                if (user->title.text->texture)
                {
                    user->title.text->texture->Release();
                    user->title.text->texture = nullptr;
                }
                Static::operator_delete(user->title.text);
                user->title.text = nullptr;
            }
        }
    }

    unsigned u0x419E79 = 0x419E79;
    void __declspec(naked) naked_0x419E73()
    {
        __asm
        {
            mov [esi+0x434],ebx
            pushad
            push esi
            call character::init
            add esp,0x4
            popad
            jmp u0x419E79
        }
    }

    unsigned u0x419623 = 0x419623;
    void __declspec(naked) naked_0x41961D()
    {
        __asm
        {
            pushad
            push edi
            call character::reset
            add esp,0x4
            popad
            mov eax,[edi+0x2AC]
            jmp u0x419623
        }
    }

    // -- Target HP view --
    namespace target_view
    {
        LPD3DXFONT get_current_game_font()
        {
            if (g_var->camera.d3dxFont0) return g_var->camera.d3dxFont0;
            if (g_var->camera.d3dxFont1) return g_var->camera.d3dxFont1;
            if (g_var->camera.d3dxFont2) return g_var->camera.d3dxFont2;
            return g_var->camera.d3dxFont3;
        }

        constexpr int kHpTextOffsetX_EP4 = 66;
        constexpr int kHpTextOffsetY_EP4 = 35;
        constexpr int kHpTextOffsetX_EP67 = 66;
        constexpr int kHpTextOffsetY_EP67 = 22;

        void draw_hp_text(CWindow* window, uint32_t currentHealth, uint32_t maxHealth)
        {
            if (!window || maxHealth == 0) return;
            currentHealth = std::min(currentHealth, maxHealth);
            char text[32]{};
            std::snprintf(text, sizeof(text), "%u/%u", currentHealth, maxHealth);
            bool ep4 = config::ui_mode() == config::UiMode::EP4;
            int x = static_cast<int>(window->pos.x) + (ep4 ? kHpTextOffsetX_EP4 : kHpTextOffsetX_EP67);
            int y = static_cast<int>(window->pos.y) + (ep4 ? kHpTextOffsetY_EP4 : kHpTextOffsetY_EP67);
            auto font = get_current_game_font();
            if (font)
            {
                RECT rect{ x, y, x + 200, y + 24 };
                font->DrawTextA(nullptr, text, -1, &rect, DT_NOCLIP, 0xFFFFFFFF);
            }
            else
                Static::DrawText_ViewPoint(x, y, 0xFFFFFFFF, text);
        }

        void draw_target_hp(CWindow* window)
        {
            if (!g_var->targetId) return;
            if (g_var->targetType == TargetType::Mob)
            {
                auto mob = CWorldMgr::FindMob(g_var->targetId);
                if (mob) draw_hp_text(window, mob->health, mob->maxHealth);
            }
            else if (g_var->targetType == TargetType::User)
            {
                auto user = CWorldMgr::FindUser(g_var->targetId);
                if (user) draw_hp_text(window, user->health, user->maxHealth);
            }
        }
    }

    unsigned u0x5351BD = 0x5351BD;
    void __declspec(naked) naked_target_view()
    {
        __asm
        {
            pushad
            push esi
            call target_view::draw_target_hp
            add esp,0x4
            popad
            mov ecx,dword ptr [esp+0x1A4]
            jmp u0x5351BD
        }
    }

}

// ===========================================================================
// hook::patch — main orchestrator
// ===========================================================================
void hook::patch()
{
    static constexpr unsigned char kPushZero[] = {0x6A, 0x00};
    static constexpr unsigned char kPushMinusOne[] = {0x68, 0xFF, 0xFF, 0xFF, 0xFF};
    static constexpr unsigned char kBypassCharacterCreateNameFormatCheck[] = {0xEB, 0x34, 0x90, 0x90};
    static constexpr unsigned char kBypassCharacterCreateNameSubstringChecks[] = {0xE9, 0x84, 0x00, 0x00, 0x00, 0x90};
    static constexpr unsigned char kBypassCharacterCreateNameVerifiedGate[] = {0xEB};
    static constexpr unsigned char kBypassCharacterCreateNameAvailabilityRequest[] = {0xE9, 0xC2, 0x04, 0x00, 0x00};
    static constexpr unsigned char kTreatCharacterCreateResultBusyAsSuccess[] = {0x74, 0x22};
    static constexpr unsigned char kLoginSplashSkipRenderBlock[] = {0xE9, 0xD8, 0x02, 0x00, 0x00};
    static constexpr unsigned char kLoginSplashSkipAfterResourceInit[] = {0xE9, 0x96, 0x02, 0x00, 0x00};

    // -- Startup and login ---------------------------------------------------
    config::install_skip_updater();

    // Login splash skip
    util::write_memory((void*)0x74A5A0, kLoginSplashSkipHiddenCopyrightMessage, sizeof(kLoginSplashSkipHiddenCopyrightMessage));
    util::write_memory((void*)0x4346B1, &kLoginSplashSkipSplashSleepDelay, sizeof(kLoginSplashSkipSplashSleepDelay));
    util::write_memory((void*)0x434ACD, &kLoginSplashSkipPostInitDelay, sizeof(kLoginSplashSkipPostInitDelay));
    util::write_memory((void*)0x4346BB, kLoginSplashSkipRenderBlock, sizeof(kLoginSplashSkipRenderBlock));
    util::write_memory((void*)0x434A97, kLoginSplashSkipAfterResourceInit, sizeof(kLoginSplashSkipAfterResourceInit));

    // Background rendering arguments
    util::write_memory((void*)0x434742, kPushZero, sizeof(kPushZero));
    util::write_memory((void*)0x434880, kPushZero, sizeof(kPushZero));
    util::write_memory((void*)0x434B42, kPushZero, sizeof(kPushZero));
    util::write_memory((void*)0x4347FC, kPushMinusOne, sizeof(kPushMinusOne));
    util::write_memory((void*)0x43493A, kPushMinusOne, sizeof(kPushMinusOne));

    // Fast transition delay
    util::write_memory((void*)0x436941, &kFastTransitionDelay, sizeof(kFastTransitionDelay));
    util::write_memory((void*)0x4D0341, &kFastTransitionDelay, sizeof(kFastTransitionDelay));

    // -- Character allocation ------------------------------------------------
    util::detour((void*)0x419E73, naked_0x419E73, 6);
    util::detour((void*)0x41961D, naked_0x41961D, 6);
    int charAllocSize = 0x444;
    util::write_memory((void*)0x41CC97, &charAllocSize, 4);
    util::write_memory((void*)0x41F055, &charAllocSize, 4);
    util::write_memory((void*)0x476F9F, &charAllocSize, 4);
    util::write_memory((void*)0x490250, &charAllocSize, 4);
    util::write_memory((void*)0x4EFE68, &charAllocSize, 4);
    util::write_memory((void*)0x4F7318, &charAllocSize, 4);
    util::write_memory((void*)0x59A3F3, &charAllocSize, 4);
    util::write_memory((void*)0x59A6DD, &charAllocSize, 4);

    // -- Character creation --------------------------------------------------
    // Bypass local ASCII-only name validation
    util::write_memory((void*)0x472214, kBypassCharacterCreateNameFormatCheck, sizeof(kBypassCharacterCreateNameFormatCheck));
    // Bypass blocked-substring checks
    util::write_memory((void*)0x472280, kBypassCharacterCreateNameSubstringChecks, sizeof(kBypassCharacterCreateNameSubstringChecks));
    // Skip "name already verified" UI gate
    util::write_memory((void*)0x472146, kBypassCharacterCreateNameVerifiedGate, sizeof(kBypassCharacterCreateNameVerifiedGate));
    // Skip availability request, go directly to creation packet
    util::write_memory((void*)0x47243A, kBypassCharacterCreateNameAvailabilityRequest, sizeof(kBypassCharacterCreateNameAvailabilityRequest));
    // Treat legacy "busy" result as success
    util::write_memory((void*)0x472A12, kTreatCharacterCreateResultBusyAsSuccess, sizeof(kTreatCharacterCreateResultBusyAsSuccess));

    // -- Combat and UI -------------------------------------------------------
    // Leader resurrection visual timer (30s → 5s)
    util::write_memory((void*)0x4D5F47, &kClientRessLeaderVisualSeconds, sizeof(kClientRessLeaderVisualSeconds));
    // UM characters can use leader resurrection dialog
    util::write_memory((void*)0x4D5BB4, kForceDeathDialogNonUltimate, sizeof(kForceDeathDialogNonUltimate));
    // Logout/game-over visual countdown (fld from custom float)
    std::uint8_t logoutVisualInitPatch[] = { 0xD9, 0x05, 0x00, 0x00, 0x00, 0x00 };
    auto logoutVisualSecondsAddress = reinterpret_cast<std::uint32_t>(&gLogoutGameOverVisualSeconds);
    std::memcpy(&logoutVisualInitPatch[2], &logoutVisualSecondsAddress, sizeof(logoutVisualSecondsAddress));
    util::write_memory((void*)0x5223A9, logoutVisualInitPatch, sizeof(logoutVisualInitPatch));
    // Buff row spacing and count
    util::write_memory((void*)0x4D74E9, &kBuffSpacing, sizeof(kBuffSpacing));
    util::write_memory((void*)0x4D750D, &kBuffSpacing, sizeof(kBuffSpacing));
    util::write_memory((void*)0x4D74EC, &kBuffCountPerRow, sizeof(kBuffCountPerRow));
    // Movable buff position (F6 + left click)
    movable_buffs::install();
    // Hold left click to apply stats x10
    util::detour((void*)0x528D8D, naked_0x528D8D, 9);
    // Remove vanilla GM H-key HP viewer
    util::write_memory((void*)0x534817, 0x1, 1);
    // Welcome message timer tick
    util::detour((void*)0x532A23, naked_0x532A23, 7);
    // Target HP view
    util::detour((void*)0x5351B6, naked_target_view, 7);
    // NPC-to-go movement bug workaround
    util::write_memory((void*)0x444129, 0xEB, 1);

    // -- Equipment and items -------------------------------------------------
    // Two-hand weapon type detection
    util::detour((void*)0x4E83CA, naked_0x4E83CA, 6);
    // Exact weapon step values
    util::detour((void*)0x4E7506, naked_0x4E7506, 8);
    // Blacksmith rune filter (effects 220..240)
    util::detour((void*)0x4C4421, naked_0x4C4421, 5);
    // Remove ep6 vehicle section (auction board)
    util::write_memory((void*)0x463FE0, 0x07, 1);
    // Speed recreation
    util::write_memory((void*)0x4C4D2F, 0x02, 1);
    // Speed enchant
    util::write_memory((void*)0x501600, 0x02, 1);
    util::write_memory((void*)0x501602, 0x02, 1);
    util::write_memory((void*)0x501631, 0x02, 1);
    util::write_memory((void*)0x501633, 0x02, 1);
    util::write_memory((void*)0x501644, 0x03, 1);
    util::write_memory((void*)0x50164D, 0x03, 1);

    // -- Visual and display --------------------------------------------------
    // Camera distance limit
    util::detour((void*)0x442BCD, naked_0x442BCD, 6);
    // Real experience values (remove x10 display multiplier)
    util::write_memory((void*)0x4963DE, 0x00, 1);
    util::write_memory((void*)0x496407, 0x00, 1);
    util::write_memory((void*)0x4F9852, 0x00, 1);
    util::write_memory((void*)0x529D05, 0x00, 1);
    util::write_memory((void*)0x529E19, 0x00, 1);
    util::write_memory((void*)0x594BA7, 0x00, 1);
    util::write_memory((void*)0x4FA494, 0x44, 1);
    // Remove level-up splash messages
    util::write_memory((void*)0x4AC538, &kHiddenLevelUpMessageSize, sizeof(kHiddenLevelUpMessageSize));
    util::write_memory((void*)0x4AC53D, &kHiddenLevelUpMessageSize, sizeof(kHiddenLevelUpMessageSize));
    util::write_memory((void*)0x4AC603, &kHiddenLevelUpMessageSize, sizeof(kHiddenLevelUpMessageSize));
    util::write_memory((void*)0x4AC608, &kHiddenLevelUpMessageSize, sizeof(kHiddenLevelUpMessageSize));
    // Show dungeon maps
    util::write_memory((void*)0x4D9497, 0x90, 2);
    // Mob/NPC ID view
    config::install_id_view();
    // Screenshot format (.PNG)
    util::write_memory((void*)0x75747C, kScreenshotPngFilenameFormats, sizeof(kScreenshotPngFilenameFormats) - 1);
    // Interface hooks shared by all UI modes (clock format)
    interface_patches::install_common();
    // EP4 layout patches (EP6/EP7 folders have their own layout)
    if (config::ui_needs_layout_patches())
        interface_patches::install();

    // -- Texture loading (TGA/JPG → PNG) -------------------------------------
    if (!gNativeTextureCreateFromFileEx)
        gNativeTextureCreateFromFileEx = create_texture_loader_trampoline();
    util::detour((void*)0x40DB00, texture_create_from_file_ex_png_hook, 7);
    util::detour((void*)0x57B560, create_texture_from_file_png, 5);

    // -- Performance workarounds ---------------------------------------------
    // Costume lag
    util::write_memory((void*)0x56F38D, 0x75, 1);
    util::write_memory((void*)0x583DED, 0x75, 1);
    // Pet/wing lag
    util::write_memory((void*)0x5881EE, 0x85, 1);

}

// ===========================================================================
// hook::select_screen
// ===========================================================================
void hook::select_screen()
{
    interface_patches::install_select_screen();
}
