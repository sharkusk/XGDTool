#include <cstring>

#include "Stfs/StfsReader.h"
#include "Utils/PathCase.h"
#include "XGDException.h"
#include "XGDLog.h"

namespace {

constexpr uint32_t kLevel1Blocks = 170 * 170;

uint32_t read_be32(const uint8_t* bytes)
{
    return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
}

uint16_t read_be16(const uint8_t* bytes)
{
    return static_cast<uint16_t>((uint16_t(bytes[0]) << 8) | uint16_t(bytes[1]));
}

uint16_t read_le16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8));
}

uint32_t read_le24(const uint8_t* bytes)
{
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16);
}

uint64_t round_up_block(uint64_t value)
{
    return (value + 0xFFF) & ~uint64_t(0xFFF);
}

uint32_t hash_info(const std::vector<uint8_t>& table, uint32_t record)
{
    size_t offset = size_t(record) * 0x18 + 0x14;
    if (offset + 4 > table.size())
    {
        throw XGDException(ErrCode::FILE_READ, HERE(), "STFS hash entry is outside the hash table");
    }
    return read_be32(table.data() + offset);
}

std::filesystem::path safe_destination(const std::filesystem::path& root, const std::filesystem::path& relative)
{
    std::filesystem::path full = (root / relative).lexically_normal();
    std::filesystem::path base = root.lexically_normal();
    std::filesystem::path rel = full.lexically_relative(base);
    if (rel.empty() || *rel.begin() == "..")
    {
        throw XGDException(ErrCode::ISO_INVALID, HERE(), "STFS path escapes the output directory");
    }
    return full;
}

void find_packages_at(const std::filesystem::path& directory, int depth, int max_depth, std::vector<std::filesystem::path>& packages)
{
    if (depth > max_depth)
    {
        return;
    }

    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error))
    {
        if (error)
        {
            return;
        }
        if (entry.is_regular_file() && StfsReader::is_package(entry.path()))
        {
            packages.push_back(entry.path());
        }
        else if (entry.is_directory())
        {
            find_packages_at(entry.path(), depth + 1, max_depth, packages);
        }
    }
}

} // namespace

StfsReader::StfsReader(const std::filesystem::path& path)
    : path_(path)
{
    stream_.open(path_, std::ios::binary);
    if (!stream_)
    {
        throw XGDException(ErrCode::FILE_OPEN, HERE(), "Failed to open STFS package: " + path_.string());
    }
    read_header();
    read_file_table();
}

void StfsReader::read_at(uint64_t offset, void* destination, size_t size)
{
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(offset));
    stream_.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(size));
    if (stream_.gcount() != static_cast<std::streamsize>(size))
    {
        throw XGDException(ErrCode::FILE_READ, HERE(), "Failed to read STFS package");
    }
}

void StfsReader::read_header()
{
    stream_.seekg(0, std::ios::end);
    uint64_t file_size = static_cast<uint64_t>(stream_.tellg());
    // volume type is a big-endian uint32 at 0x3A9
    if (file_size < 0x3AD)
    {
        throw XGDException(ErrCode::ISO_INVALID, HERE(), "File is too small to be an STFS package");
    }

    uint8_t magic[4];
    read_at(0, magic, sizeof(magic));
    bool live = std::memcmp(magic, "LIVE", 4) == 0;
    bool pirs = std::memcmp(magic, "PIRS", 4) == 0;
    bool con = std::memcmp(magic, "CON ", 4) == 0;
    if (!live && !pirs && !con)
    {
        throw XGDException(ErrCode::ISO_INVALID, HERE(), "Not an Xbox 360 LIVE, PIRS, or CON package");
    }

    uint8_t header_size_bytes[4];
    read_at(0x340, header_size_bytes, sizeof(header_size_bytes));
    uint32_t header_size = read_be32(header_size_bytes);
    if (header_size == 0 || header_size > file_size)
    {
        throw XGDException(ErrCode::ISO_INVALID, HERE(), "STFS header size is invalid");
    }
    data_start_ = round_up_block(header_size);

    // Metadata begins at 0x344. The 0x24-byte volume descriptor starts at 0x379.
    uint8_t descriptor[0x34];
    read_at(0x379, descriptor, sizeof(descriptor));
    uint32_t volume_type = read_be32(descriptor + 0x30);
    if (volume_type != 0)
    {
        throw XGDException(ErrCode::ISO_INVALID, HERE(), "SVOD packages are not XBLA titles");
    }

    uint8_t flags = descriptor[2];
    read_only_ = (flags & 0x01) != 0;
    root_active_index_ = (flags & 0x02) != 0;
    blocks_per_hash_ = read_only_ ? 1 : 2;
    block_step_[0] = kBlocksPerHashLevel + blocks_per_hash_;
    block_step_[1] = (kBlocksPerHashLevel * kBlocksPerHashLevel) + ((kBlocksPerHashLevel + 1) * blocks_per_hash_);

    file_table_block_count_ = read_le16(descriptor + 3);
    file_table_block_ = read_le24(descriptor + 5);
    total_block_count_ = read_be32(descriptor + 0x1C);
}

