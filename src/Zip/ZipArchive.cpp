#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#include <zlib.h>

#include "Zip/ZipArchive.h"
#include "Utils/PathCase.h"
#include "XGDException.h"

namespace {

uint16_t read_le16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8));
}

uint32_t read_le32(const uint8_t* bytes)
{
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

uint64_t read_le64(const uint8_t* bytes)
{
    return uint64_t(read_le32(bytes)) | (uint64_t(read_le32(bytes + 4)) << 32);
}

void read_exact(std::ifstream& input, uint64_t offset, void* destination, size_t size)
{
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset));
    input.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(size));
    if (input.gcount() != static_cast<std::streamsize>(size))
    {
        throw XGDException(ErrCode::FILE_READ, HERE(), "Failed to read zip");
    }
}

struct ZipEntry
{
    std::string name;
    uint16_t method{0};
    uint16_t flags{0};
    uint64_t compressed_size{0};
    uint64_t uncompressed_size{0};
    uint64_t local_offset{0};
};

bool safe_zip_path(const std::string& name, std::filesystem::path& relative)
{
    if (name.empty())
    {
        return false;
    }

    std::string normalized = name;
    for (char& character : normalized)
    {
        if (character == '\\')
        {
            character = '/';
        }
    }
    if (normalized.front() == '/' || (normalized.size() >= 2 && normalized[1] == ':'))
    {
        return false;
    }

    std::filesystem::path path;
    std::string part;
    for (size_t index = 0; index <= normalized.size(); ++index)
    {
        if (index == normalized.size() || normalized[index] == '/')
        {
            if (part == "..")
            {
                return false;
            }
            if (!part.empty() && part != ".")
            {
                path /= part;
            }
            part.clear();
        }
        else
        {
            part.push_back(normalized[index]);
        }
    }
    relative = path;
    return true;
}

void apply_zip64_extra(const uint8_t* extra, size_t extra_size, uint32_t compressed32, uint32_t uncompressed32, uint32_t offset32, ZipEntry& entry)
{
    size_t position = 0;
    while (position + 4 <= extra_size)
    {
        uint16_t id = read_le16(extra + position);
        uint16_t size = read_le16(extra + position + 2);
        position += 4;
        if (position + size > extra_size)
        {
            break;
        }
        if (id == 0x0001)
        {
            size_t cursor = 0;
            if (uncompressed32 == 0xFFFFFFFFu && cursor + 8 <= size)
            {
                entry.uncompressed_size = read_le64(extra + position + cursor);
                cursor += 8;
            }
            if (compressed32 == 0xFFFFFFFFu && cursor + 8 <= size)
            {
                entry.compressed_size = read_le64(extra + position + cursor);
                cursor += 8;
            }
            if (offset32 == 0xFFFFFFFFu && cursor + 8 <= size)
            {
                entry.local_offset = read_le64(extra + position + cursor);
            }
        }
        position += size;
    }
}

