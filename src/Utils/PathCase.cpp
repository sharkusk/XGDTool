#include <chrono>
#include <fstream>
#include <map>
#include <system_error>

#include "Utils/PathCase.h"
#include "XGDException.h"

namespace {

std::string normalize(std::string_view path)
{
    std::string out;
    out.reserve(path.size());
    for (char character : path)
    {
        if (character == '\\')
        {
            character = '/';
        }
        if (character == '/' && !out.empty() && out.back() == '/')
        {
            continue;
        }
        out.push_back(character);
    }
    while (out.size() > 1 && out.back() == '/')
    {
        out.pop_back();
    }
    return out;
}

std::string fold_ascii(std::string_view path)
{
    std::string folded = normalize(path);
    for (char& character : folded)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return folded;
}

void append_groups(std::string& message, const std::vector<std::vector<std::string>>& groups)
{
    for (const std::vector<std::string>& group : groups)
    {
        for (const std::string& path : group)
        {
            message += "  ";
            message += path;
            message += "\n";
        }
    }
}

bool volume_folds_case(const std::filesystem::path& destination)
{
    std::error_code error;
    std::filesystem::path directory = destination;
    if (std::filesystem::is_regular_file(directory, error))
    {
        directory = directory.parent_path();
    }
    while (!directory.empty() && !std::filesystem::is_directory(directory, error))
    {
        directory = directory.parent_path();
    }
    if (directory.empty() || !std::filesystem::is_directory(directory, error))
    {
        return true;
    }

    const std::string stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path lower_path = directory / ("xgdcase-" + stamp + "a");
    const std::filesystem::path upper_path = directory / ("xgdcase-" + stamp + "A");
    if (std::filesystem::exists(lower_path, error) || std::filesystem::exists(upper_path, error))
    {
        return true;
    }

    {
        std::ofstream probe(lower_path, std::ios::binary);
        if (!probe)
        {
            return true;
        }
    }

    const bool folds = std::filesystem::exists(upper_path, error);
    std::filesystem::remove(lower_path, error);
    return folds;
}

}

namespace PathCase {

std::vector<std::vector<std::string>> collisions(const std::vector<std::string>& relative_paths)
{
    std::map<std::string, size_t> group_index;
    std::vector<std::vector<std::string>> groups;
    std::vector<size_t> order;

    for (const std::string& relative_path : relative_paths)
    {
        const std::string exact = normalize(relative_path);
        if (exact.empty() || exact == ".")
        {
            continue;
        }

        const std::string folded = fold_ascii(exact);
        const auto found = group_index.find(folded);
        if (found == group_index.end())
        {
            group_index.emplace(folded, groups.size());
            groups.push_back({ exact });
            continue;
        }

        std::vector<std::string>& group = groups[found->second];
        bool seen = false;
        for (const std::string& existing : group)
        {
            if (existing == exact)
            {
                seen = true;
                break;
            }
        }
        if (!seen)
        {
            if (group.size() == 1)
            {
                order.push_back(found->second);
            }
            group.push_back(exact);
        }
    }

    std::vector<std::vector<std::string>> colliding;
    colliding.reserve(order.size());
    for (size_t index : order)
    {
        colliding.push_back(std::move(groups[index]));
    }
    return colliding;
}

void reject_host_collisions(const std::vector<std::string>& relative_paths, const std::filesystem::path& destination)
{
    const std::vector<std::vector<std::string>> groups = collisions(relative_paths);
    if (groups.empty() || !volume_folds_case(destination))
    {
        return;
    }

    std::string message = "These names differ only by case, and this output volume treats them as one file:\n";
    append_groups(message, groups);
    message += "No files were written for this input. A case-sensitive volume can hold both names.";
    throw XGDException(ErrCode::FILE_WRITE, HERE(), message);
}

void reject_archive_collisions(const std::vector<std::string>& relative_paths, std::string_view format_name)
{
    const std::vector<std::vector<std::string>> groups = collisions(relative_paths);
    if (groups.empty())
    {
        return;
    }

    std::string message = "These names differ only by case, so ";
    message.append(format_name);
    message += " cannot store both:\n";
    append_groups(message, groups);
    throw XGDException(ErrCode::FILE_WRITE, HERE(), message);
}

}