uint64_t StfsReader::block_offset(uint64_t block_index) const
{
    uint64_t base = kBlocksPerHashLevel;
    uint64_t block = block_index;
    for (uint32_t level = 0; level < 3; ++level)
    {
        block += ((block_index + base) / base) * blocks_per_hash_;
        if (block_index < base)
        {
            break;
        }
        base *= kBlocksPerHashLevel;
    }
    return data_start_ + (block << 12);
}

uint32_t StfsReader::hash_block_number(uint32_t block_index, uint32_t level) const
{
    if (level == 0)
    {
        if (block_index < kBlocksPerHashLevel)
        {
            return 0;
        }

        uint32_t block = (block_index / kBlocksPerHashLevel) * block_step_[0];
        block += ((block_index / kLevel1Blocks) + 1) * blocks_per_hash_;
        if (block_index < kLevel1Blocks)
        {
            return block;
        }
        return block + blocks_per_hash_;
    }

    if (level == 1)
    {
        if (block_index < kLevel1Blocks)
        {
            return block_step_[0];
        }
        return (block_index / kLevel1Blocks) * block_step_[1] + blocks_per_hash_;
    }

    return block_step_[1];
}

uint64_t StfsReader::hash_block_offset(uint32_t block_index, uint32_t level) const
{
    return data_start_ + (uint64_t(hash_block_number(block_index, level)) << 12);
}

uint32_t StfsReader::next_block(uint32_t block_index)
{
    auto load_table = [this](uint64_t base_offset, uint32_t secondary)
    {
        if (hash_tables_.find(base_offset) != hash_tables_.end())
        {
            return;
        }
        std::vector<uint8_t> table(kBlockSize);
        read_at(base_offset + secondary, table.data(), table.size());
        hash_tables_.emplace(base_offset, std::move(table));
    };

    uint32_t secondary = 0;
    if (!read_only_)
    {
        secondary = root_active_index_ ? kBlockSize : 0;
        if (total_block_count_ > kBlocksPerHashLevel)
        {
            uint64_t level1_offset = hash_block_offset(block_index, 1);
            if (hash_tables_.find(level1_offset) == hash_tables_.end())
            {
                if (total_block_count_ > kLevel1Blocks)
                {
                    uint64_t level2_offset = hash_block_offset(block_index, 2);
                    load_table(level2_offset, secondary);
                    uint32_t record = (block_index / kLevel1Blocks) % kBlocksPerHashLevel;
                    uint32_t info = hash_info(hash_tables_.at(level2_offset), record);
                    secondary = (info & 0x40000000u) ? kBlockSize : 0;
                }
                load_table(level1_offset, secondary);
            }
            uint32_t record = (block_index / kBlocksPerHashLevel) % kBlocksPerHashLevel;
            uint32_t info = hash_info(hash_tables_.at(level1_offset), record);
            secondary = (info & 0x40000000u) ? kBlockSize : 0;
        }
    }

    uint64_t level0_offset = hash_block_offset(block_index, 0);
    load_table(level0_offset, secondary);
    uint32_t record = block_index % kBlocksPerHashLevel;
    return hash_info(hash_tables_.at(level0_offset), record) & 0xFFFFFFu;
}

std::filesystem::path StfsReader::entry_path(const std::vector<int32_t>& parents, int32_t parent_index, const std::string& name) const
{
    std::vector<std::string> parts;
    parts.push_back(name);

    int32_t current = parent_index;
    int guard = 0;
    while (current >= 0)
    {
        if (static_cast<size_t>(current) >= entries_.size() || static_cast<size_t>(current) >= parents.size() || ++guard > 64)
        {
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "STFS directory index is invalid");
        }
        parts.push_back(entries_[static_cast<size_t>(current)].path.filename().string());
        current = parents[static_cast<size_t>(current)];
    }

    std::filesystem::path path;
    for (auto part = parts.rbegin(); part != parts.rend(); ++part)
    {
        if (part->empty() || *part == "." || *part == "..")
        {
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Unsafe STFS file name");
        }
        path /= *part;
    }
    return path;
}

