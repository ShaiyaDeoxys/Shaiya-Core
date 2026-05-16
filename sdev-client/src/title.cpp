#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <memory>
#include <algorithm>
using std::max;
using std::min;
#include <cctype>
#include <cstdint>
#include <cstring>
#include <windows.h>
#include <gdiplus.h>
#include <objidl.h>
#include <external/stb/stb_image.h>
#include <util/util.h>
#include "include/main.h"
#include "include/shaiya/CCharacter.h"
#include "include/shaiya/CDataFile.h"
#include "include/shaiya/CMonster.h"
#include "include/shaiya/CStaticText.h"
#include "include/shaiya/CTexture.h"
#include "include/shaiya/ItemInfo.h"
#include "include/shaiya/HexColor.h"
#include "include/shaiya/Static.h"
using namespace shaiya;
#pragma comment(lib, "gdiplus.lib")

namespace title
{
    using ItemId = uint32_t;

    constexpr float chat_y_add = 1.75F;
    constexpr uint16_t kRainbowTitleRange = 11;
    constexpr uint8_t kTextTitleReqIg = 1;
    constexpr uint8_t kPngTitleReqIg = 2;
    constexpr uint8_t kGifTitleReqIg = 3;
    constexpr float kMaxVisualTitleWidth = 180.0f;
    constexpr float kMaxVisualTitleHeight = 48.0f;
    constexpr const char* kTitleSahFolder         = "assets\\titles";
    constexpr const char* kTitleAnimatedSahFolder = "assets\\titlesanimated";
    constexpr PROPID kGdiplusFrameDelayProperty = 0x5100;
    std::unordered_map<CCharacter*, ItemId> userTitleItemIds;

    enum class VisualTitleKind
    {
        Png,
        Gif
    };

    struct VisualTitleFrame
    {
        CTexture texture{};
        DWORD delayMs = 100;
    };

    struct VisualTitleAsset
    {
        VisualTitleKind kind{};
        uint16_t index = 0;
        uint64_t dataOffset = 0;
        uint64_t dataSize = 0;
        CTexture texture{};
        std::vector<VisualTitleFrame> frames;
        bool loadAttempted = false;
    };

    std::unordered_map<uint32_t, VisualTitleAsset> visualTitleAssets;
    bool visualTitleCatalogLoaded = false;
    ULONG_PTR gdiplusToken = 0;
    bool gdiplusStartAttempted = false;

    D3DCOLOR make_rgb_color(uint8_t red, uint8_t green, uint8_t blue)
    {
        return 0xFF000000
            | (static_cast<D3DCOLOR>(red) << 16)
            | (static_cast<D3DCOLOR>(green) << 8)
            | static_cast<D3DCOLOR>(blue);
    }

    D3DCOLOR get_rainbow_title_color()
    {
        auto phase = (GetTickCount() / 450) % 6;
        auto step = static_cast<uint8_t>((GetTickCount() % 450) * 255 / 450);
        auto inverse = static_cast<uint8_t>(255 - step);

        switch (phase)
        {
        case 0:
            return make_rgb_color(255, step, 0);
        case 1:
            return make_rgb_color(inverse, 255, 0);
        case 2:
            return make_rgb_color(0, 255, step);
        case 3:
            return make_rgb_color(0, inverse, 255);
        case 4:
            return make_rgb_color(step, 0, 255);
        default:
            return make_rgb_color(255, 0, inverse);
        }
    }

    bool is_cloak_item(const ItemInfo* itemInfo)
    {
        if (!itemInfo)
            return false;

        return itemInfo->type == std::to_underlying(RealType::Cloak)
            || itemInfo->type == std::to_underlying(RealType::FuryCloak);
    }

    bool is_title_cloak(const ItemInfo* itemInfo)
    {
        if (!is_cloak_item(itemInfo))
            return false;

        return itemInfo->reqIg == kTextTitleReqIg
            || itemInfo->reqIg == kPngTitleReqIg
            || itemInfo->reqIg == kGifTitleReqIg;
    }

