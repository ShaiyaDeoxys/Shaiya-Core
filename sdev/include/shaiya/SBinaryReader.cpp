#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include "SBinaryReader.h"
using namespace shaiya;

namespace
{
    constexpr std::size_t kMaxStringLength = 16 * 1024 * 1024;

    void read_exact(std::ifstream& stream, char* data, std::size_t size)
    {
        if (!size)
            return;

        stream.read(data, size);
        if (stream.gcount() != static_cast<std::streamsize>(size))
            throw std::runtime_error("Unexpected end of binary stream");
    }
}

void SBinaryReader::close()
{
    if (!m_stream)
        return;

    if (m_stream.is_open())
        m_stream.close();
}

void SBinaryReader::ignore(size_t count)
{
    m_stream.ignore(count);
    if (m_stream.gcount() != static_cast<std::streamsize>(count))
        throw std::runtime_error("Unexpected end of binary stream");
}

int SBinaryReader::peek()
{
    return m_stream.peek();
}

std::string SBinaryReader::readChars(size_t count)
{
    std::string buffer(count, 0);
    read_exact(m_stream, buffer.data(), buffer.size());
    return buffer;
}

std::string SBinaryReader::readString()
{
    auto count = readUInt32();
    if (count > kMaxStringLength)
        throw std::runtime_error("Binary string is too large");

    std::string buffer(count, 0);
    read_exact(m_stream, buffer.data(), buffer.size());
    return buffer;
}

int8_t SBinaryReader::readInt8()
{
    int8_t value{};
    read_exact(m_stream, reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

int16_t SBinaryReader::readInt16()
{
    int16_t value{};
    read_exact(m_stream, reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

int32_t SBinaryReader::readInt32()
{
    int32_t value{};
    read_exact(m_stream, reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

int64_t SBinaryReader::readInt64()
{
    int64_t value{};
    read_exact(m_stream, reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

uint8_t SBinaryReader::readUInt8()
{
    uint8_t value{};
    read_exact(m_stream, reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

uint16_t SBinaryReader::readUInt16()
{
    uint16_t value{};
    read_exact(m_stream, reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

uint32_t SBinaryReader::readUInt32()
{
    uint32_t value{};
    read_exact(m_stream, reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

uint64_t SBinaryReader::readUInt64()
{
    uint64_t value{};
    read_exact(m_stream, reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

float SBinaryReader::readSingle()
{
    float value{};
    read_exact(m_stream, reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

double SBinaryReader::readDouble()
{
    double value{};
    read_exact(m_stream, reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}