void StfsReader::read_file_table()
{
    std::vector<int32_t> parents;
    uint32_t table_block = file_table_block_;

    for (uint32_t block_number = 0; block_number < file_table_block_count_; ++block_number)
    {
        std::vector<uint8_t> block(kBlockSize);
        read_at(block_offset(table_block), block.data(), block.size());

        for (uint32_t entry_index = 0; entry_index < kBlockSize / 0x40; ++entry_index)
        {
            const uint8_t* entry = block.data() + entry_index * 0x40;
            if (entry[0] == 0)
            {
                break;
            }

            uint8_t flags = entry[0x28];
            uint32_t name_length = flags & 0x3F;
            if (name_length > 40)
            {
                name_length = 40;
            }
            std::string name(reinterpret_cast<const char*>(entry), name_length);
            size_t nul = name.find('\0');
            if (nul != std::string::npos)
            {
                name.resize(nul);
            }
            if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
            {
                throw XGDException(ErrCode::ISO_INVALID, HERE(), "Unsafe STFS file name");
            }

            uint16_t directory_index = read_be16(entry + 0x32);
            int32_t parent = directory_index == 0xFFFF ? -1 : static_cast<int32_t>(directory_index);
            if (parent >= static_cast<int32_t>(entries_.size()))
            {
                throw XGDException(ErrCode::ISO_INVALID, HERE(), "STFS directory index is out of range");
            }

            Entry item;
            item.path = entry_path(parents, parent, name);
            item.size = read_be32(entry + 0x34);
            item.directory = (flags & 0x80) != 0;
            item.start_block = read_le24(entry + 0x2F);
            entries_.push_back(std::move(item));
            parents.push_back(parent);
        }

        uint32_t next = next_block(table_block);
        if (next == kEndOfChain)
        {
            break;
        }
        table_block = next;
    }
}

void StfsReader::list() const
{
    for (const auto& entry : entries_)
    {
        if (entry.directory)
        {
            continue;
        }
        XGDLog() << entry.path.generic_string() << " (" << entry.size << " bytes)\n";
    }
}

void StfsReader::extract(const std::filesystem::path& out_directory)
{
    std::vector<std::string> paths;
    paths.reserve(entries_.size());
    for (const auto& entry : entries_)
    {
        if (!entry.path.empty())
        {
            paths.push_back(entry.path.generic_string());
        }
    }
    PathCase::reject_host_collisions(paths, out_directory);

    std::error_code error;
    std::filesystem::create_directories(out_directory, error);
    if (error)
    {
        throw XGDException(ErrCode::FS_MKDIR, HERE(), error.message());
    }

    uint64_t total = 0;
    for (const auto& entry : entries_)
    {
        if (!entry.directory)
        {
            total += entry.size;
        }
    }

    uint64_t processed = 0;
    std::vector<uint8_t> buffer(kBlockSize);

    for (const auto& entry : entries_)
    {
        std::filesystem::path destination = safe_destination(out_directory, entry.path);
        if (entry.directory)
        {
            std::filesystem::create_directories(destination, error);
            if (error)
            {
                throw XGDException(ErrCode::FS_MKDIR, HERE(), error.message());
            }
            continue;
        }

        std::filesystem::create_directories(destination.parent_path(), error);
        if (error)
        {
            throw XGDException(ErrCode::FS_MKDIR, HERE(), error.message());
        }

        std::ofstream out(destination, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            throw XGDException(ErrCode::FILE_OPEN, HERE(), "Failed to create " + destination.string());
        }

        uint64_t remaining = entry.size;
        uint32_t block = entry.start_block;
        uint32_t steps = 0;
        while (remaining > 0 && block != kEndOfChain)
        {
            if (++steps > total_block_count_ + 1)
            {
                throw XGDException(ErrCode::ISO_INVALID, HERE(), "STFS block chain is too long");
            }

            size_t chunk = remaining < kBlockSize ? static_cast<size_t>(remaining) : static_cast<size_t>(kBlockSize);
            read_at(block_offset(block), buffer.data(), chunk);
            out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(chunk));
            if (!out)
            {
                throw XGDException(ErrCode::FILE_WRITE, HERE(), "Failed to write " + destination.string());
            }
            remaining -= chunk;
            processed += chunk;
            if (total > 0)
            {
                XGDLog().print_progress(processed, total);
            }
            block = next_block(block);
        }

        if (remaining != 0)
        {
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "STFS file ended early: " + entry.path.generic_string());
        }
    }
}

bool StfsReader::is_package(const std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
    {
        return false;
    }
    uint64_t file_size = std::filesystem::file_size(path, error);
    if (error || file_size < 0x3AD)
    {
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return false;
    }

    uint8_t magic[4];
    input.read(reinterpret_cast<char*>(magic), sizeof(magic));
    if (!input)
    {
        return false;
    }
    bool live = std::memcmp(magic, "LIVE", 4) == 0;
    bool pirs = std::memcmp(magic, "PIRS", 4) == 0;
    bool con = std::memcmp(magic, "CON ", 4) == 0;
    if (!live && !pirs && !con)
    {
        return false;
    }

    input.seekg(0x3A9);
    uint8_t volume_type_bytes[4];
    input.read(reinterpret_cast<char*>(volume_type_bytes), sizeof(volume_type_bytes));
    if (!input)
    {
        return false;
    }
    return read_be32(volume_type_bytes) == 0;
}

std::vector<std::filesystem::path> StfsReader::find_packages(const std::filesystem::path& directory, int max_depth)
{
    std::vector<std::filesystem::path> packages;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error))
    {
        return packages;
    }
    find_packages_at(directory, 0, max_depth, packages);
    return packages;
}
