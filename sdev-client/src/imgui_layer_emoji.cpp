#include "include/imgui_layer_internal.h"
#include <external/stb/stb_image.h>
#pragma comment(lib, "gdiplus.lib")

#include "include/debug_panel.h"
#include <util/util.h>

void naked_chat_add_token_filter();
void naked_chat_balloon_text_create();
void naked_capture_chat_balloon_text();
void naked_floating_text_create();
void naked_capture_floating_static_text();
void naked_static_text_create();
void naked_floating_static_text_draw();
void naked_native_text_draw_probe();
namespace imgui_layer
{
    bool is_emoji_transition_grace_active()
    {
        return g_emojiMapGraceUntilTick != 0
            && static_cast<int32_t>(GetTickCount() - g_emojiMapGraceUntilTick) < 0;
    }

    void clear_emoji_text_overlays()
    {
        {
            std::lock_guard<std::mutex> lock(g_chatEmojiMutex);
            g_lowerChatEmojiLines.clear();
        }

        {
            std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
            g_hasPendingFloatingEmojiLine = false;
            g_pendingFloatingEmojiLine = {};
            g_floatingEmojiLines.clear();
            g_floatingEmojiRenders.clear();
        }
    }

    void sync_emoji_overlay_scene_state()
    {
        auto now = GetTickCount();
        auto gameScene = is_game_scene();
        auto charId = g_pPlayerData ? g_pPlayerData->charId : uint32_t{ 0 };
        auto mapId = gameScene ? g_pPlayerData->mapId : uint16_t{ 0 };
        auto* user = gameScene ? g_pWorldMgr->user : nullptr;

        if (!g_emojiSceneStateInitialized)
        {
            g_emojiSceneStateInitialized = true;
            g_lastEmojiGameScene = gameScene;
            g_lastEmojiCharId = charId;
            g_lastEmojiMapId = mapId;
            g_lastEmojiUser = user;
            return;
        }

        if (!gameScene)
        {
            if (g_lastEmojiGameScene && g_emojiSceneLostTick == 0)
            {
                g_emojiSceneLostTick = now;
                g_emojiMapGraceUntilTick = now + kEmojiSceneGraceMs;
                g_emojiSceneTransitionUntilTick = now + kEmojiSceneGraceMs;
                g_showEmojiPicker = false;
                clear_emoji_text_overlays();
            }

            if (g_lastEmojiGameScene && now - g_emojiSceneLostTick < kEmojiSceneGraceMs)
                return;

            if (g_lastEmojiGameScene || g_lastEmojiCharId != 0)
                clear_emoji_text_overlays();

            g_lastEmojiGameScene = false;
            g_lastEmojiCharId = 0;
            g_lastEmojiMapId = 0;
            g_lastEmojiUser = nullptr;
            return;
        }

        g_emojiSceneLostTick = 0;

        if ((g_lastEmojiCharId != 0 && g_lastEmojiCharId != charId)
            || (!g_lastEmojiGameScene && g_lastEmojiCharId == 0))
        {
            clear_emoji_text_overlays();
        }

        if (mapId != 0 && g_lastEmojiMapId != 0 && mapId != g_lastEmojiMapId)
        {
            g_lastEmojiMapId = mapId;
            g_emojiMapGraceUntilTick = now + kEmojiMapChangeGraceMs;
            g_emojiSceneTransitionUntilTick = now + kEmojiMapChangeGraceMs;
            g_showEmojiPicker = false;
            clear_emoji_text_overlays();
        }
        else
        {
            g_lastEmojiMapId = mapId;
        }

        if (!g_lastEmojiGameScene || g_lastEmojiUser != user)
            g_emojiSceneTransitionUntilTick = now + 1000;

        g_lastEmojiGameScene = true;
        g_lastEmojiCharId = charId;
        g_lastEmojiUser = user;
    }

    void handle_emoji_button_interaction()
    {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            g_showEmojiPicker = !g_showEmojiPicker;
    }

    const std::string& get_game_base_dir()
    {
        static std::string baseDir = []() -> std::string {
            char modulePath[MAX_PATH]{};
            if (GetModuleFileNameA(nullptr, modulePath, sizeof(modulePath)) == 0)
                return ".\\";
            std::string p(modulePath);
            auto slash = p.find_last_of("\\/");
            if (slash == std::string::npos)
                return ".\\";
            p.resize(slash + 1);
            return p;
        }();
        return baseDir;
    }

