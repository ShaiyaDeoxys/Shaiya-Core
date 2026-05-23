#include "include/imgui_layer_internal.h"
#include "include/game_data_archive.h"
#include <external/stb/stb_image.h>
#include "include/shaiya/CDataFile.h"
#include "include/shaiya/CItem.h"
#include "include/shaiya/CTexture.h"
#include "include/shaiya/ItemInfo.h"
#pragma comment(lib, "d3d9.lib")

namespace imgui_layer {
    namespace
    {
        struct ClientAtlasFile
        {
            uint64_t offset = 0;
            uint64_t size = 0;
        };

        bool g_clientIconAtlasScanned = false;
        std::unordered_map<std::string, ClientAtlasFile> g_clientIconAtlasFiles;

        bool is_client_icon_atlas_folder(const std::string& lowerPath)
        {
            return lowerPath == "interface\\icon"
                || lowerPath == "data\\interface\\icon"
                || lowerPath == "data/interface/icon";
        }

        void ensure_client_icon_atlas_index()
        {
            if (g_clientIconAtlasScanned)
                return;

            g_clientIconAtlasScanned = true;
            game_data::scan_sah_files([](const game_data::SahFileEntry& entry) {
                if (is_client_icon_atlas_folder(entry.lowerPath)
                    && entry.lowerFileName.size() > 4
                    && entry.lowerFileName.ends_with(".dds"))
                {
                    g_clientIconAtlasFiles.try_emplace(
                        entry.lowerFileName,
                        ClientAtlasFile{ entry.offset, entry.size });
                }
            });
        }
    }

    int count_inventory_item(uint8_t type, uint8_t typeId)
    {
        auto count = 0;
        auto maxBag = g_pPlayerData->inventory.size() - 1;

        for (size_t bag = 1; bag <= maxBag; ++bag)
        {
            for (const auto& item : g_pPlayerData->inventory[bag])
            {
                if (item.type == type && item.typeId == typeId)
                    count += item.count;
            }
        }

        return count;
    }

    int resolve_item_icon_index(int type, int typeId)
    {
        auto* itemInfo = CDataFile::GetItemInfo(type, typeId);
        if (!itemInfo)
            return -1;

        return static_cast<int>(itemInfo->icon);
    }

    const char* get_native_item_icon_folder()
    {
        static constexpr const char* kDefaultIconFolder = "data/interface/icon";
        auto kConfiguredIconFolder = reinterpret_cast<const char*>(0x7AC104);
        if (GetFileAttributesA(kDefaultIconFolder) != INVALID_FILE_ATTRIBUTES)
            return kDefaultIconFolder;
        if (kConfiguredIconFolder && kConfiguredIconFolder[0])
            return kConfiguredIconFolder;
        return kDefaultIconFolder;
    }

    CTexture* get_or_load_native_item_icon_texture(int iconId)
    {
        if (iconId < 0)
            return nullptr;

        auto& entry = g_nativeItemIcons[iconId];
        if (!entry.loadAttempted)
        {
            entry.iconId = iconId;
            entry.loadAttempted = true;

            char fileName[32]{};
            std::snprintf(fileName, sizeof(fileName), iconId < 100 ? "%02d.dds" : "%d.dds", iconId);
            if (!CTexture::CreateFromFile(&entry.texture, get_native_item_icon_folder(), fileName, 32, 32))
            {
                std::snprintf(fileName, sizeof(fileName), iconId < 100 ? "%02d.tga" : "%d.tga", iconId);
                CTexture::CreateFromFile(&entry.texture, get_native_item_icon_folder(), fileName, 32, 32);
            }
        }

        return entry.texture.texture ? &entry.texture : nullptr;
    }