    D3DCOLOR get_title_color(uint16_t range)
    {
        switch (range)
        {
        case 1:
            return std::to_underlying(HexColor::Red);
        case 2:
            return std::to_underlying(HexColor::DodgerBlue);
        case 3:
            return std::to_underlying(HexColor::Green);
        case 4:
            return std::to_underlying(HexColor::Yellow);
        case 5:
            return std::to_underlying(HexColor::Orange);
        case 6:
            return std::to_underlying(HexColor::Purple);
        case 7:
            return std::to_underlying(HexColor::Pink);
        case 8:
            return std::to_underlying(HexColor::Cyan);
        case 9:
            return std::to_underlying(HexColor::Gold);
        case 10:
            return std::to_underlying(HexColor::Silver);
        case kRainbowTitleRange:
            return get_rainbow_title_color();
        default:
            return std::to_underlying(HexColor::White);
        }
    }

    std::string get_game_relative_path(const char* relativePath)
    {
        char modulePath[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, modulePath, sizeof(modulePath)) == 0)
            return relativePath ? relativePath : "";

        auto path = std::string(modulePath);
        auto slash = path.find_last_of("\\/");
        if (slash == std::string::npos)
            return relativePath ? relativePath : "";

        path.resize(slash + 1);
        if (relativePath)
            path += relativePath;