    std::string get_game_relative_path(const char* relativePath)
    {
        auto path = get_game_base_dir();
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

    bool parse_visual_token_file_name(const char* fileName, const char* prefix, const char* extension, int& index)
    {
        auto prefixLength = std::strlen(prefix);
        if (!fileName || _strnicmp(fileName, prefix, prefixLength) != 0)
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

        if (parsed <= 0 || _stricmp(current, extension) != 0)
            return false;

        index = parsed;
        return true;
    }

    const char* visual_token_prefix(VisualTokenKind kind)
    {
        return kind == VisualTokenKind::Gif ? "gif" : "emoji";
    }

    bool is_visual_token_enabled(VisualTokenKind kind)
    {
        return kind == VisualTokenKind::Gif ? g_gifsEnabled : g_emojisEnabled;
    }

    bool match_visual_token_text(const char* text, VisualTokenKind& kind, std::size_t& tokenLength)
    {
        if (!text || text[0] != ':')
            return false;

        struct TokenPattern
        {
            VisualTokenKind kind;
            const char* prefix;
        };

        constexpr TokenPattern kPatterns[] = {
            { VisualTokenKind::Emoji, "emoji" },
            { VisualTokenKind::Gif, "gif" }
        };

        for (auto& pattern : kPatterns)
        {
            auto prefixLength = std::strlen(pattern.prefix);
            if (_strnicmp(text + 1, pattern.prefix, prefixLength) != 0)
                continue;

            auto* current = text + 1 + prefixLength;
            if (*current < '1' || *current > '9')
                continue;

            do
            {
                ++current;
            } while (*current >= '0' && *current <= '9');

            if (*current != ':')
                continue;

            kind = pattern.kind;
            tokenLength = static_cast<std::size_t>(current - text + 1);
            return true;
        }

        return false;
    }

    bool has_visual_token_index(VisualTokenKind kind, int index)
    {
        return std::find_if(g_emojis.begin(), g_emojis.end(), [kind, index](const EmojiEntry& emoji) {
            return emoji.kind == kind && emoji.index == index;
        }) != g_emojis.end();
    }

    bool is_sah_visual_token_folder(const std::string& lowerPath, VisualTokenKind kind)
    {
        auto folder = kind == VisualTokenKind::Gif ? kGifSahFolder : kEmojiSahFolder;
        return lowerPath == folder || lowerPath == std::string("data\\") + folder;
    }

    bool is_sah_general_folder(const std::string& lowerPath)
    {
        return lowerPath == kGeneralSahFolder
            || lowerPath == std::string("data\\") + kGeneralSahFolder;
    }

    bool try_add_visual_token_from_sah_file(VisualTokenKind kind, const std::string& fileName, uint64_t fileOffset, uint64_t fileSize)
    {
        auto index = 0;
        auto prefix = visual_token_prefix(kind);
        auto extension = kind == VisualTokenKind::Gif ? ".gif" : ".png";
        if (!parse_visual_token_file_name(fileName.c_str(), prefix, extension, index) || has_visual_token_index(kind, index))
            return false;

        char token[32]{};
        std::snprintf(token, sizeof(token), ":%s%d:", prefix, index);
        g_emojis.push_back({ kind, index, token, fileName, fileOffset, fileSize, nullptr, nullptr, {}, false, false, 0 });
        return true;
    }

    bool scan_sah_directory_for_visual_tokens(const std::vector<char>& data, std::size_t& offset, const std::string& parentPath)
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

            if (is_sah_visual_token_folder(lowerPath, VisualTokenKind::Emoji))
            {
                try_add_visual_token_from_sah_file(VisualTokenKind::Emoji, fileName, fileOffset, fileSize);
            }
            else if (is_sah_visual_token_folder(lowerPath, VisualTokenKind::Gif))
            {
                try_add_visual_token_from_sah_file(VisualTokenKind::Gif, fileName, fileOffset, fileSize);
            }
            else if (is_sah_general_folder(lowerPath))
            {
                // Roulette background PNG lives in Assets/General.
                auto lowerFileName = to_lower_ascii(fileName);
                if (lowerFileName == "roulette.png" && !g_rouletteBgFound)
                {
                    g_rouletteBgFound = true;
                    g_rouletteBgDataOffset = fileOffset;
                    g_rouletteBgDataSize = fileSize;
                }
                else if (lowerFileName == "rewardicon.png" && !g_rewardIconFound)
                {
                    g_rewardIconFound = true;
                    g_rewardIconDataOffset = fileOffset;
                    g_rewardIconDataSize = fileSize;
                }
                else if (lowerFileName == "rouletteicon.png" && !g_rouletteIconFound)
                {
                    g_rouletteIconFound = true;
                    g_rouletteIconDataOffset = fileOffset;
                    g_rouletteIconDataSize = fileSize;
                }
                else if (lowerFileName == "settingsicon.png" && !g_settingsIconFound)
                {
                    g_settingsIconFound = true;
                    g_settingsIconDataOffset = fileOffset;
                    g_settingsIconDataSize = fileSize;
                }
                else if (lowerFileName == "npcicon.png" && !g_npcIconFound)
                {
                    g_npcIconFound = true;
                    g_npcIconDataOffset = fileOffset;
                    g_npcIconDataSize = fileSize;
                }
            }
        }

        uint32_t directoryCount = 0;
        if (!read_sah_u32(data, offset, directoryCount))
            return false;

        for (uint32_t i = 0; i < directoryCount; ++i)
        {
            if (!scan_sah_directory_for_visual_tokens(data, offset, path))
                return false;
        }