    const char* item_icon_atlas_file_for_type(int type)
    {
        static constexpr const char* kAtlasByItemType[] = {
            nullptr,
            "01.dds", "02.dds", "03.dds", "04.dds", "05.dds", "06.dds", "07.dds", "08.dds", "09.dds", "10.dds",
            "11.dds", "12.dds", "13.dds", "14.dds", "15.dds", "16.dds", "17.dds", "18.dds", "19.dds", "20.dds",
            "21.dds", "22.dds", "23.dds", "24.dds", "icon_somo.dds", "icon_somo.dds", "icon_somo.dds",
            "icon_somo.dds", "icon_somo.dds", "icon_rapis.dds", "31.dds", "32.dds", "33.dds", "34.dds",
            "35.dds", "36.dds", "37.dds", "icon_somo.dds", "39.dds", "40.dds", "icon_somo.dds",
            "icon_somo.dds", "icon_somo.dds", "icon_somo.dds", "01.dds", "02.dds", "03.dds", "04.dds",
            "05.dds", "05.dds", "06.dds", "06.dds", "07.dds", "07.dds", "08.dds", "08.dds", "09.dds",
            "10.dds", "11.dds", "12.dds", "12.dds", "13.dds", "13.dds", "14.dds", "15.dds", "16.dds",
            "17.dds", "18.dds", "19.dds", "20.dds", "21.dds", "16.dds", "17.dds", "18.dds", nullptr,
            "20.dds", "21.dds", nullptr, nullptr, nullptr, nullptr, "32.dds", "33.dds", "34.dds", "35.dds",
            "36.dds", "31.dds", "32.dds", "33.dds", nullptr, "34.dds", "35.dds", nullptr, "icon_somo.dds",
            "icon_rapis.dds", "23.dds", "40.dds", nullptr, "icon_somo.dds", "icon_somo2.dds",
            "icon_somo2.dds", "icon_somo3.dds", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            "icon_pet.dds", "icon_Wing.dds", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            "icon_DualLayer.dds",
        };

        static_assert(std::size(kAtlasByItemType) == 151);

        if (type <= 0 || type >= static_cast<int>(std::size(kAtlasByItemType)))
            return nullptr;

        return kAtlasByItemType[type];
    }

    bool item_icon_atlas_layout(const std::string& fileName, int& outCols, int& outRows, int& outWidth, int& outHeight)
    {
        auto lowerFileName = game_data::lower_ascii(fileName);
        if (lowerFileName == "icon_rapis.dds")
        {
            outCols = 8;
            outRows = 16;
            outWidth = 256;
            outHeight = 512;
            return true;
        }

        if (lowerFileName == "icon_pet.dds"
            || lowerFileName == "icon_wing.dds"
            || lowerFileName == "icon_duallayer.dds")
        {
            outCols = 16;
            outRows = 8;
            outWidth = 512;
            outHeight = 256;
            return true;
        }

        if (lowerFileName == "icon_somo.dds"
            || lowerFileName == "icon_somo2.dds"
            || lowerFileName == "icon_somo3.dds")
        {
            outCols = 16;
            outRows = 16;
            outWidth = 512;
            outHeight = 512;
            return true;
        }

        if (lowerFileName.size() > 4 && lowerFileName.ends_with(".dds"))
        {
            outCols = 4;
            outRows = 16;
            outWidth = 128;
            outHeight = 512;
            return true;
        }

        return false;
    }

    bool get_item_icon_atlas_config(int type, std::string& outFileName, int& outCols, int& outRows, int& outWidth, int& outHeight)
    {
        auto* fileName = item_icon_atlas_file_for_type(type);
        if (!fileName)
            return false;

        outFileName = fileName;
        return item_icon_atlas_layout(outFileName, outCols, outRows, outWidth, outHeight);
    }

    LPDIRECT3DTEXTURE9 load_item_icon_game_data_texture(const std::string& fileName, int width, int height)
    {
        if (fileName.empty())
            return nullptr;

        ensure_client_icon_atlas_index();
        auto found = g_clientIconAtlasFiles.find(game_data::lower_ascii(fileName));
        if (found != g_clientIconAtlasFiles.end() && found->second.size > 0 && found->second.size <= UINT_MAX)
        {
            std::vector<char> fileData;
            if (game_data::read_saf_file(found->second.offset, found->second.size, fileData))
                return create_texture_from_image_memory(g_device, fileData.data(), static_cast<UINT>(fileData.size()));
        }

        CTexture texture{};
        if (!CTexture::CreateFromFile(&texture, get_native_item_icon_folder(), fileName.c_str(), width, height))
            return nullptr;

        return texture.texture;
    }

    LPDIRECT3DTEXTURE9 get_or_load_item_icon_atlas_texture(const std::string& fileName, int width, int height)
    {
        auto& entry = g_itemIconAtlases[fileName];
        if (entry.fileName.empty())
        {
            entry.fileName = fileName;
            entry.width = width;
            entry.height = height;
        }

        if (!entry.loadAttempted)
        {
            entry.loadAttempted = true;
            entry.texture = load_item_icon_game_data_texture(fileName, width, height);
        }

        return entry.texture;
    }

