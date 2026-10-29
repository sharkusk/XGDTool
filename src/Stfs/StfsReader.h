#ifndef _STFS_READER_H_
#define _STFS_READER_H_

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

// Reads an Xbox 360 STFS package (LIVE, PIRS, or CON). Games on Demand
// containers are SVOD and are rejected.
class StfsReader
{
public:
    struct Entry
    {
        std::filesystem::path path;
        uint64_t size{0};
        bool directory{false};
        uint32_t start_block{0};
    };

    explicit StfsReader(const std::filesystem::path& path);

    const std::vector<Entry>& entries() const { return entries_; };

    void list() const;
    void extract(const std::filesystem::path& out_directory);

    static bool is_package(const std::filesystem::path& path);
    static std::vector<std::filesystem::path> find_packages(const std::filesystem::path& directory, int max_depth);

private:
    static constexpr uint32_t kBlockSize = 0x1000;
    static constexpr uint32_t kBlocksPerHashLevel = 170;
    static constexpr uint32_t kEndOfChain = 0xFFFFFF;

    std::filesystem::path path_;
    std::ifstream stream_;
    uint64_t data_start_{0};
    uint32_t blocks_per_hash_{1};
    uint32_t block_step_[2]{0, 0};
    uint32_t file_table_block_count_{0};
    uint32_t file_table_block_{0};
    uint32_t total_block_count_{0};
    bool read_only_{true};
    bool root_active_index_{false};
    std::vector<Entry> entries_;
    std::unordered_map<uint64_t, std::vector<uint8_t>> hash_tables_;

    void read_header();
    void read_file_table();
    uint64_t block_offset(uint64_t block_index) const;
    uint32_t hash_block_number(uint32_t block_index, uint32_t level) const;
    uint64_t hash_block_offset(uint32_t block_index, uint32_t level) const;
    uint32_t next_block(uint32_t block_index);
    void read_at(uint64_t offset, void* destination, size_t size);
    std::filesystem::path entry_path(const std::vector<int32_t>& parents, int32_t index, const std::string& name) const;
};

#endif // _STFS_READER_H_
