#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <util/util.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <cstdint>
#include <cstring>

#include "include/interface_redirect.h"

namespace
{
    constexpr char kPngExtension[] = ".png";

    // Active custom UI folder name (9 chars, same length as "interface").
    // Set by patch_folder_to_custom() based on the UI level from CONFIG.ini.
    const char* g_customUiFolder = "intf_epi6";

    std::string to_lower_copy(std::string value)
    {
        for (auto& c : value)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        return value;
    }

    bool ends_with_ignore_case(const std::string& value, const char* suffix)
    {
        auto suffixLength = std::strlen(suffix);
        if (value.size() < suffixLength)
            return false;

        auto tail = value.substr(value.size() - suffixLength);
        return to_lower_copy(tail) == suffix;
    }

    std::filesystem::path get_client_data_sah_path()
    {
        char moduleFileName[MAX_PATH]{};
        if (!GetModuleFileNameA(nullptr, moduleFileName, MAX_PATH))
            return {};

        std::filesystem::path path(moduleFileName);
        path = path.parent_path();
        path /= "data.sah";
        return path;
    }

    struct InterfacePngRedirectData
    {
        std::unordered_set<std::string> fileNames;
        std::unordered_set<std::string> relativePaths;
    };

    void add_png_asset_redirect_entries(
        InterfacePngRedirectData& data,
        const std::string& relativePathWithoutExtension)
    {
        auto normalizedPath = relativePathWithoutExtension;
        for (auto& c : normalizedPath)
        {
            if (c == '/')
                c = '\\';
        }

        auto fileNamePos = normalizedPath.find_last_of("\\/");
        auto fileNameWithoutExtension = fileNamePos == std::string::npos
            ? normalizedPath
            : normalizedPath.substr(fileNamePos + 1);

        for (const auto* extension : { ".tga", ".jpg" })
        {
            data.fileNames.insert(fileNameWithoutExtension + extension);
            data.relativePaths.insert(normalizedPath + extension);
        }
    }

    bool set_contains_suffix(
        const std::unordered_set<std::string>& values,
        const std::string& candidate)
    {
        for (const auto& value : values)
        {
            if (candidate.size() < value.size())
                continue;

            if (candidate.compare(candidate.size() - value.size(), value.size(), value) == 0)
                return true;
        }

        return false;
    }

    InterfacePngRedirectData collect_interface_png_redirect_data()
    {
        InterfacePngRedirectData data{};
        auto dataSahPath = get_client_data_sah_path();
        if (dataSahPath.empty() || !std::filesystem::exists(dataSahPath))
            return data;

        std::ifstream stream(dataSahPath, std::ios::binary);
        if (!stream)
            return data;

        auto is_section_token = [](const std::string& token) -> bool
        {
            constexpr const char* relevantSections[]
            {
                "interface",
                "chaoticsquare",
                "worldname",
                "npcface",
                "minimap",
                "gamecard",
                "tarocard",
                "icon",
            };

            for (const auto* section : relevantSections)
            {
                if (token == section)
                    return true;
            }

            return false;
        };

        std::string currentSection;
        std::string token;
        char ch{};
        while (stream.get(ch))
        {
            auto uch = static_cast<unsigned char>(ch);
            if (uch >= 0x20 && uch <= 0x7E)
            {
                token.push_back(static_cast<char>(uch));
                continue;
            }

            if (token.size() >= 3)
            {
                auto lowerToken = to_lower_copy(token);
                if (is_section_token(lowerToken))
                {
                    currentSection = lowerToken;
                }
                else if (currentSection == "chaoticsquare" && lowerToken == "text")
                {
                    currentSection = "chaoticsquare\\text";
                }
                else if (lowerToken.ends_with(".png"))
                {
                    auto stem = lowerToken.substr(0, lowerToken.size() - 4);
                    if (currentSection == "icon")
                    {
                        // Icon handling stays disabled in the redirect for now.
                    }
                    else if (currentSection == "interface")
                    {
                        add_png_asset_redirect_entries(data, stem);
                    }
                    else if (!currentSection.empty())
                    {
                        add_png_asset_redirect_entries(data, currentSection + "\\" + stem);
                    }
                }
            }

            token.clear();
        }

        // These two root interface names are present in the historical lists we
        // built during migration work, but they do not appear cleanly in the
        // client's data.sah string stream. Keep them as explicit root entries so
        // the PNG redirect still covers them.
        add_png_asset_redirect_entries(data, "ch_option_graphic2");
        add_png_asset_redirect_entries(data, "window_button");

        return data;
    }