    void draw_item_icon_at(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, int type, int typeId)
    {
        if (!drawList)
            return;

        auto iconIndex = resolve_item_icon_index(type, typeId);
        std::string atlasFileName;
        int atlasCols = 0;
        int atlasRows = 0;
        int atlasWidth = 0;
        int atlasHeight = 0;
        if (get_item_icon_atlas_config(type, atlasFileName, atlasCols, atlasRows, atlasWidth, atlasHeight))
        {
            if (auto atlasTexture = get_or_load_item_icon_atlas_texture(atlasFileName, atlasWidth, atlasHeight))
            {
                if (iconIndex > 0)
                {
                    auto atlasIndex = iconIndex - 1;
                    if (atlasIndex >= 0 && atlasIndex < atlasCols * atlasRows)
                    {
                        auto col = static_cast<float>(atlasIndex % atlasCols);
                        auto row = static_cast<float>(atlasIndex / atlasCols);
                        ImVec2 uvMin(col / static_cast<float>(atlasCols), row / static_cast<float>(atlasRows));
                        ImVec2 uvMax((col + 1.0f) / static_cast<float>(atlasCols), (row + 1.0f) / static_cast<float>(atlasRows));
                        drawList->AddImage(reinterpret_cast<ImTextureID>(atlasTexture), min, max, uvMin, uvMax);
                        return;
                    }
                }
            }
        }

        drawList->AddRectFilled(min, max, IM_COL32(72, 54, 34, 230), 3.0f);
        drawList->AddRect(min, max, IM_COL32(170, 135, 78, 210), 3.0f);
    }

    void draw_item_count_badge(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, int count)
    {
        if (!drawList || count <= 1)
            return;

        char buffer[16]{};
        std::snprintf(buffer, sizeof(buffer), "%d", count);
        auto textSize = ImGui::CalcTextSize(buffer);
        auto textPos = ImVec2(max.x - textSize.x - 3.0f, max.y - textSize.y - 1.0f);
        drawList->AddText(ImVec2(textPos.x - 1.0f, textPos.y), IM_COL32(0, 0, 0, 210), buffer);
        drawList->AddText(ImVec2(textPos.x + 1.0f, textPos.y), IM_COL32(0, 0, 0, 210), buffer);
        drawList->AddText(ImVec2(textPos.x, textPos.y - 1.0f), IM_COL32(0, 0, 0, 210), buffer);
        drawList->AddText(ImVec2(textPos.x, textPos.y + 1.0f), IM_COL32(0, 0, 0, 210), buffer);
        drawList->AddText(textPos, IM_COL32(255, 244, 210, 255), buffer);
    }

    // DDS header structures (subset needed for DXT-compressed icon atlases)
    #pragma pack(push, 1)
    struct DDS_PIXELFORMAT
    {
        DWORD dwSize;
        DWORD dwFlags;
        DWORD dwFourCC;
        DWORD dwRGBBitCount;
        DWORD dwRBitMask;
        DWORD dwGBitMask;
        DWORD dwBBitMask;
        DWORD dwABitMask;
    };

    struct DDS_HEADER
    {
        DWORD dwMagic;       // 'DDS '
        DWORD dwSize;        // 124
        DWORD dwFlags;
        DWORD dwHeight;
        DWORD dwWidth;
        DWORD dwPitchOrLinearSize;
        DWORD dwDepth;
        DWORD dwMipMapCount;
        DWORD dwReserved1[11];
        DDS_PIXELFORMAT ddspf;
        DWORD dwCaps;
        DWORD dwCaps2;
        DWORD dwCaps3;
        DWORD dwCaps4;
        DWORD dwReserved2;
    };
    #pragma pack(pop)

    constexpr DWORD kDdsMagic = 0x20534444; // 'DDS '
    constexpr DWORD kDdpfFourCC = 0x4;

    static DWORD make_fourcc(char a, char b, char c, char d)
    {
        return static_cast<DWORD>(a) | (static_cast<DWORD>(b) << 8)
            | (static_cast<DWORD>(c) << 16) | (static_cast<DWORD>(d) << 24);
    }