std::vector<ZipEntry> read_central_directory(std::ifstream& input, uint64_t file_size)
{
    constexpr size_t kMaxComment = 65535;
    constexpr size_t kEocdSize = 22;
    size_t window = kMaxComment + kEocdSize;
    if (window > file_size)
    {
        window = static_cast<size_t>(file_size);
    }

    if (window < kEocdSize)
    {
        throw XGDException(ErrCode::ISO_INVALID, HERE(), "Zip directory was not found");
    }

    std::vector<uint8_t> tail(window);
    uint64_t tail_offset = file_size - window;
    read_exact(input, tail_offset, tail.data(), tail.size());

    int eocd_index = -1;
    for (size_t index = tail.size() - kEocdSize; ; --index)
    {
        if (tail[index] == 0x50 && tail[index + 1] == 0x4B && tail[index + 2] == 0x05 && tail[index + 3] == 0x06)
        {
            uint16_t comment_length = read_le16(tail.data() + index + 20);
            if (index + kEocdSize + comment_length == tail.size())
            {
                eocd_index = static_cast<int>(index);
                break;
            }
        }
        if (index == 0)
        {
            break;
        }
    }
    if (eocd_index < 0)
    {
        throw XGDException(ErrCode::ISO_INVALID, HERE(), "Zip directory was not found");
    }

    const uint8_t* eocd = tail.data() + eocd_index;
    uint16_t disk_number = read_le16(eocd + 4);
    uint16_t directory_disk = read_le16(eocd + 6);
    uint64_t entry_count = read_le16(eocd + 10);
    uint64_t directory_size = read_le32(eocd + 12);
    uint64_t directory_offset = read_le32(eocd + 16);
    if (disk_number != 0 || directory_disk != 0)
    {
        throw XGDException(ErrCode::ISO_INVALID, HERE(), "Split zip archives are not supported");
    }

    bool need_zip64 = entry_count == 0xFFFF || directory_size == 0xFFFFFFFF || directory_offset == 0xFFFFFFFF;
    if (need_zip64)
    {
        if (eocd_index < 20)
        {
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Zip64 directory locator was not found");
        }
        const uint8_t* locator = eocd - 20;
        if (std::memcmp(locator, "PK\x06\x07", 4) != 0)
        {
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Zip64 directory locator was not found");
        }
        uint64_t zip64_offset = read_le64(locator + 8);
        uint8_t zip64[56];
        read_exact(input, zip64_offset, zip64, sizeof(zip64));
        if (std::memcmp(zip64, "PK\x06\x06", 4) != 0)
        {
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Zip64 directory is invalid");
        }
        entry_count = read_le64(zip64 + 32);
        directory_size = read_le64(zip64 + 40);
        directory_offset = read_le64(zip64 + 48);
    }

    std::vector<uint8_t> directory(static_cast<size_t>(directory_size));
    if (directory_size > 0)
    {
        read_exact(input, directory_offset, directory.data(), directory.size());
    }

    std::vector<ZipEntry> entries;
    size_t position = 0;
    for (uint64_t index = 0; index < entry_count; ++index)
    {
        if (position + 46 > directory.size() || std::memcmp(directory.data() + position, "PK\x01\x02", 4) != 0)
        {
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Zip directory entry is invalid");
        }
        ZipEntry entry;
        entry.flags = read_le16(directory.data() + position + 8);
        entry.method = read_le16(directory.data() + position + 10);
        uint32_t compressed32 = read_le32(directory.data() + position + 20);
        uint32_t uncompressed32 = read_le32(directory.data() + position + 24);
        uint16_t name_length = read_le16(directory.data() + position + 28);
        uint16_t extra_length = read_le16(directory.data() + position + 30);
        uint16_t comment_length = read_le16(directory.data() + position + 32);
        uint32_t offset32 = read_le32(directory.data() + position + 42);
        entry.compressed_size = compressed32;
        entry.uncompressed_size = uncompressed32;
        entry.local_offset = offset32;

        size_t name_offset = position + 46;
        if (name_offset + name_length + extra_length + comment_length > directory.size())
        {
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Zip directory entry is truncated");
        }
        entry.name.assign(reinterpret_cast<const char*>(directory.data() + name_offset), name_length);
        if (extra_length > 0)
        {
            apply_zip64_extra(directory.data() + name_offset + name_length, extra_length, compressed32, uncompressed32, offset32, entry);
        }
        entries.push_back(std::move(entry));
        position = name_offset + name_length + extra_length + comment_length;
    }
    return entries;
}

void inflate_raw(std::ifstream& input, std::ofstream& output, uint64_t data_offset, uint64_t compressed_size, uint64_t uncompressed_size)
{
    if (compressed_size == 0)
    {
        if (uncompressed_size != 0)
        {
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Zip entry is empty");
        }
        return;
    }

    z_stream stream{};
    if (inflateInit2(&stream, -15) != Z_OK)
    {
        throw XGDException(ErrCode::MISC, HERE(), "Failed to start zip inflation");
    }

    std::vector<unsigned char> in_buffer(64 * 1024);
    std::vector<unsigned char> out_buffer(64 * 1024);
    uint64_t remaining = compressed_size;
    uint64_t file_offset = data_offset;
    uint64_t written = 0;
    int result = Z_OK;

    while (result != Z_STREAM_END)
    {
        if (stream.avail_in == 0)
        {
            if (remaining == 0)
            {
                inflateEnd(&stream);
                throw XGDException(ErrCode::ISO_INVALID, HERE(), "Zip entry ended early");
            }
            size_t chunk = remaining > in_buffer.size() ? in_buffer.size() : static_cast<size_t>(remaining);
            read_exact(input, file_offset, in_buffer.data(), chunk);
            file_offset += chunk;
            remaining -= chunk;
            stream.next_in = in_buffer.data();
            stream.avail_in = static_cast<uInt>(chunk);
        }

        stream.next_out = out_buffer.data();
        stream.avail_out = static_cast<uInt>(out_buffer.size());
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END)
        {
            inflateEnd(&stream);
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Zip entry could not be inflated");
        }
        size_t produced = out_buffer.size() - stream.avail_out;
        output.write(reinterpret_cast<const char*>(out_buffer.data()), static_cast<std::streamsize>(produced));
        if (!output)
        {
            inflateEnd(&stream);
            throw XGDException(ErrCode::FILE_WRITE, HERE(), "Failed to write unzipped file");
        }
        written += produced;
    }
    inflateEnd(&stream);

    if (written != uncompressed_size)
    {
        throw XGDException(ErrCode::ISO_INVALID, HERE(), "Unzipped size does not match the zip directory");
    }
}

} // namespace

