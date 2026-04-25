#include <filesystem>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ini.h"

int util::ini::delete_key(const wchar_t* section, const wchar_t* key, const std::filesystem::path& path)
{
    return WritePrivateProfileStringW(section, key, nullptr, path.c_str());
}

int util::ini::delete_section(const wchar_t* section, const std::filesystem::path& path)
{
    return WritePrivateProfileStringW(section, nullptr, nullptr, path.c_str());
}

std::vector<std::wstring> util::ini::get_keys(const wchar_t* section, const std::filesystem::path& path)
{
    std::vector<std::wstring> vec;
    auto str = get_value(section, nullptr, nullptr, path);
    if (str.empty())
        return vec;

    std::wistringstream iss(str);
    for (std::wstring str; std::getline(iss, str, L'\0'); )
        vec.push_back(str);

    return vec;
}

std::vector<std::pair<std::wstring, std::wstring>> util::ini::get_pairs(const wchar_t* section, const std::filesystem::path& path)
{
    std::vector<std::pair<std::wstring, std::wstring>> vec;
    for (const auto& key : get_keys(section, path))
    {
        auto value = get_value(section, key.c_str(), nullptr, path);
        vec.push_back({ key, value });
    }

    return vec;
}

std::vector<std::wstring> util::ini::get_sections(const std::filesystem::path& path)
{
    std::vector<std::wstring> vec;
    auto str = get_value(nullptr, nullptr, nullptr, path);
    if (str.empty())
        return vec;

    std::wistringstream iss(str);
    for (std::wstring str; std::getline(iss, str, L'\0'); )
        vec.push_back(str);

    return vec;
}

unsigned util::ini::get_value(const wchar_t* section, const wchar_t* key, int defaultValue, const std::filesystem::path& path)
{
    return GetPrivateProfileIntW(section, key, defaultValue, path.c_str());
}

std::wstring util::ini::get_value(const wchar_t* section, const wchar_t* key, const wchar_t* defaultValue, const std::filesystem::path& path)
{
    if (defaultValue == nullptr)
        defaultValue = L"";

    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec)
        return defaultValue;

    constexpr auto kMaxProfileChars = static_cast<std::uintmax_t>(std::numeric_limits<DWORD>::max() - 1);
    if (size > kMaxProfileChars)
        size = kMaxProfileChars;

    // INI section/key enumerations are returned as double-null-terminated lists.
    // Keep extra room so GetPrivateProfileStringW can report the complete list.
    std::wstring buffer(static_cast<std::size_t>(size) + 2, L'\0');
    auto count = GetPrivateProfileStringW(
        section,
        key,
        defaultValue,
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        path.c_str());
    if (!count)
        return defaultValue;

    buffer.resize(count);
    return buffer;
}

int util::ini::write_pair(const wchar_t* section, const wchar_t* key, const wchar_t* value, const std::filesystem::path& path)
{
    return WritePrivateProfileStringW(section, key, value, path.c_str());
}