    LPDIRECT3DTEXTURE9 create_texture_from_dds_memory(LPDIRECT3DDEVICE9 device, const void* data, UINT dataSize)
    {
        if (!device || !data || dataSize < sizeof(DDS_HEADER))
            return nullptr;

        auto* header = static_cast<const DDS_HEADER*>(data);
        if (header->dwMagic != kDdsMagic)
            return nullptr;

        auto width = header->dwWidth;
        auto height = header->dwHeight;
        if (width == 0 || height == 0)
            return nullptr;

        D3DFORMAT format = D3DFMT_UNKNOWN;
        UINT blockSize = 16;
        if (header->ddspf.dwFlags & kDdpfFourCC)
        {
            auto fourCC = header->ddspf.dwFourCC;
            if (fourCC == make_fourcc('D', 'X', 'T', '1'))      { format = D3DFMT_DXT1; blockSize = 8; }
            else if (fourCC == make_fourcc('D', 'X', 'T', '3')) { format = D3DFMT_DXT3; }
            else if (fourCC == make_fourcc('D', 'X', 'T', '5')) { format = D3DFMT_DXT5; }
        }

        if (format == D3DFMT_UNKNOWN)
            return nullptr;

        auto pixelDataOffset = static_cast<UINT>(sizeof(DWORD) + header->dwSize);
        if (pixelDataOffset >= dataSize)
            return nullptr;

        auto pixelDataSize = dataSize - pixelDataOffset;
        auto* pixelData = static_cast<const BYTE*>(data) + pixelDataOffset;

        // Compute expected size for mip level 0
        auto blocksWide = (width + 3) / 4;
        auto blocksHigh = (height + 3) / 4;
        auto expectedSize = blocksWide * blocksHigh * blockSize;
        if (pixelDataSize < expectedSize)
            return nullptr;

        LPDIRECT3DTEXTURE9 texture = nullptr;
        if (FAILED(device->CreateTexture(width, height, 1, 0, format, D3DPOOL_MANAGED, &texture, nullptr)) || !texture)
            return nullptr;

        D3DLOCKED_RECT locked{};
        if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
        {
            texture->Release();
            return nullptr;
        }

        // For block-compressed formats, copy row-of-blocks at a time
        auto srcPitch = blocksWide * blockSize;
        for (UINT row = 0; row < blocksHigh; ++row)
        {
            auto* dst = static_cast<BYTE*>(locked.pBits) + row * locked.Pitch;
            auto* src = pixelData + row * srcPitch;
            std::memcpy(dst, src, srcPitch);
        }

        texture->UnlockRect(0);
        return texture;
    }

    LPDIRECT3DTEXTURE9 create_texture_from_png_memory(LPDIRECT3DDEVICE9 device, const void* data, UINT dataSize)
    {
        if (!device || !data || dataSize == 0)
            return nullptr;

        int width = 0, height = 0, channels = 0;
        auto* pixels = stbi_load_from_memory(
            static_cast<const stbi_uc*>(data),
            static_cast<int>(dataSize),
            &width, &height, &channels, 4); // force RGBA
        if (!pixels)
            return nullptr;

        LPDIRECT3DTEXTURE9 texture = nullptr;
        if (FAILED(device->CreateTexture(
            static_cast<UINT>(width), static_cast<UINT>(height),
            1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)) || !texture)
        {
            stbi_image_free(pixels);
            return nullptr;
        }

        D3DLOCKED_RECT locked{};
        if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
        {
            texture->Release();
            stbi_image_free(pixels);
            return nullptr;
        }

        // stb_image outputs RGBA, D3D9 expects BGRA (A8R8G8B8) — swizzle R<->B
        for (int y = 0; y < height; ++y)
        {
            auto* src = pixels + y * width * 4;
            auto* dst = static_cast<BYTE*>(locked.pBits) + y * locked.Pitch;
            for (int x = 0; x < width; ++x)
            {
                dst[x * 4 + 0] = src[x * 4 + 2]; // B
                dst[x * 4 + 1] = src[x * 4 + 1]; // G
                dst[x * 4 + 2] = src[x * 4 + 0]; // R
                dst[x * 4 + 3] = src[x * 4 + 3]; // A
            }
        }

        texture->UnlockRect(0);
        stbi_image_free(pixels);
        return texture;
    }

    // Auto-detect format by header and load accordingly (replaces D3DXCreateTextureFromFileInMemory)
    LPDIRECT3DTEXTURE9 create_texture_from_image_memory(LPDIRECT3DDEVICE9 device, const void* data, UINT dataSize)
    {
        if (!device || !data || dataSize < 4)
            return nullptr;

        auto magic = *static_cast<const DWORD*>(data);

        // DDS: 'DDS ' magic
        if (magic == kDdsMagic)
            return create_texture_from_dds_memory(device, data, dataSize);

        // PNG / JPEG / BMP / TGA — anything stb_image supports
        return create_texture_from_png_memory(device, data, dataSize);
    }

    void release_item_icon_textures()
    {
        for (auto& [_, entry] : g_itemIconAtlases)
        {
            if (entry.texture)
            {
                entry.texture->Release();
                entry.texture = nullptr;
            }
            entry.loadAttempted = false;
        }
    }

} // namespace imgui_layer