        return path;
    }

    std::string to_lower_ascii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    bool read_sah_u32(const std::vector<char>& data, std::size_t& offset, uint32_t& value)
    {
        if (offset + sizeof(value) > data.size())
            return false;

        std::memcpy(&value, data.data() + offset, sizeof(value));
        offset += sizeof(value);
        return true;
    }

    bool read_sah_u64(const std::vector<char>& data, std::size_t& offset, uint64_t& value)
    {
        if (offset + sizeof(value) > data.size())
            return false;

        std::memcpy(&value, data.data() + offset, sizeof(value));
        offset += sizeof(value);
        return true;
    }

    bool read_sah_string(const std::vector<char>& data, std::size_t& offset, std::string& value)
    {
        uint32_t length = 0;
        if (!read_sah_u32(data, offset, length) || offset + length > data.size())
            return false;

        value.assign(data.data() + offset, data.data() + offset + length);
        while (!value.empty() && value.back() == '\0')
            value.pop_back();

        offset += length;
        return true;
    }

    bool parse_visual_title_file_name(const char* fileName, const char* extension, uint16_t& index)
    {
        constexpr const char* kPrefix = "title";
        auto prefixLength = std::strlen(kPrefix);
        if (!fileName || _strnicmp(fileName, kPrefix, prefixLength) != 0)
            return false;

        auto* current = fileName + prefixLength;
        if (*current < '1' || *current > '9')
            return false;

        auto parsed = 0;
        while (*current >= '0' && *current <= '9')
        {
            parsed = parsed * 10 + (*current - '0');
            ++current;
        }

        if (parsed <= 0 || parsed > UINT16_MAX || _stricmp(current, extension) != 0)
            return false;

        index = static_cast<uint16_t>(parsed);
        return true;
    }

    uint32_t visual_title_key(VisualTitleKind kind, uint16_t index)
    {
        return (static_cast<uint32_t>(kind) << 16) | index;
    }

    bool is_sah_visual_title_folder(const std::string& lowerPath, VisualTitleKind kind)
    {
        auto folder = kind == VisualTitleKind::Gif ? kTitleAnimatedSahFolder : kTitleSahFolder;
        return lowerPath == folder || lowerPath == std::string("data\\") + folder;
    }

    void try_add_visual_title_from_sah_file(VisualTitleKind kind, const std::string& fileName, uint64_t fileOffset, uint64_t fileSize)
    {
        uint16_t index = 0;
        auto extension = kind == VisualTitleKind::Gif ? ".gif" : ".png";
        if (!parse_visual_title_file_name(fileName.c_str(), extension, index))
            return;

        auto key = visual_title_key(kind, index);
        if (visualTitleAssets.find(key) != visualTitleAssets.end())
            return;

        visualTitleAssets.emplace(key, VisualTitleAsset{ kind, index, fileOffset, fileSize });
    }

    bool scan_sah_directory_for_visual_titles(const std::vector<char>& data, std::size_t& offset, const std::string& parentPath)
    {
        std::string name;
        if (!read_sah_string(data, offset, name))
            return false;

        auto path = parentPath;
        if (!name.empty())
            path = path.empty() ? name : path + "\\" + name;

        uint32_t fileCount = 0;
        if (!read_sah_u32(data, offset, fileCount))
            return false;

        auto lowerPath = to_lower_ascii(path);
        for (uint32_t i = 0; i < fileCount; ++i)
        {
            std::string fileName;
            uint64_t fileOffset = 0;
            uint64_t fileSize = 0;
            if (!read_sah_string(data, offset, fileName)
                || !read_sah_u64(data, offset, fileOffset)
                || !read_sah_u64(data, offset, fileSize))
            {
                return false;
            }

            if (is_sah_visual_title_folder(lowerPath, VisualTitleKind::Png))
                try_add_visual_title_from_sah_file(VisualTitleKind::Png, fileName, fileOffset, fileSize);
            else if (is_sah_visual_title_folder(lowerPath, VisualTitleKind::Gif))
                try_add_visual_title_from_sah_file(VisualTitleKind::Gif, fileName, fileOffset, fileSize);
        }

        uint32_t directoryCount = 0;
        if (!read_sah_u32(data, offset, directoryCount))
            return false;

        for (uint32_t i = 0; i < directoryCount; ++i)
        {
            if (!scan_sah_directory_for_visual_titles(data, offset, path))
                return false;
        }

        return true;
    }

    void ensure_visual_title_catalog_loaded()
    {
        if (visualTitleCatalogLoaded)
            return;

        visualTitleCatalogLoaded = true;
        auto sahPath = get_game_relative_path("data.sah");
        std::ifstream stream(sahPath, std::ios::binary);
        if (!stream)
            return;

        std::vector<char> data(
            (std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>());
        if (data.size() <= 0x34 || std::memcmp(data.data(), "SAH", 3) != 0)
            return;

        auto offset = std::size_t{ 0x34 };
        scan_sah_directory_for_visual_titles(data, offset, "");
    }

    bool read_client_data_file(const VisualTitleAsset& asset, std::vector<char>& fileData)
    {
        if (asset.dataSize == 0 || asset.dataSize > UINT_MAX)
            return false;

        auto safPath = get_game_relative_path("data.saf");
        std::ifstream stream(safPath, std::ios::binary);
        if (!stream)
            return false;

        stream.seekg(static_cast<std::streamoff>(asset.dataOffset), std::ios::beg);
        if (!stream)
            return false;

        fileData.assign(static_cast<std::size_t>(asset.dataSize), 0);
        stream.read(fileData.data(), static_cast<std::streamsize>(fileData.size()));
        return stream.gcount() == static_cast<std::streamsize>(fileData.size());
    }

    bool assign_texture_size(CTexture& texture)
    {
        if (!texture.texture)
            return false;

        D3DSURFACE_DESC desc{};
        if (FAILED(texture.texture->GetLevelDesc(0, &desc)))
            return false;

        texture.size.width = static_cast<float>(desc.Width);
        texture.size.height = static_cast<float>(desc.Height);
        return true;
    }

    bool load_png_title_texture(VisualTitleAsset& asset)
    {
        auto device = g_var->camera.device;
        if (!device)
            return false;

        std::vector<char> fileData;
        if (!read_client_data_file(asset, fileData))
            return false;

        int width = 0, height = 0, channels = 0;
        auto* pixels = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(fileData.data()),
            static_cast<int>(fileData.size()),
            &width, &height, &channels, 4);
        if (!pixels)
            return false;

        LPDIRECT3DTEXTURE9 texture = nullptr;
        if (FAILED(device->CreateTexture(
            static_cast<UINT>(width), static_cast<UINT>(height),
            1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)) || !texture)
        {
            stbi_image_free(pixels);
            return false;
        }

        D3DLOCKED_RECT locked{};
        if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
        {
            texture->Release();
            stbi_image_free(pixels);
            return false;
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

        asset.texture.texture = texture;
        return assign_texture_size(asset.texture);
    }

    bool ensure_gdiplus_started()
    {
        if (gdiplusToken)
            return true;

        if (gdiplusStartAttempted)
            return false;

        gdiplusStartAttempted = true;
        Gdiplus::GdiplusStartupInput input;
        return Gdiplus::GdiplusStartup(&gdiplusToken, &input, nullptr) == Gdiplus::Ok;
    }

    DWORD read_gif_frame_delay_ms(Gdiplus::Bitmap& bitmap, UINT frameIndex)
    {
        auto propertySize = bitmap.GetPropertyItemSize(kGdiplusFrameDelayProperty);
        if (propertySize == 0)
            return 100;

        std::vector<BYTE> propertyData(propertySize);
        auto* property = reinterpret_cast<Gdiplus::PropertyItem*>(propertyData.data());
        if (bitmap.GetPropertyItem(kGdiplusFrameDelayProperty, propertySize, property) != Gdiplus::Ok)
            return 100;

        if (property->type != PropertyTagTypeLong || property->length < sizeof(UINT) * (frameIndex + 1))
            return 100;

        auto delays = static_cast<UINT*>(property->value);
        auto delayMs = static_cast<DWORD>(delays[frameIndex]) * 10;
        return delayMs >= 20 ? delayMs : 100;
    }

    LPDIRECT3DTEXTURE9 create_texture_from_argb_bitmap(Gdiplus::Bitmap& bitmap)
    {
        auto device = g_var->camera.device;
        if (!device)
            return nullptr;

        auto width = bitmap.GetWidth();
        auto height = bitmap.GetHeight();
        if (width == 0 || height == 0)
            return nullptr;

        Gdiplus::Rect rect(0, 0, static_cast<INT>(width), static_cast<INT>(height));
        Gdiplus::BitmapData bitmapData{};
        if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) != Gdiplus::Ok)
            return nullptr;

        LPDIRECT3DTEXTURE9 texture = nullptr;
        if (FAILED(device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)) || !texture)
        {
            bitmap.UnlockBits(&bitmapData);
            return nullptr;
        }

        D3DLOCKED_RECT locked{};
        if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
        {
            texture->Release();
            bitmap.UnlockBits(&bitmapData);
            return nullptr;
        }

        auto* sourceBase = static_cast<BYTE*>(bitmapData.Scan0);
        auto sourceStride = bitmapData.Stride;
        for (UINT y = 0; y < height; ++y)
        {
            auto* source = sourceStride >= 0
                ? sourceBase + y * sourceStride
                : sourceBase + (height - 1 - y) * (-sourceStride);
            auto* destination = static_cast<BYTE*>(locked.pBits) + y * locked.Pitch;
            std::memcpy(destination, source, width * 4);
        }

        texture->UnlockRect(0);
        bitmap.UnlockBits(&bitmapData);
        return texture;
    }

    bool load_gif_title_frames(VisualTitleAsset& asset)
    {
        if (!ensure_gdiplus_started())
            return false;

        std::vector<char> fileData;
        if (!read_client_data_file(asset, fileData) || fileData.empty())
            return false;

        auto memory = GlobalAlloc(GMEM_MOVEABLE, fileData.size());
        if (!memory)
            return false;

        auto* destination = GlobalLock(memory);
        if (!destination)
        {
            GlobalFree(memory);
            return false;
        }
        std::memcpy(destination, fileData.data(), fileData.size());
        GlobalUnlock(memory);

        IStream* stream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream)) || !stream)
        {
            GlobalFree(memory);
            return false;
        }

        std::unique_ptr<Gdiplus::Bitmap> bitmap(Gdiplus::Bitmap::FromStream(stream));
        if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok)
        {
            bitmap.reset();
            stream->Release();
            return false;
        }

        auto dimensionCount = bitmap->GetFrameDimensionsCount();
        if (dimensionCount == 0)
        {
            bitmap.reset();
            stream->Release();
            return false;
        }

        std::vector<GUID> dimensions(dimensionCount);
        if (bitmap->GetFrameDimensionsList(dimensions.data(), dimensionCount) != Gdiplus::Ok)
        {
            bitmap.reset();
            stream->Release();
            return false;
        }

        auto frameCount = bitmap->GetFrameCount(&dimensions[0]);
        if (frameCount == 0)
        {
            bitmap.reset();
            stream->Release();
            return false;
        }

        for (UINT i = 0; i < frameCount; ++i)
        {
            if (bitmap->SelectActiveFrame(&dimensions[0], i) != Gdiplus::Ok)
                continue;

            VisualTitleFrame frame{};
            frame.texture.texture = create_texture_from_argb_bitmap(*bitmap);
            if (!frame.texture.texture || !assign_texture_size(frame.texture))
                continue;

            frame.delayMs = read_gif_frame_delay_ms(*bitmap, i);
            asset.frames.push_back(frame);
        }

        bitmap.reset();
        stream->Release();
        return !asset.frames.empty();
    }

    VisualTitleAsset* get_visual_title_asset(VisualTitleKind kind, uint16_t index)
    {
        if (index == 0)
            return nullptr;

        ensure_visual_title_catalog_loaded();
        auto it = visualTitleAssets.find(visual_title_key(kind, index));
        if (it == visualTitleAssets.end())
            return nullptr;

        auto& asset = it->second;
        if (!asset.loadAttempted)
        {
            if (asset.kind == VisualTitleKind::Gif)
                load_gif_title_frames(asset);
            else
                load_png_title_texture(asset);
            asset.loadAttempted = true;
        }

        return &asset;
    }

    CTexture* get_visual_title_texture(VisualTitleAsset& asset)
    {
        if (asset.kind == VisualTitleKind::Png)
            return asset.texture.texture ? &asset.texture : nullptr;

        if (asset.frames.empty())
            return nullptr;

        auto totalDelay = DWORD{ 0 };
        for (auto& frame : asset.frames)
            totalDelay += frame.delayMs;

        if (totalDelay == 0)
            return &asset.frames.front().texture;

        auto frameTick = GetTickCount() % totalDelay;
        auto elapsed = DWORD{ 0 };
        for (auto& frame : asset.frames)
        {
            elapsed += frame.delayMs;
            if (frameTick < elapsed)
                return &frame.texture;
        }

        return &asset.frames.back().texture;
    }

    struct ScreenTextureVertex
    {
        float x;
        float y;
        float z;
        float rhw;
        D3DCOLOR color;
        float u;
        float v;
    };

    bool draw_screen_texture(CTexture* texture, float x, float y, float width, float height)
    {
        auto device = g_var->camera.device;
        if (!device || !texture || !texture->texture || width <= 0.0f || height <= 0.0f)
            return false;

        DWORD oldFvf = 0;
        LPDIRECT3DTEXTURE9 oldTexture = nullptr;
        DWORD oldAlphaBlend = 0;
        DWORD oldSrcBlend = 0;
        DWORD oldDestBlend = 0;
        DWORD oldLighting = 0;
        DWORD oldCullMode = 0;
        DWORD oldZEnable = 0;
        DWORD oldSamplerAddressU = 0;
        DWORD oldSamplerAddressV = 0;

        device->GetFVF(&oldFvf);
        device->GetTexture(0, reinterpret_cast<IDirect3DBaseTexture9**>(&oldTexture));
        device->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
        device->GetRenderState(D3DRS_SRCBLEND, &oldSrcBlend);
        device->GetRenderState(D3DRS_DESTBLEND, &oldDestBlend);
        device->GetRenderState(D3DRS_LIGHTING, &oldLighting);
        device->GetRenderState(D3DRS_CULLMODE, &oldCullMode);
        device->GetRenderState(D3DRS_ZENABLE, &oldZEnable);
        device->GetSamplerState(0, D3DSAMP_ADDRESSU, &oldSamplerAddressU);
        device->GetSamplerState(0, D3DSAMP_ADDRESSV, &oldSamplerAddressV);

        ScreenTextureVertex vertices[] = {
            { x - 0.5f,         y - 0.5f,          0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 0.0f },
            { x + width - 0.5f, y - 0.5f,          0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f },
            { x - 0.5f,         y + height - 0.5f, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f },
            { x + width - 0.5f, y + height - 0.5f, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 1.0f },
        };

        constexpr DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
        device->SetTexture(0, texture->texture);
        device->SetFVF(kFvf);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        device->SetRenderState(D3DRS_LIGHTING, FALSE);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_ZENABLE, FALSE);
        device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        auto result = device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(ScreenTextureVertex));

        device->SetSamplerState(0, D3DSAMP_ADDRESSU, oldSamplerAddressU);
        device->SetSamplerState(0, D3DSAMP_ADDRESSV, oldSamplerAddressV);
        device->SetRenderState(D3DRS_ZENABLE, oldZEnable);
        device->SetRenderState(D3DRS_CULLMODE, oldCullMode);
        device->SetRenderState(D3DRS_LIGHTING, oldLighting);
        device->SetRenderState(D3DRS_DESTBLEND, oldDestBlend);
        device->SetRenderState(D3DRS_SRCBLEND, oldSrcBlend);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlphaBlend);
        device->SetFVF(oldFvf);
        device->SetTexture(0, oldTexture);
        if (oldTexture)
            oldTexture->Release();

        return SUCCEEDED(result);
    }

    void reset(CCharacter* user)
    {
        userTitleItemIds.erase(user);

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

    bool ensure_text(CCharacter* user, ItemId itemId, const std::string& text)
    {
        auto cachedItemId = userTitleItemIds.find(user);
        if (user->title.text && cachedItemId != userTitleItemIds.end() && cachedItemId->second == itemId)
            return true;

        reset(user);

        user->title.text = CStaticText::Create(text.c_str());
        if (!user->title.text)
            return false;

        auto w = CStaticText::GetTextWidth(text.c_str());
        user->title.pointX = static_cast<int>(w * 0.5);
        userTitleItemIds[user] = itemId;
        return true;
    }

    void draw_visual_title(CCharacter* user, ItemId itemId, VisualTitleKind kind, uint16_t index, float x, float y, float extrusion)
    {
        auto asset = get_visual_title_asset(kind, index);
        if (!asset)
        {
            reset(user);
            return;
        }

        auto texture = get_visual_title_texture(*asset);
        if (!texture || !texture->texture)
        {
            reset(user);
            return;
        }

        if (user->title.text)
            reset(user);

        userTitleItemIds[user] = itemId;

        auto sourceWidth = texture->size.width;
        auto sourceHeight = texture->size.height;
        if (sourceWidth <= 0.0f || sourceHeight <= 0.0f)
            return;

        auto scale = std::min(kMaxVisualTitleWidth / sourceWidth, kMaxVisualTitleHeight / sourceHeight);
        scale = std::min(scale, 1.0f);

        auto width = static_cast<int>(sourceWidth * scale);
        auto height = static_cast<int>(sourceHeight * scale);
        auto posX = static_cast<int>(x - (static_cast<float>(width) * 0.5f));
        auto posY = static_cast<int>(y - 30.0f - static_cast<float>(height));

        draw_screen_texture(texture, static_cast<float>(posX), static_cast<float>(posY), static_cast<float>(width), static_cast<float>(height));
    }

    void hook(CCharacter* user, float x, float y, float extrusion)
    {
        if (!g_showTitles)
        {
            reset(user);
            return;
        }

        auto cloakType = user->equipment.type[ItemSlot::Cloak];
        auto cloakTypeId = user->equipment.typeId[ItemSlot::Cloak];

        if (!cloakType)
        {
            reset(user);
            return;
        }

        auto itemInfo = CDataFile::GetItemInfo(cloakType, cloakTypeId);
        if (!itemInfo)
        {
            reset(user);
            return;
        }

        if (!is_title_cloak(itemInfo))
        {
            reset(user);
            return;
        }

        auto itemId = (static_cast<ItemId>(itemInfo->type) * 1000) + itemInfo->typeId;

        if (itemInfo->reqIg == kPngTitleReqIg)
        {
            draw_visual_title(user, itemId, VisualTitleKind::Png, itemInfo->range, x, y, extrusion);
            return;
        }

        if (itemInfo->reqIg == kGifTitleReqIg)
        {
            draw_visual_title(user, itemId, VisualTitleKind::Gif, itemInfo->range, x, y, extrusion);
            return;
        }

        if (!itemInfo->name || !itemInfo->name[0])
        {
            reset(user);
            return;
        }

        std::string text(itemInfo->name);
        auto color = get_title_color(itemInfo->range);

        if (!ensure_text(user, itemId, text))
            return;

        auto posY = static_cast<int>(y - 30.0);
        auto posX = static_cast<int>(x - user->title.pointX);

        CStaticText::Draw(user->title.text, posX, posY, extrusion, color);
    }
}