    bool is_known_interface_group_string(const std::string& lowerString)
    {
        constexpr const char* groupMarkers[]
        {
            "worldname\\",
            "npcface\\",
            "minimap\\",
            "minimap_",
            "gamecard\\",
            "tarocard\\",
            "wm_",
            "wm_button",
            "random_loading",
            "loading",
            "main_bottom",
            "main_bottom_button",
            "main_stats_",
            "chaoticsquare\\",
        };

        for (const auto* marker : groupMarkers)
        {
            if (lowerString.find(marker) != std::string::npos)
                return true;
        }

        return false;
    }

    bool is_known_interface_format_string(const std::string& lowerString)
    {
        constexpr const char* knownFormats[]
        {
            "%d.tga",
            "%d.jpg",
            "%02d.tga",
            "%02d.jpg",
            "%d_%02d.tga",
            "%d_%02d.jpg",
            "minimap_%d.tga",
            "minimap_%d.jpg",
            "minimap_%02d.tga",
            "minimap_%02d.jpg",
            "wm_e%d.tga",
            "wm_e%d.jpg",
            "wm_f%d.tga",
            "wm_f%d.jpg",
            "wm_%d.tga",
            "wm_%d.jpg",
            "wm_%c%d.tga",
            "wm_%c%d.jpg",
            "wm_%s.tga",
            "wm_%s.jpg",
            "%s.tga",
            "%s.jpg",
            "%s%d.tga",
            "%s%d.jpg",
            "%s0%d.tga",
            "%s0%d.jpg",
            "%s00%d.tga",
            "%s00%d.jpg",
            "%s000%d.tga",
            "%s000%d.jpg",
            "random_loading%02d.jpg",
            "game_card%d.tga",
            "tarocard%d.tga",
            "war_%d.tga",
        };

        for (const auto* format : knownFormats)
        {
            if (lowerString == format)
                return true;
        }

        return false;
    }

    bool should_redirect_interface_string(
        const std::string& lowerString,
        const InterfacePngRedirectData& redirectData)
    {
        if (!ends_with_ignore_case(lowerString, ".tga") && !ends_with_ignore_case(lowerString, ".jpg"))
            return false;

        // Keep icon assets on their original formats for now.
        if (lowerString.find("icon\\") != std::string::npos
            || lowerString.find("icon/") != std::string::npos
            || lowerString.find("\\icon\\") != std::string::npos
            || lowerString.find("/icon/") != std::string::npos)
            return false;

        if (is_known_interface_format_string(lowerString))
            return true;

        if (redirectData.fileNames.contains(lowerString))
            return true;

        if (set_contains_suffix(redirectData.fileNames, lowerString))
            return true;

        if (redirectData.relativePaths.contains(lowerString))
            return true;

        if (set_contains_suffix(redirectData.relativePaths, lowerString))
            return true;

        auto lastSlash = lowerString.find_last_of("\\/");
        if (lastSlash != std::string::npos)
        {
            auto fileName = lowerString.substr(lastSlash + 1);
            if (redirectData.fileNames.contains(fileName))
                return true;

            if (set_contains_suffix(redirectData.fileNames, fileName))
                return true;
        }

        if ((lowerString.find("data/interface/") != std::string::npos
            || lowerString.find("data\\interface\\") != std::string::npos)
            && lowerString.find("data/interface/icon/") == std::string::npos
            && lowerString.find("data\\interface\\icon\\") == std::string::npos)
            return true;

        return is_known_interface_group_string(lowerString);
    }

