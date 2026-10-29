#ifndef _ZIP_ARCHIVE_H_
#define _ZIP_ARCHIVE_H_

#include <filesystem>

class ZipArchive
{
public:
    static bool is_zip(const std::filesystem::path& path);
    static void extract(const std::filesystem::path& zip_path, const std::filesystem::path& out_directory);
};

#endif // _ZIP_ARCHIVE_H_