        return true;
    }

    void ensure_emoji_catalog_loaded()
    {
        if (g_emojiCatalogLoaded)
            return;

        g_emojiCatalogLoaded = true;
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
        scan_sah_directory_for_visual_tokens(data, offset, "");

        std::sort(g_emojis.begin(), g_emojis.end(), [](const EmojiEntry& lhs, const EmojiEntry& rhs) {
            if (lhs.kind != rhs.kind)
                return lhs.kind < rhs.kind;
            return lhs.index < rhs.index;
        });

        // Build token prefix index for O(1) lookup.
        // Key is a 16-bit hash of the first 2 chars after ':'.
        g_emojiTokenIndex.clear();
        g_emojiTokenIndex.reserve(g_emojis.size());
        for (auto& emoji : g_emojis)
        {
            if (emoji.token.size() < 3)
                continue; // need at least ":X:"
            uint16_t key = static_cast<uint16_t>(
                (static_cast<unsigned char>(emoji.token[1]) << 8) |
                 static_cast<unsigned char>(emoji.token[2]));
            g_emojiTokenIndex[key].push_back(&emoji);
        }
    }

    bool read_client_data_file(const EmojiEntry& emoji, std::vector<char>& fileData)
    {
        if (emoji.dataSize == 0 || emoji.dataSize > UINT_MAX)
            return false;

        auto safPath = get_game_relative_path("data.saf");
        std::ifstream stream(safPath, std::ios::binary);
        if (!stream)
            return false;

        stream.seekg(static_cast<std::streamoff>(emoji.dataOffset), std::ios::beg);
        if (!stream)
            return false;

        fileData.assign(static_cast<std::size_t>(emoji.dataSize), 0);
        stream.read(fileData.data(), static_cast<std::streamsize>(fileData.size()));
        return stream.gcount() == static_cast<std::streamsize>(fileData.size());
    }

    LPDIRECT3DTEXTURE9 load_png_texture(EmojiEntry& emoji)
    {
        if (!g_device)
            return nullptr;

        std::vector<char> fileData;
        if (!read_client_data_file(emoji, fileData))
            return nullptr;

        return create_texture_from_image_memory(g_device, fileData.data(), static_cast<UINT>(fileData.size()));
    }

    LPDIRECT3DTEXTURE9 load_saf_texture(LPDIRECT3DTEXTURE9& texture, bool& loadAttempted, bool found, uint64_t offset, uint64_t size)
    {
        if (loadAttempted)
            return texture;
        loadAttempted = true;

        if (!found || size == 0 || size > UINT_MAX)
            return nullptr;

        if (!g_device)
        {
            loadAttempted = false;
            return nullptr;
        }

        auto safPath = get_game_relative_path("data.saf");
        std::ifstream stream(safPath, std::ios::binary);
        if (!stream)
            return nullptr;

        stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!stream)
            return nullptr;

        std::vector<char> fileData(static_cast<std::size_t>(size), 0);
        stream.read(fileData.data(), static_cast<std::streamsize>(fileData.size()));
        if (stream.gcount() != static_cast<std::streamsize>(fileData.size()))
            return nullptr;

        texture = create_texture_from_image_memory(g_device, fileData.data(), static_cast<UINT>(fileData.size()));
        return texture;
    }

    LPDIRECT3DTEXTURE9 load_roulette_bg_texture()
    {
        return load_saf_texture(g_rouletteBgTexture, g_rouletteBgLoadAttempted, g_rouletteBgFound, g_rouletteBgDataOffset, g_rouletteBgDataSize);
    }

    LPDIRECT3DTEXTURE9 load_reward_icon_texture()
    {
        return load_saf_texture(g_rewardIconTexture, g_rewardIconLoadAttempted, g_rewardIconFound, g_rewardIconDataOffset, g_rewardIconDataSize);
    }

    LPDIRECT3DTEXTURE9 load_roulette_icon_texture()
    {
        return load_saf_texture(g_rouletteIconTexture, g_rouletteIconLoadAttempted, g_rouletteIconFound, g_rouletteIconDataOffset, g_rouletteIconDataSize);
    }

    LPDIRECT3DTEXTURE9 load_settings_icon_texture()
    {
        return load_saf_texture(g_settingsIconTexture, g_settingsIconLoadAttempted, g_settingsIconFound, g_settingsIconDataOffset, g_settingsIconDataSize);
    }

    LPDIRECT3DTEXTURE9 load_npc_icon_texture()
    {
        return load_saf_texture(g_npcIconTexture, g_npcIconLoadAttempted, g_npcIconFound, g_npcIconDataOffset, g_npcIconDataSize);
    }

    bool ensure_gdiplus_started()
    {
        if (g_gdiplusToken)
            return true;

        if (g_gdiplusStartAttempted)
            return false;

        g_gdiplusStartAttempted = true;
        Gdiplus::GdiplusStartupInput input;
        return Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr) == Gdiplus::Ok;
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
        if (!g_device)
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
        if (FAILED(g_device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)) || !texture)
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

    LPDIRECT3DTEXTURE9 load_gif_preview_texture(EmojiEntry& emoji)
    {
        if (!ensure_gdiplus_started())
            return nullptr;

        std::vector<char> fileData;
        if (!read_client_data_file(emoji, fileData) || fileData.empty())
            return nullptr;

        auto memory = GlobalAlloc(GMEM_MOVEABLE, fileData.size());
        if (!memory)
            return nullptr;

        auto* destination = GlobalLock(memory);
        if (!destination)
        {
            GlobalFree(memory);
            return nullptr;
        }

        std::memcpy(destination, fileData.data(), fileData.size());
        GlobalUnlock(memory);

        IStream* stream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream)) || !stream)
        {
            GlobalFree(memory);
            return nullptr;
        }

        std::unique_ptr<Gdiplus::Bitmap> bitmap(Gdiplus::Bitmap::FromStream(stream));
        if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok)
        {
            bitmap.reset();
            stream->Release();
            return nullptr;
        }

        auto texture = create_texture_from_argb_bitmap(*bitmap);
        bitmap.reset();
        stream->Release();
        return texture;
    }

    void load_gif_frames(EmojiEntry& emoji)
    {
        if (!ensure_gdiplus_started())
            return;

        std::vector<char> fileData;
        if (!read_client_data_file(emoji, fileData) || fileData.empty())
            return;

        auto memory = GlobalAlloc(GMEM_MOVEABLE, fileData.size());
        if (!memory)
            return;

        auto* destination = GlobalLock(memory);
        if (!destination)
        {
            GlobalFree(memory);
            return;
        }
        std::memcpy(destination, fileData.data(), fileData.size());
        GlobalUnlock(memory);

        IStream* stream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream)) || !stream)
        {
            GlobalFree(memory);
            return;
        }

        std::unique_ptr<Gdiplus::Bitmap> bitmap(Gdiplus::Bitmap::FromStream(stream));
        if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok)
        {
            bitmap.reset();
            stream->Release();
            return;
        }

        auto dimensionCount = bitmap->GetFrameDimensionsCount();
        if (dimensionCount == 0)
        {
            bitmap.reset();
            stream->Release();
            return;
        }

        std::vector<GUID> dimensions(dimensionCount);
        if (bitmap->GetFrameDimensionsList(dimensions.data(), dimensionCount) != Gdiplus::Ok)
        {
            bitmap.reset();
            stream->Release();
            return;
        }

        auto frameCount = bitmap->GetFrameCount(&dimensions[0]);
        if (frameCount == 0)
        {
            bitmap.reset();
            stream->Release();
            return;
        }

        for (UINT i = 0; i < frameCount; ++i)
        {
            if (bitmap->SelectActiveFrame(&dimensions[0], i) != Gdiplus::Ok)
                continue;

            auto texture = create_texture_from_argb_bitmap(*bitmap);
            if (!texture)
                continue;

            emoji.frames.push_back({ texture, read_gif_frame_delay_ms(*bitmap, i) });
        }
        bitmap.reset();
        stream->Release();
    }

    LPDIRECT3DTEXTURE9 get_emoji_texture(EmojiEntry& emoji)
    {
        if (!emoji.loadAttempted)
        {
            if (emoji.kind == VisualTokenKind::Gif)
                load_gif_frames(emoji);
            else
                emoji.texture = load_png_texture(emoji);
            emoji.loadAttempted = true;
        }

        if (emoji.kind != VisualTokenKind::Gif)
            return emoji.texture;

        if (emoji.frames.empty())
            return nullptr;

        DWORD totalDelay = 0;
        for (auto& frame : emoji.frames)
            totalDelay += frame.delayMs ? frame.delayMs : 100;

        if (totalDelay == 0)
            return emoji.frames.front().texture;

        auto position = GetTickCount() % totalDelay;
        DWORD accumulated = 0;
        for (auto& frame : emoji.frames)
        {
            accumulated += frame.delayMs ? frame.delayMs : 100;
            if (position < accumulated)
                return frame.texture;
        }

        return emoji.frames.back().texture;
    }

    EmojiEntry* find_emoji_by_token(const char* text)
    {
        if (!text || text[0] != ':')
            return nullptr;

        ensure_emoji_catalog_loaded();

        // Need at least ":X:" (3 chars) for any valid token
        if (text[1] == '\0' || text[2] == '\0')
            return nullptr;

        // Use 2-char prefix index for O(1) bucket lookup
        uint16_t key = static_cast<uint16_t>(
            (static_cast<unsigned char>(text[1]) << 8) |
             static_cast<unsigned char>(text[2]));

        auto it = g_emojiTokenIndex.find(key);
        if (it == g_emojiTokenIndex.end())
            return nullptr;

        for (auto* emoji : it->second)
        {
            auto tokenLength = emoji->token.size();
            if (std::strncmp(text, emoji->token.c_str(), tokenLength) == 0)
                return emoji;
        }

        return nullptr;
    }

    bool is_lower_chat_type(int chatType)
    {
        // Upper bar system messages — never rendered in the lower chat box,
        // so emoji overlay must not track them.
        // 15=orange dmg, 16=red dmg, 17=red death, 18=yellow acquire,
        // 19=green acquire, 20=violet dmg, 21=blue acquire, 22=green acquire
        if (chatType >= 15 && chatType <= 22)
            return false;

        // System/notice types that render in the upper bar or as popup text,
        // not in the scrollable lower chat box. Useful for upper/lower routing tests
        // sweep: 23,24,25,28,29,30,31,34,50 all go to the upper bar.
        // 51+ are placeholder duplicates (verified 52..250 identical).
        if (chatType == 23 || chatType == 24 || chatType == 25
            || chatType == 28 || chatType == 29 || chatType == 30
            || chatType == 31 || chatType == 34 || chatType == 50
            || chatType >= 51)
            return false;

        // Lower chat: types 0-14, 26-27, 32-33, 35-49
        return chatType >= 0 && chatType <= 50;
    }

    static float measure_chat_prefix_width_fallback(const char* text, int len)
    {
        auto w = 0.0f;
        for (int i = 0; i < len; ++i)
            w += text[i] == '\t' ? 24.0f : 5.5f;
        return w;
    }

    float measure_chat_prefix_width(const std::string& prefix)
    {
        if (prefix.empty())
            return 0.0f;

        // Guard: only call the native measure function when the game is
        // fully loaded — avoids intermittent crash at startup when the
        // text-measure object at 0x22B69B0 isn't initialised yet.
        if (!is_game_scene())
            return measure_chat_prefix_width_fallback(prefix.c_str(), static_cast<int>(prefix.size()));

        using MeasureTextWidth = int(__thiscall*)(void*, const char*, int, int);
        auto measureTextWidth = reinterpret_cast<MeasureTextWidth>(0x575740);

        __try
        {
            auto width = measureTextWidth(
                reinterpret_cast<void*>(0x22B69B0),
                prefix.c_str(),
                static_cast<int>(prefix.size()),
                0);

            if (width > 0)
                return static_cast<float>(width);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        return measure_chat_prefix_width_fallback(prefix.c_str(), static_cast<int>(prefix.size()));
    }

    bool sanitize_visual_tokens(const char* text, std::string& output, ChatEmojiLineOverlay& line, int replacementSpaces)
    {
        if (!text || text[0] == '\0')
            return false;

        auto changed = false;
        auto textLength = std::strlen(text);
        output.clear();
        output.reserve(textLength);
        line = {};
        line.tick = GetTickCount();

        for (auto index = std::size_t{ 0 }; index < textLength;)
        {
            auto emoji = find_emoji_by_token(text + index);
            if (emoji)
            {
                if (is_visual_token_enabled(emoji->kind))
                {
                    line.tokens.push_back({ emoji, measure_chat_prefix_width(output) });
                    output.append(replacementSpaces, ' ');
                }

                index += emoji->token.size();
                changed = true;
                continue;
            }

            VisualTokenKind tokenKind{};
            std::size_t tokenLength = 0;
            if (match_visual_token_text(text + index, tokenKind, tokenLength) && !is_visual_token_enabled(tokenKind))
            {
                index += tokenLength;
                changed = true;
                continue;
            }

            output.push_back(text[index]);
            ++index;
        }

        return changed;
    }

    float get_chat_text_width()
    {
        if (g_chatTextWidthOverride > 0.0f)
            return g_chatTextWidthOverride;
        if (!g_var) return 200.0f;
        auto clientW = static_cast<float>(g_var->client.width);
        auto w = clientW * g_tune.chatRightPct - g_tune.chatTextX - 20.0f;
        return w > 50.0f ? w : 50.0f;
    }

    int estimate_visual_lines(const char* text)
    {
        if (!text || text[0] == '\0' || !g_var)
            return 1;

        auto textWidth = measure_chat_prefix_width(std::string(text, std::strlen(text)));
        auto lines = static_cast<int>(std::ceil(textWidth / get_chat_text_width()));
        return lines > 0 ? lines : 1;
    }

    void remember_chat_emoji_line(int chatType, const char* text, ChatEmojiLineOverlay&& line)
    {
        if (!is_lower_chat_type(chatType) || !text || text[0] == '\0')
            return;

        auto vLines = estimate_visual_lines(text);

        std::lock_guard<std::mutex> lock(g_chatEmojiMutex);
        g_lowerChatEmojiLines.push_back({ text, std::move(line.tokens), vLines });
        while (g_lowerChatEmojiLines.size() > 100)
            g_lowerChatEmojiLines.pop_front();

        // Auto-scroll to bottom when new message arrives (like the game does)
        g_chatScrollOffset = 0;
    }

    bool matches_sanitized_static_text(const char* text, const std::string& expected, std::size_t* outPrefixLen)
    {
        if (outPrefixLen)
            *outPrefixLen = 0;

        if (!text || expected.empty())
            return false;

        auto textLength = std::strlen(text);
        auto expLen = expected.size();

        if (textLength == 0 || expLen == 0)
            return false;

        if (textLength <= expLen)
        {
            if (std::strncmp(text, expected.c_str(), textLength) != 0)
                return false;
            for (auto i = textLength; i < expLen; ++i)
            {
                if (expected[i] != ' ')
                    return false;
            }
            return true;
        }

        auto found = std::strstr(text, expected.c_str());
        if (found)
        {
            if (outPrefixLen)
                *outPrefixLen = static_cast<std::size_t>(found - text);
            return true;
        }

        auto nonSpaceEnd = expLen;
        while (nonSpaceEnd > 0 && expected[nonSpaceEnd - 1] == ' ')
            --nonSpaceEnd;

        if (nonSpaceEnd >= 4)
        {
            std::string trimmed = expected.substr(0, nonSpaceEnd);
            found = std::strstr(text, trimmed.c_str());
            if (found)
            {
                if (outPrefixLen)
                    *outPrefixLen = static_cast<std::size_t>(found - text);
                return true;
            }
        }

        return false;
    }

    bool safe_matches_sanitized_static_text(const char* text, const std::string& expected, std::size_t* outPrefixLen)
    {
        __try
        {
            return matches_sanitized_static_text(text, expected, outPrefixLen);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    const char* __cdecl prepare_chat_text_for_emojis(int chatType, const char* text)
    {
        // Record every chat type for the GM debug panel
        debug_panel::record_chat_type(chatType, text);

        if (!text || text[0] == '\0')
            return text;

        if (debug_panel::hide_native_chat_visuals())
            return "";

        ChatEmojiLineOverlay line{};
        auto changed = sanitize_visual_tokens(text, g_sanitizedChatText, line, 5);

        if (!changed)
        {
            ChatEmojiLineOverlay emptyLine{};
            remember_chat_emoji_line(chatType, text, std::move(emptyLine));
            return text;
        }

        remember_chat_emoji_line(chatType, g_sanitizedChatText.c_str(), std::move(line));
        return g_sanitizedChatText.c_str();
    }

    const char* prepare_text_for_emojis(const char* text, bool pushStaticCreateContext)
    {
        if (!text || text[0] == '\0')
            return text;

        ChatEmojiLineOverlay line{};
        auto changed = sanitize_visual_tokens(text, g_sanitizedFloatingText, line, 4);
        auto* result = changed ? g_sanitizedFloatingText.c_str() : text;

        if (!changed)
        {
            std::lock_guard<std::mutex> lock(g_chatEmojiMutex);
            for (auto it = g_lowerChatEmojiLines.rbegin(); it != g_lowerChatEmojiLines.rend(); ++it)
            {
                std::size_t prefixLen = 0;
                if (matches_sanitized_static_text(text, it->text, &prefixLen))
                {
                    line.tick = GetTickCount();
                    line.tokens = it->tokens;

                    if (prefixLen > 0)
                    {
                        std::string prefix(text, prefixLen);
                        auto prefixWidth = measure_chat_prefix_width(prefix);
                        for (auto& token : line.tokens)
                            token.xOffset += prefixWidth;
                    }
                    break;
                }
            }
        }

        if (pushStaticCreateContext)
        {
            g_staticTextCreateStack.push_back(std::move(line));
        }
        else
        {
            std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
            g_hasPendingFloatingEmojiLine = !line.tokens.empty();
            if (g_hasPendingFloatingEmojiLine)
                g_pendingFloatingEmojiLine = std::move(line);
        }

        return result;
    }

    const char* __cdecl prepare_static_text_for_emojis(const char* text)
    {
        return prepare_text_for_emojis(text, true);
    }

    constexpr std::size_t kMaxBubbleTextLength = 30;

    const char* __cdecl prepare_floating_text_for_emojis(const char* text)
    {
        // Skip emoji rendering in bubbles for long messages — they wrap and look broken
        if (text && std::strlen(text) > kMaxBubbleTextLength)
            return text;
        return prepare_text_for_emojis(text, false);
    }

    void __cdecl capture_floating_static_text(void* staticText)
    {
        std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
        if (!g_hasPendingFloatingEmojiLine)
            return;

        if (staticText)
            g_floatingEmojiLines[staticText] = g_pendingFloatingEmojiLine;

        g_pendingFloatingEmojiLine = {};
        g_hasPendingFloatingEmojiLine = false;
    }

    void __cdecl capture_created_static_text(void* staticText)
    {
        if (g_staticTextCreateStack.empty())
        {
            capture_floating_static_text(staticText);
            return;
        }

        auto line = std::move(g_staticTextCreateStack.back());
        g_staticTextCreateStack.pop_back();

        if (!staticText || line.tokens.empty())
            return;

        std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
        g_floatingEmojiLines[staticText] = std::move(line);
    }

    void __cdecl capture_chat_balloon_text(void* chatBalloon)
    {
        if (!chatBalloon)
            return;

        auto staticText = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(chatBalloon) + 0x8);
        capture_floating_static_text(staticText);
    }

    bool is_lower_chat_render_position(int x, int y)
    {
        return x >= 0
            && x <= static_cast<int>(g_tune.lowerChatMaxX)
            && y >= static_cast<int>(g_tune.lowerChatMinY);
    }

    void __cdecl record_native_text_draw_probe(std::uintptr_t returnAddress, const char* text, int x, int y)
    {
        if (!is_lower_chat_render_position(x, y))
            return;

        std::vector<ChatEmojiTokenOverlay> tokens;
        {
            std::lock_guard<std::mutex> lock(g_chatEmojiMutex);
            for (auto it = g_lowerChatEmojiLines.rbegin(); it != g_lowerChatEmojiLines.rend(); ++it)
            {
                std::size_t prefixLen = 0;
                if (safe_matches_sanitized_static_text(text, it->text, &prefixLen))
                {
                    tokens = it->tokens;
                    if (prefixLen > 0)
                    {
                        std::string prefix(text, prefixLen);
                        auto prefixWidth = measure_chat_prefix_width(prefix);
                        for (auto& token : tokens)
                            token.xOffset += prefixWidth;
                    }
                    break;
                }
            }
        }

        if (tokens.empty())
            return;

        std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
        g_floatingEmojiRenders.push_back({
            GetTickCount(),
            x,
            y,
            true,
            std::move(tokens) });

        if (g_floatingEmojiRenders.size() > 512)
            g_floatingEmojiRenders.erase(g_floatingEmojiRenders.begin(), g_floatingEmojiRenders.begin() + (g_floatingEmojiRenders.size() - 512));
    }

    void __cdecl record_floating_static_text_render(void* staticText, int x, int y)
    {
        if (!staticText)
            return;

        auto lowerChat = is_lower_chat_render_position(x, y);

        std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
        auto line = g_floatingEmojiLines.find(staticText);
        if (line == g_floatingEmojiLines.end())
            return;

        if (line->second.tokens.empty())
            return;

        auto now = GetTickCount();
        for (auto& render : g_floatingEmojiRenders)
        {
            if (render.tick == now
                && render.x == x
                && render.y == y
                && render.tokens.size() == line->second.tokens.size())
            {
                return;
            }
        }

        g_floatingEmojiRenders.push_back({
            now,
            x,
            y,
            lowerChat,
            line->second.tokens });

        if (g_floatingEmojiRenders.size() > 512)
            g_floatingEmojiRenders.erase(g_floatingEmojiRenders.begin(), g_floatingEmojiRenders.begin() + (g_floatingEmojiRenders.size() - 512));
    }

    void draw_visual_token_run(ImDrawList* drawList, const std::vector<ChatEmojiTokenOverlay>& tokens, float baseX, float baseY, float iconSize)
    {
        auto emojiIndex = 0;
        for (auto& token : tokens)
        {
            if (!token.emoji || !is_visual_token_enabled(token.emoji->kind))
                continue;

            auto texture = get_emoji_texture(*token.emoji);
            if (!texture)
                continue;

            auto x = baseX + token.xOffset + static_cast<float>(emojiIndex) * 2.0f;
            drawList->AddImage(
                reinterpret_cast<ImTextureID>(texture),
                ImVec2(x, baseY),
                ImVec2(x + iconSize, baseY + iconSize));
            ++emojiIndex;
        }
    }

    void draw_floating_emoji_overlays()
    {
        if (!is_game_scene() || is_emoji_transition_grace_active())
            return;

        std::vector<FloatingEmojiRenderOverlay> renders;
        {
            std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
            auto now = GetTickCount();

            // Only keep very recent entries (single frame)
            for (auto it = g_floatingEmojiRenders.begin(); it != g_floatingEmojiRenders.end();)
            {
                if (now - it->tick > 35)
                    it = g_floatingEmojiRenders.erase(it);
                else
                    ++it;
            }

            renders = g_floatingEmojiRenders;
        }

        auto drawList = ImGui::GetBackgroundDrawList();

        if (renders.empty())
            return;

        auto iconSize = g_tune.floatingIconSize;
        auto yAdjust = g_tune.floatingYAdjust;
        for (auto& render : renders)
        {
            if (render.lowerChat)
                continue; // Lower chat emojis are drawn by draw_lower_chat_emoji_overlay

            draw_visual_token_run(
                drawList,
                render.tokens,
                static_cast<float>(render.x),
                static_cast<float>(render.y) + yAdjust,
                iconSize);
        }
    }

    int read_chat_scroll_offset()
    {
        return g_chatScrollOffset;
    }

    void set_chat_scroll_offset(int value)
    {
        g_chatScrollOffset = value;
        if (g_chatScrollOffset < 0) g_chatScrollOffset = 0;

        auto chatW = get_chat_text_width();
        int totalVisual = 0;
        {
            std::lock_guard<std::mutex> lock(g_chatEmojiMutex);
            for (auto& l : g_lowerChatEmojiLines)
            {
                auto tw = measure_chat_prefix_width(l.text);
                auto vl = static_cast<int>(std::ceil(tw / chatW));
                totalVisual += vl > 0 ? vl : 1;
            }
        }
        auto maxScroll = totalVisual - kScrollMinLines;
        if (maxScroll < 0) maxScroll = 0;
        if (g_chatScrollOffset > maxScroll) g_chatScrollOffset = maxScroll;
    }

    void draw_lower_chat_emoji_overlay()
    {
        if (!g_var || !is_game_scene())
            return;
        if (is_emoji_transition_grace_active())
            return;

        auto drawList = ImGui::GetBackgroundDrawList();
        auto iconSize = g_tune.iconSize;

        auto clientW = static_cast<float>(g_var->client.width);
        auto clientH = static_cast<float>(g_var->client.height);

        auto chatLeftX = 9.0f;
        auto chatRightX = clientW * g_tune.chatRightPct;
        auto chatTextX = g_tune.chatTextX;
        auto lineHeight = g_tune.lineHeight;
        auto chatBottomY = clientH - g_tune.chatBottomOffset;
        auto chatTopY = chatBottomY - clientH * g_tune.chatTopPct;

        auto maxVisibleLines = static_cast<int>((chatBottomY - chatTopY) / lineHeight);
        if (maxVisibleLines < 4) maxVisibleLines = 4;
        if (maxVisibleLines > 30) maxVisibleLines = 30;

        auto scrollOffset = read_chat_scroll_offset();

        std::lock_guard<std::mutex> lock(g_chatEmojiMutex);

        auto totalLines = static_cast<int>(g_lowerChatEmojiLines.size());
        if (totalLines == 0)
            return;

        // Clip to chat area bounds — emojis outside this rect are invisible
        drawList->PushClipRect(
            ImVec2(chatLeftX, chatTopY),
            ImVec2(chatRightX, chatBottomY + iconSize));

        // Wrap-width used for both line-count estimation and per-emoji
        // X positioning — a single source of truth avoids drift.
        auto chatTextWidth = chatRightX - chatTextX - 20.0f;
        if (chatTextWidth < 50.0f) chatTextWidth = 50.0f;

        // Walk from newest to oldest, tracking visual line position.
        auto visualRow = 0;  // visual rows from bottom (0 = bottom-most)
        for (auto it = g_lowerChatEmojiLines.rbegin();
            it != g_lowerChatEmojiLines.rend();
            ++it)
        {
            // Recalculate visual lines at render time using the same
            // chatTextWidth that will position emojis.  The pre-computed
            // value can drift when resolution changes or the estimate at
            // insertion time used different constants.
            auto textWidth = measure_chat_prefix_width(it->text);
            auto msgVisualLines = static_cast<int>(std::ceil(textWidth / chatTextWidth));
            if (msgVisualLines < 1) msgVisualLines = 1;

            auto msgTopRow = visualRow + msgVisualLines - 1;

            if (msgTopRow < scrollOffset)
            {
                visualRow += msgVisualLines;
                continue;
            }

            auto displayRow = msgTopRow - scrollOffset;
            if (displayRow >= maxVisibleLines)
                break;

            if (!it->tokens.empty())
            {
                for (auto& token : it->tokens)
                {
                    if (!token.emoji || !is_visual_token_enabled(token.emoji->kind))
                        continue;

                    auto texture = get_emoji_texture(*token.emoji);
                    if (!texture)
                        continue;

                    // Which wrapped line does this emoji land on? (0 = first)
                    auto wrapLine = static_cast<int>(token.xOffset / chatTextWidth);
                    auto localX   = token.xOffset - wrapLine * chatTextWidth;

                    auto emojiRow = displayRow - wrapLine;
                    if (emojiRow < 0)
                        continue;  // scrolled out

                    auto y = chatBottomY - static_cast<float>(emojiRow + 1) * lineHeight;
                    auto x = chatTextX + localX;

                    drawList->AddImage(
                        reinterpret_cast<ImTextureID>(texture),
                        ImVec2(x, y),
                        ImVec2(x + iconSize, y + iconSize));
                }
            }

            visualRow += msgVisualLines;
        }

        drawList->PopClipRect();
    }

    void release_emoji_textures()
    {
        ensure_emoji_catalog_loaded();
        for (auto& emoji : g_emojis)
        {
            if (emoji.texture)
            {
                emoji.texture->Release();
                emoji.texture = nullptr;
            }
            if (emoji.previewTexture)
            {
                emoji.previewTexture->Release();
                emoji.previewTexture = nullptr;
            }
            for (auto& frame : emoji.frames)
            {
                if (frame.texture)
                {
                    frame.texture->Release();
                    frame.texture = nullptr;
                }
            }
            emoji.frames.clear();
            emoji.loadAttempted = false;
            emoji.previewLoadAttempted = false;
            emoji.previewLastUsedTick = 0;
        }
    }

    void post_emoji_token(const char* token)
    {
        ensure_client_sysmsg_dispatch_ready();
        if (!token || !g_var->hwnd || !IsWindow(g_var->hwnd))
            return;

        PostMessageA(g_var->hwnd, kClientEmojiTokenWindowMessage, 0, reinterpret_cast<LPARAM>(token));
    }

    void draw_emoji_fallback(const ImVec2& min, const ImVec2& max, ImU32 color)
    {
        auto drawList = ImGui::GetWindowDrawList();
        auto center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        auto radius = std::min(max.x - min.x, max.y - min.y) * 0.36f;
        drawList->AddCircleFilled(center, radius, color, 20);
        auto eyeRadius = std::max(1.0f, radius * 0.14f);
        drawList->AddCircleFilled(ImVec2(center.x - radius * 0.35f, center.y - radius * 0.18f), eyeRadius, IM_COL32(35, 31, 26, 230), 8);
        drawList->AddCircleFilled(ImVec2(center.x + radius * 0.35f, center.y - radius * 0.18f), eyeRadius, IM_COL32(35, 31, 26, 230), 8);
        drawList->AddBezierCubic(
            ImVec2(center.x - radius * 0.42f, center.y + radius * 0.18f),
            ImVec2(center.x - radius * 0.22f, center.y + radius * 0.46f),
            ImVec2(center.x + radius * 0.22f, center.y + radius * 0.46f),
            ImVec2(center.x + radius * 0.42f, center.y + radius * 0.18f),
            IM_COL32(35, 31, 26, 230),
            std::max(1.0f, radius * 0.17f));
    }

    bool emoji_image_button(const char* id, EmojiEntry& emoji, const ImVec2& size)
    {
        auto texture = get_emoji_texture(emoji);
        bool clicked = false;
        if (texture)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.14f));
            clicked = ImGui::ImageButton(
                id,
                reinterpret_cast<ImTextureID>(texture),
                size,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PopStyleColor(3);
        }
        else
        {
            return false;
        }

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", emoji.token.c_str());

        return clicked;
    }

    bool is_visual_token_picker_load_attempted(const EmojiEntry& emoji)
    {
        return emoji.kind == VisualTokenKind::Gif
            ? emoji.previewLoadAttempted
            : emoji.loadAttempted;
    }

    LPDIRECT3DTEXTURE9 get_visual_token_picker_texture(EmojiEntry& emoji, bool allowLoad)
    {
        if (emoji.kind != VisualTokenKind::Gif)
        {
            if (!allowLoad && !emoji.loadAttempted)
                return nullptr;

            return get_emoji_texture(emoji);
        }

        if (!emoji.previewLoadAttempted && allowLoad)
        {
            emoji.previewTexture = load_gif_preview_texture(emoji);
            emoji.previewLoadAttempted = true;
        }

        if (emoji.previewTexture)
            emoji.previewLastUsedTick = GetTickCount();

        return emoji.previewTexture;
    }

    // The picker may expose hundreds of GIFs; keep browsing cheap by caching
    // static previews only and evicting older previews as the user scrolls.
    void prune_gif_picker_preview_textures()
    {
        constexpr std::size_t kMaxResidentGifPickerPreviews = 96;
        std::vector<EmojiEntry*> residentPreviews;
        residentPreviews.reserve(g_emojis.size());

        for (auto& emoji : g_emojis)
        {
            if (emoji.kind == VisualTokenKind::Gif && emoji.previewTexture)
                residentPreviews.push_back(&emoji);
        }

        if (residentPreviews.size() <= kMaxResidentGifPickerPreviews)
            return;

        std::sort(
            residentPreviews.begin(),
            residentPreviews.end(),
            [](const EmojiEntry* lhs, const EmojiEntry* rhs)
            {
                return lhs->previewLastUsedTick < rhs->previewLastUsedTick;
            });

        auto texturesToRelease = residentPreviews.size() - kMaxResidentGifPickerPreviews;
        for (std::size_t i = 0; i < texturesToRelease; ++i)
        {
            auto* emoji = residentPreviews[i];
            if (!emoji || !emoji->previewTexture)
                continue;

            emoji->previewTexture->Release();
            emoji->previewTexture = nullptr;
            emoji->previewLoadAttempted = false;
            emoji->previewLastUsedTick = 0;
        }
    }

    bool image_texture_button(const char* id, LPDIRECT3DTEXTURE9 texture, const ImVec2& size)
    {
        if (!texture)
            return false;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.14f));
        auto clicked = ImGui::ImageButton(
            id,
            reinterpret_cast<ImTextureID>(texture),
            size,
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f),
            ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PopStyleColor(3);
        return clicked;
    }

    bool visual_token_slot_button(const char* id, EmojiEntry& emoji, const ImVec2& size, bool allowLoad)
    {
        auto attempted = is_visual_token_picker_load_attempted(emoji);
        auto texture = (allowLoad || attempted) ? get_visual_token_picker_texture(emoji, allowLoad) : nullptr;
        if (texture)
        {
            auto clicked = image_texture_button(id, texture, size);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", emoji.token.c_str());

            return clicked;
        }

        ImGui::InvisibleButton(id, size);
        auto min = ImGui::GetItemRectMin();
        auto max = ImGui::GetItemRectMax();
        auto drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, IM_COL32(31, 34, 40, 120), 4.0f);
        drawList->AddRect(min, max, IM_COL32(105, 116, 132, 120), 4.0f);

        attempted = is_visual_token_picker_load_attempted(emoji);
        auto label = attempted ? "!" : "...";
        auto textSize = ImGui::CalcTextSize(label);
        drawList->AddText(
            ImVec2(min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f),
            IM_COL32(185, 192, 204, 185),
            label);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", attempted ? emoji.token.c_str() : "Loading...");

        return false;
    }

    bool draw_visual_token_picker_grid(VisualTokenKind kind, const ImVec2& iconSize)
    {
        if (!is_visual_token_enabled(kind))
        {
            ImGui::TextDisabled("Off");
            return false;
        }

        constexpr int kColumns = 8;
        constexpr int kMaxGifLoadsPerFrame = 3;
        constexpr int kMaxEmojiLoadsPerFrame = 16;
        auto loadBudget = kind == VisualTokenKind::Gif ? kMaxGifLoadsPerFrame : kMaxEmojiLoadsPerFrame;
        auto loadsThisFrame = 0;
        auto clicked = false;
        std::vector<std::size_t> entries;
        entries.reserve(g_emojis.size());
        for (std::size_t i = 0; i < g_emojis.size(); ++i)
        {
            if (g_emojis[i].kind == kind)
                entries.push_back(i);
        }

        ImGui::BeginChild(
            kind == VisualTokenKind::Gif ? "##gif_picker_scroll" : "##emoji_picker_scroll",
            ImVec2(0.0f, 0.0f),
            false,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);

        if (entries.empty())
        {
            ImGui::TextDisabled("Empty");
            ImGui::EndChild();
            return false;
        }

        auto rowCount = static_cast<int>((entries.size() + kColumns - 1) / kColumns);
        auto rowHeight = iconSize.y + ImGui::GetStyle().ItemSpacing.y;
        ImGuiListClipper clipper;
        clipper.Begin(rowCount, rowHeight);
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                for (int col = 0; col < kColumns; ++col)
                {
                    auto entryOffset = static_cast<std::size_t>(row * kColumns + col);
                    if (entryOffset >= entries.size())
                        break;

                    auto emojiIndex = entries[entryOffset];
                    auto& emoji = g_emojis[emojiIndex];
                    auto attempted = is_visual_token_picker_load_attempted(emoji);
                    auto allowLoad = attempted || loadsThisFrame < loadBudget;
                    if (!attempted && allowLoad)
                        ++loadsThisFrame;

                    ImGui::PushID(static_cast<int>(emojiIndex));
                    if (visual_token_slot_button("##visual_token", emoji, iconSize, allowLoad))
                    {
                        post_emoji_token(emoji.token.c_str());
                        g_showEmojiPicker = false;
                        clicked = true;
                    }
                    ImGui::PopID();

                    if (col + 1 < kColumns)
                        ImGui::SameLine();
                }
            }
        }

        ImGui::EndChild();
        if (kind == VisualTokenKind::Gif)
            prune_gif_picker_preview_textures();

        return clicked;
    }

    void draw_visual_token_picker_controls()
    {
        auto changed = false;
        changed |= ImGui::Checkbox("Emojis", &g_emojisEnabled);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Gifs", &g_gifsEnabled);

        if (changed)
            save_imgui_settings();

        // Reposition controls
        if (g_emojiRepositionMode)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.35f, 0.1f, 0.9f));
            if (ImGui::Button("Click anywhere to place", ImVec2(-1.0f, 0.0f)))
                g_emojiRepositionMode = false;
            ImGui::PopStyleColor();
        }
        else
        {
            auto availW = ImGui::GetContentRegionAvail().x;
            auto spacing = ImGui::GetStyle().ItemSpacing.x;
            auto btnW = (availW - spacing) * 0.5f;
            if (ImGui::Button("Move", ImVec2(btnW, 0.0f)))
                g_emojiRepositionMode = true;
            ImGui::SameLine();
            if (ImGui::Button("Reset", ImVec2(btnW, 0.0f)))
            {
                g_emojiButtonPosition = kDefaultEmojiButtonPosition;
                g_emojiRepositionMode = false;
                save_imgui_settings();
            }
        }

        ImGui::Separator();
    }

    void draw_emoji_overlay()
    {
        if (!is_game_scene() || is_emoji_transition_grace_active())
            return;

        auto& io = ImGui::GetIO();
        if (!is_overlay_display_usable(io.DisplaySize))
            return;

        auto now = GetTickCount();
        if (static_cast<int32_t>(now - g_emojiSceneTransitionUntilTick) < 0)
            return;

        auto buttonSize = kEmojiButtonSize;
        g_emojiButtonPosition = clamp_window_position(g_emojiButtonPosition, buttonSize, io.DisplaySize);

        // In reposition mode the button follows the cursor until clicked
        if (g_emojiRepositionMode)
        {
            auto hasMousePos = io.MousePos.x >= 0.0f && io.MousePos.y >= 0.0f;
            if (hasMousePos)
            {
                auto newPos = ImVec2(io.MousePos.x - buttonSize.x * 0.5f,
                                     io.MousePos.y - buttonSize.y * 0.5f);
                g_emojiButtonPosition = clamp_window_position(newPos, buttonSize, io.DisplaySize);
            }

            // Click anywhere (outside the picker) to confirm placement
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                && !is_cursor_in_rect(g_emojiPickerRect))
            {
                g_emojiRepositionMode = false;
                save_imgui_settings();
            }
        }

        ImGui::SetNextWindowPos(g_emojiButtonPosition, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(
            "##emoji_button_overlay",
            nullptr,
            ImGuiWindowFlags_NoDecoration
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::InvisibleButton("##emoji_toggle", buttonSize);

        auto min = ImGui::GetItemRectMin();
        auto max = ImGui::GetItemRectMax();
        remember_rect(g_emojiButtonRect, min, max);
        if (!g_emojiRepositionMode)
            handle_emoji_button_interaction();

        draw_emoji_fallback(min, max,
            g_emojiRepositionMode ? IM_COL32(28, 23, 18, 255) : IM_COL32(246, 199, 63, 255));
        if (ImGui::IsItemHovered() || is_cursor_in_rect(g_emojiButtonRect))
        {
            ImGui::SetNextWindowPos(ImVec2(min.x - 2.0f, min.y - 24.0f), ImGuiCond_Always);
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(g_emojiRepositionMode ? "Click to place here" : "Open emojis");
            ImGui::EndTooltip();
        }

        ImGui::End();
        ImGui::PopStyleVar(2);

        if (!g_showEmojiPicker)
            return;

        auto pickerSize = kEmojiPickerSize;
        g_emojiPickerPosition = clamp_window_position(
            ImVec2(g_emojiButtonPosition.x + kEmojiPickerOffset.x,
                   g_emojiButtonPosition.y + kEmojiPickerOffset.y),
            pickerSize, io.DisplaySize);

        ImGui::SetNextWindowPos(g_emojiPickerPosition, ImGuiCond_Always);
        ImGui::SetNextWindowSize(pickerSize, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.55f);
        bool pickerOpen = g_showEmojiPicker;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin(
            "##emoji_picker",
            &pickerOpen,
            ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBringToFrontOnFocus))
        {
            auto size = ImGui::GetWindowSize();

            auto pos = ImGui::GetWindowPos();
            g_emojiPickerPosition = pos;
            remember_rect(g_emojiPickerRect, pos, ImVec2(pos.x + size.x, pos.y + size.y));

            const auto iconSize = kEmojiPickerIconSize;
            draw_visual_token_picker_controls();
            if (ImGui::BeginTabBar("##visual_token_tabs", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton))
            {
                if (ImGui::BeginTabItem("Emojis"))
                {
                    draw_visual_token_picker_grid(VisualTokenKind::Emoji, iconSize);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Gifs"))
                {
                    draw_visual_token_picker_grid(VisualTokenKind::Gif, iconSize);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
        g_showEmojiPicker = pickerOpen;
    }

    void patch_call(void* address, void* destination)
    {
#pragma pack(push, 1)
        struct CallInstruction
        {
            uint8_t opcode;
            uint32_t operand;
        } instruction{ 0xE8, 0 };
#pragma pack(pop)

        static_assert(sizeof(CallInstruction) == 5);

        instruction.operand = static_cast<uint32_t>(
            reinterpret_cast<std::uintptr_t>(destination) - reinterpret_cast<std::uintptr_t>(address) - sizeof(instruction));
        util::write_memory(address, &instruction, sizeof(instruction));
    }

    void patch_calls_to(std::uintptr_t target, void* destination)
    {
        constexpr auto kTextStart = std::uintptr_t{ 0x401000 };
        constexpr auto kTextEnd = std::uintptr_t{ 0x745000 };

        for (auto address = kTextStart; address + 5 <= kTextEnd; ++address)
        {
            auto bytes = reinterpret_cast<std::uint8_t*>(address);
            if (bytes[0] != 0xE8)
                continue;

            auto operand = *reinterpret_cast<std::int32_t*>(bytes + 1);
            auto callTarget = address + 5 + operand;
            if (callTarget == target)
                patch_call(reinterpret_cast<void*>(address), destination);
        }
    }

    void install_chat_emoji_hook()
    {
        if (g_chatEmojiHookInstalled)
            return;

        util::detour((void*)0x422B90, naked_chat_add_token_filter, 6);
        patch_call((void*)0x412744, naked_chat_balloon_text_create);
        util::detour((void*)0x41274D, naked_capture_chat_balloon_text, 6);
        patch_call((void*)0x453DEF, naked_floating_text_create);
        util::detour((void*)0x453DF4, naked_capture_floating_static_text, 9);
        patch_calls_to(0x57C280, naked_static_text_create);
        patch_calls_to(0x57CA20, naked_floating_static_text_draw);
        patch_calls_to(0x573C00, naked_native_text_draw_probe);

        g_chatEmojiHookInstalled = true;
    }

}
