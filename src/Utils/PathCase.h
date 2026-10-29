#ifndef _PATH_CASE_H_
#define _PATH_CASE_H_

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace PathCase {

std::vector<std::vector<std::string>> collisions(const std::vector<std::string>& relative_paths);

// Stop when the destination volume would make any of these paths the same file.
void reject_host_collisions(const std::vector<std::string>& relative_paths, const std::filesystem::path& destination);

// Stop when a case-insensitive archive or rebuilt image cannot store every path.
void reject_archive_collisions(const std::vector<std::string>& relative_paths, std::string_view format_name);

}

#endif // _PATH_CASE_H_
