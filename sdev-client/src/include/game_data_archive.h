#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace game_data
{
    struct SahFileEntry
    {
        std::string path;
        std::string lowerPath;
        std::string fileName;
        std::string lowerFileName;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
    };

    const std::string& base_dir();
    std::string relative_path(const char* relativePath);
    std::string lower_ascii(std::string value);

    bool scan_sah_files(const std::function<void(const SahFileEntry&)>& visitor);
    bool read_saf_file(std::uint64_t offset, std::uint64_t size, std::vector<char>& out);
}