bool ZipArchive::is_zip(const std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
    {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return false;
    }
    uint8_t magic[4] = {};
    input.read(reinterpret_cast<char*>(magic), sizeof(magic));
    if (input.gcount() < 4)
    {
        return false;
    }
    return magic[0] == 0x50 && magic[1] == 0x4B && magic[2] == 0x03 && magic[3] == 0x04;
}

void ZipArchive::extract(const std::filesystem::path& zip_path, const std::filesystem::path& out_directory)
{
    std::ifstream input(zip_path, std::ios::binary);
    if (!input)
    {
        throw XGDException(ErrCode::FILE_OPEN, HERE(), "Failed to open zip: " + zip_path.string());
    }
    input.seekg(0, std::ios::end);
    uint64_t file_size = static_cast<uint64_t>(input.tellg());

    const std::vector<ZipEntry> entries = read_central_directory(input, file_size);
    std::vector<std::string> paths;
    paths.reserve(entries.size());
    for (const ZipEntry& entry : entries)
    {
        std::filesystem::path relative;
        if (!safe_zip_path(entry.name, relative) || relative.empty())
        {
            continue;
        }
        paths.push_back(relative.generic_string());
    }
    PathCase::reject_host_collisions(paths, out_directory);

    std::error_code error;
    std::filesystem::create_directories(out_directory, error);
    if (error)
    {
        throw XGDException(ErrCode::FS_MKDIR, HERE(), error.message());
    }

    for (const ZipEntry& entry : entries)
    {
        if ((entry.flags & 0x1) != 0)
        {
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Encrypted zip entries are not supported");
        }
        bool directory = !entry.name.empty() && (entry.name.back() == '/' || entry.name.back() == '\\');
        std::filesystem::path relative;
        if (!safe_zip_path(entry.name, relative))
        {
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Zip path is not safe: " + entry.name);
        }
        if (relative.empty())
        {
            continue;
        }

        std::filesystem::path destination = (out_directory / relative).lexically_normal();
        std::filesystem::path rel = destination.lexically_relative(out_directory.lexically_normal());
        if (rel.empty() || *rel.begin() == "..")
        {
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Zip path escapes the output directory");
        }
        if (directory)
        {
            std::filesystem::create_directories(destination, error);
            if (error)
            {
                throw XGDException(ErrCode::FS_MKDIR, HERE(), error.message());
            }
            continue;
        }
        if (entry.method != 0 && entry.method != 8)
        {
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Unsupported zip compression");
        }

        std::filesystem::create_directories(destination.parent_path(), error);
        if (error)
        {
            throw XGDException(ErrCode::FS_MKDIR, HERE(), error.message());
        }
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            throw XGDException(ErrCode::FILE_OPEN, HERE(), "Failed to create " + destination.string());
        }

        uint8_t local[30];
        read_exact(input, entry.local_offset, local, sizeof(local));
        if (std::memcmp(local, "PK\x03\x04", 4) != 0)
        {
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Zip local header is invalid");
        }
        uint16_t name_length = read_le16(local + 26);
        uint16_t extra_length = read_le16(local + 28);
        uint64_t data_offset = entry.local_offset + 30 + name_length + extra_length;

        if (entry.method == 0)
        {
            std::vector<uint8_t> buffer(64 * 1024);
            uint64_t remaining = entry.compressed_size;
            uint64_t offset = data_offset;
            while (remaining > 0)
            {
                size_t chunk = remaining > buffer.size() ? buffer.size() : static_cast<size_t>(remaining);
                read_exact(input, offset, buffer.data(), chunk);
                output.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(chunk));
                if (!output)
                {
                    throw XGDException(ErrCode::FILE_WRITE, HERE(), "Failed to write unzipped file");
                }
                offset += chunk;
                remaining -= chunk;
            }
        }
        else
        {
            inflate_raw(input, output, data_offset, entry.compressed_size, entry.uncompressed_size);
        }
    }
}