    void patch_module_interface_texture_extensions_to_png()
    {
        auto redirectData = collect_interface_png_redirect_data();
        if (redirectData.fileNames.empty() && redirectData.relativePaths.empty())
            return;

        auto moduleBase = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
        if (!moduleBase)
            return;

        auto dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(moduleBase);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
            return;

        auto ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(moduleBase + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
            return;

        auto imageSize = static_cast<std::size_t>(ntHeaders->OptionalHeader.SizeOfImage);
        for (std::size_t i = 0; i < imageSize; ++i)
        {
            auto current = moduleBase[i];
            if (current < 0x20 || current > 0x7E)
                continue;

            auto start = i;
            while (i < imageSize && moduleBase[i] >= 0x20 && moduleBase[i] <= 0x7E)
                ++i;

            if (i >= imageSize || moduleBase[i] != 0)
                continue;

            auto length = i - start;
            if (length < 5)
                continue;

            std::string candidate(reinterpret_cast<char*>(moduleBase + start), length);
            auto lowerCandidate = to_lower_copy(candidate);
            if (!should_redirect_interface_string(lowerCandidate, redirectData))
                continue;

            if (ends_with_ignore_case(lowerCandidate, ".tga")
                || ends_with_ignore_case(lowerCandidate, ".jpg"))
            {
                // Only rewrite the file extension in-place.
                // The original Game.exe string remains the same path/name, but now
                // points to a .png file instead of .tga/.jpg.
                constexpr char pngExtension[] = ".png";
                util::write_memory(moduleBase + start + length - 4, pngExtension, 4);
            }
        }
    }

    bool should_redirect_interface_folder_string(const std::string& lowerString)
    {
        return lowerString == "interface"
            || lowerString.starts_with("interface\\")
            || lowerString.starts_with("interface/")
            || lowerString.find("data/interface") != std::string::npos
            || lowerString.find("data\\interface") != std::string::npos
            || lowerString.find("/interface/") != std::string::npos
            || lowerString.find("\\interface\\") != std::string::npos;
    }

    void patch_module_interface_folder_to_custom()
    {
        auto moduleBase = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
        if (!moduleBase)
            return;

        auto dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(moduleBase);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
            return;

        auto ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(moduleBase + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
            return;

        auto imageSize = static_cast<std::size_t>(ntHeaders->OptionalHeader.SizeOfImage);
        for (std::size_t i = 0; i < imageSize; ++i)
        {
            auto current = moduleBase[i];
            if (current < 0x20 || current > 0x7E)
                continue;

            auto start = i;
            while (i < imageSize && moduleBase[i] >= 0x20 && moduleBase[i] <= 0x7E)
                ++i;

            if (i >= imageSize || moduleBase[i] != 0)
                continue;

            auto length = i - start;
            if (length < 9)
                continue;

            std::string candidate(reinterpret_cast<char*>(moduleBase + start), length);
            auto lowerCandidate = to_lower_copy(candidate);
            if (!should_redirect_interface_folder_string(lowerCandidate))
                continue;

            std::size_t pos = 0;
            while ((pos = lowerCandidate.find("interface", pos)) != std::string::npos)
            {
                util::write_memory(moduleBase + start + pos, g_customUiFolder, 9);
                pos += 9;
            }
        }
    }

    void patch_screenshot_extensions_to_png()
    {
        // Screenshot filename templates used by Game.exe.
        // This does not change the screenshot folder logic; it only rewrites the
        // built-in .jpg/.JPG filename templates to .png so newly generated
        // screenshots are saved with a PNG extension instead.
        //
        // If the client ever needs to go back to JPEG screenshots, remove or
        // comment out this helper call in hook::patch().
        util::write_memory((void*)0x75667C, kPngExtension, 4);
        util::write_memory((void*)0x756688, kPngExtension, 4);
        util::write_memory((void*)0x756694, kPngExtension, 4);
        util::write_memory((void*)0x7566A0, kPngExtension, 4);
        util::write_memory((void*)0x7566AC, kPngExtension, 4);
        util::write_memory((void*)0x7566B8, kPngExtension, 4);
        util::write_memory((void*)0x7566C4, kPngExtension, 4);
        util::write_memory((void*)0x7566D0, kPngExtension, 4);
    }
}

namespace interface_redirect
{
    void patch_texture_extensions_to_png()
    {
        patch_module_interface_texture_extensions_to_png();
    }

    void patch_folder_to_custom(int uiLevel)
    {
        g_customUiFolder = (uiLevel == 2) ? "intf_epi8" : "intf_epi6";
        patch_module_interface_folder_to_custom();
    }

    void patch_screenshot_extensions_to_png()
    {
        ::patch_screenshot_extensions_to_png();
    }
}