unsigned u0x453E81 = 0x453E81;
void __declspec(naked) naked_0x453E7C()
{
    __asm
    {
        pushad
        pushfd

        sub esp,0xC
        fld dword ptr[esp+0x4C]
        fstp dword ptr[esp+0x8]

        fld dword ptr[esp+0x48]
        fstp dword ptr[esp+0x4]

        fld dword ptr[esp+0x44]
        fstp dword ptr[esp]

        push esi // user
        call title::hook
        add esp,0x10

        popfd
        popad

        // original
        mov eax,dword ptr ds:[0x22B69A8]
        jmp u0x453E81
    }
}

unsigned n0x4184CF = 0x4184CF;
unsigned u0x418312 = 0x418312;
void __declspec(naked) naked_0x41830D()
{
    __asm 
    {
        // monster->model
        cmp dword ptr[eax+0x74],0x0
        je _0x4184CF
        
        // original
        cmp dword ptr[esp+0x38],0x0
        jmp u0x418312

        _0x4184CF:
        jmp n0x4184CF
    }
}

unsigned u0x412765 = 0x412765;
void __declspec(naked) naked_0x41275F()
{
    __asm
    {
        fld dword ptr[title::chat_y_add]
        jmp u0x412765
    }
}

unsigned u0x59F0C8 = 0x59F0C8;
void __declspec(naked) naked_0x59F0C3()
{
    __asm
    {
        pushad

        push esi // user
        call title::reset
        add esp,0x4

        popad

        // original 
        cmp byte ptr[esp+0x14],0x0
        jmp u0x59F0C8
    }
}

void hook::title()
{
    util::detour((void*)0x453E7C, naked_0x453E7C, 5);
    // hide pets without a model
    util::detour((void*)0x41830D, naked_0x41830D, 5);
    // increase chat balloon height (1.5 to 1.75)
    util::detour((void*)0x41275F, naked_0x41275F, 6);
    // 0x507 packet method
    util::detour((void*)0x59F0C3, naked_0x59F0C3, 5);
}
