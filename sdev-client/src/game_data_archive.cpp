#include "include/game_data_archive.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

namespace game_data
{
    namespace
    {
        bool read_u32(const std::vector<char>& data, std::size_t& offset, std::uint32_t& value)
        {
            if (offset + sizeof(value) > data.size())
                return false;

            std::memcpy(&value, data.data() + offset, sizeof(value));
            offset += sizeof(value);
            return true;
        }

        bool read_u64(const std::vector<char>& data, std::size_t& offset, std::uint64_t& value)
        {
            if (offset + sizeof(value) > data.size())
                return false;

            std::memcpy(&value, data.data() + offset, sizeof(value));
            offset += sizeof(value);
            return true;
        }

        bool read_string(const std::vector<char>& data, std::size_t& offset, std::string& value)
        {
            std::uint32_t length = 0;
            if (!read_u32(data, offset, length) || offset + length > data.size())
                return false;

            value.assign(data.data() + offset, data.data() + offset + length);
            while (!value.empty() && value.back() == '\0')
                value.pop_back();

            offset += length;
            return true;
        }

        bool scan_directory(
            const std::vector<char>& data,
            std::size_t& offset,
            const std::string& parentPath,
            const std::function<void(const SahFileEntry&)>& visitor)
        {
            std::string name;
            if (!read_string(data, offset, name))
                return false;

            auto path = parentPath;
            if (!name.empty())
                path = path.empty() ? name : path + "\\" + name;

            std::uint32_t fileCount = 0;
            if (!read_u32(data, offset, fileCount))
                return false;

            auto lowerPath = lower_ascii(path);
            for (std::uint32_t i = 0; i < fileCount; ++i)
            {
                std::string fileName;
                std::uint64_t fileOffset = 0;
                std::uint64_t fileSize = 0;
                if (!read_string(data, offset, fileName)
                    || !read_u64(data, offset, fileOffset)
                    || !read_u64(data, offset, fileSize))
                    return false;

                visitor(SahFileEntry{
                    path,
                    lowerPath,
                    fileName,
                    lower_ascii(fileName),
                    fileOffset,
                    fileSize
                });
            }

            std::uint32_t directoryCount = 0;
            if (!read_u32(data, offset, directoryCount))
                return false;

            for (std::uint32_t i = 0; i < directoryCount; ++i)
            {
                if (!scan_directory(data, offset, path, visitor))
                    return false;
            }

            return true;
        }
    }

    const std::string& base_dir()
    {
        static std::string cached = [] {
            char modulePath[MAX_PATH]{};
            if (GetModuleFileNameA(nullptr, modulePath, sizeof(modulePath)) == 0)
                return std::string(".\\");

            std::string path(modulePath);
            auto slash = path.find_last_of("\\/");
            if (slash == std::string::npos)
                return std::string(".\\");

            path.resize(slash + 1);
            return path;
        }();

        return cached;
    }

    std::string relative_path(const char* relativePath)
    {
        auto path = base_dir();
        if (relativePath)
            path += relativePath;
        return path;
    }

    std::string lower_ascii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    bool scan_sah_files(const std::function<void(const SahFileEntry&)>& visitor)
    {
        if (!visitor)
            return false;

        std::ifstream stream(relative_path("data.sah"), std::ios::binary);
        if (!stream)
            return false;

        std::vector<char> data(
            (std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>());
        if (data.size() <= 0x34 || std::memcmp(data.data(), "SAH", 3) != 0)
            return false;

        auto offset = std::size_t{ 0x34 };
        return scan_directory(data, offset, "", visitor);
    }

    bool read_saf_file(std::uint64_t offset, std::uint64_t size, std::vector<char>& out)
    {
        out.clear();
        if (size == 0 || size > UINT_MAX)
            return false;

        std::ifstream stream(relative_path("data.saf"), std::ios::binary);
        if (!stream)
            return false;

        stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!stream)
            return false;

        out.assign(static_cast<std::size_t>(size), 0);
        stream.read(out.data(), static_cast<std::streamsize>(out.size()));
        return stream.gcount() == static_cast<std::streamsize>(out.size());
    }
}
