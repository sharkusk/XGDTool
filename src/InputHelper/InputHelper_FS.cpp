#include <algorithm>

#include "Utils/StringUtils.h"
#include "InputHelper/InputHelper.h"
#include "Stfs/StfsReader.h"

bool InputHelper::has_extension(const std::filesystem::path& path, const std::string& extension) 
{
    if (path.filename().string().size() > extension.size()) 
    {
        return StringUtils::case_insensitive_search(path.filename().string(), extension);
    }
    return false;
}

bool InputHelper::is_extracted_dir(const std::filesystem::path& path) 
{
    bool exe_found = false; 
    
    for (const auto& entry : std::filesystem::directory_iterator(path)) 
    {
        if (entry.is_regular_file()) 
        {
            if (has_extension(entry.path(), ".xbe")) 
            {
                exe_found = true;
            }
            else if (has_extension(entry.path(), ".xex")) 
            {
                exe_found = true;
            }
            else if (has_extension(entry.path(), ".iso") ||
                     has_extension(entry.path(), ".cso") ||
                     has_extension(entry.path(), ".cci"))
            {
                return false;
            }
        }
    }
    return exe_found;
}

bool InputHelper::is_god_dir_helper(const std::filesystem::path& path, int current_depth, int max_depth) 
{
    if (current_depth > max_depth) 
    {
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(path)) 
    {
        if (entry.is_regular_file() && entry.path().filename().string().rfind("Data", 0) == 0) 
        {
            if (entry.path().parent_path().extension() == ".data") 
            {
                return true;
            }
        } 
        else if (entry.is_directory()) 
        {
            if (is_god_dir_helper(entry.path(), current_depth + 1, max_depth)) 
            {
                return true;
            }
        }
    }
    return false;
}

bool InputHelper::is_god_dir(const std::filesystem::path& path) 
{
    return is_god_dir_helper(path, 0, 2);
}

FileType InputHelper::get_filetype(const std::filesystem::path& path) 
{
    if (std::filesystem::is_directory(path)) 
    {
        if (is_extracted_dir(path)) 
        {
            return FileType::DIR;
        } 
        else if (is_god_dir(path)) 
        {
            return FileType::GoD;
        }
        else if (StfsReader::find_packages(path, 6).size() == 1)
        {
            return FileType::XBLA;
        }
    }
    else if (std::filesystem::is_regular_file(path))
    {
        if (has_extension(path, ".iso")) 
        {
            return FileType::ISO;
        } 
        else if (has_extension(path, ".zar")) 
        {
            return FileType::ZAR;
        } 
        else if (has_extension(path, ".cso")) 
        {
            return FileType::CSO;
        } 
        else if (has_extension(path, ".cci")) 
        {
            return FileType::CCI;
        } 
        else if (has_extension(path, ".xbe")) 
        {
            return FileType::XBE;
        }
        else if (StfsReader::is_package(path))
        {
            return FileType::XBLA;
        }
    }
    return FileType::UNKNOWN;
}

std::vector<std::filesystem::path> InputHelper::find_split_filepaths(const std::filesystem::path& in_filepath) 
{
    std::string extension = in_filepath.extension().string();
    std::filesystem::path stem = in_filepath.stem();
    std::string stem_str = stem.string();

    if (stem_str.size() > 2) 
    {
        std::string subextension = stem_str.substr(stem_str.size() - 2);
        if (subextension == ".1" || subextension == ".2") 
        {
            std::string base_filename = stem_str.substr(0, stem_str.size() - 2);
            std::filesystem::path base_path = in_filepath.parent_path() / base_filename;

            if (subextension == ".1") 
            {
                std::filesystem::path in_filepath1 = in_filepath;
                std::filesystem::path in_filepath2 = base_path.string() + ".2" + extension;
                if (std::filesystem::exists(in_filepath2)) 
                {
                    return { in_filepath1, in_filepath2 };
                }
            } 
            else if (subextension == ".2") 
            {
                std::filesystem::path in_filepath1 = base_path.string() + ".1" + extension;
                std::filesystem::path in_filepath2 = in_filepath;
                if (std::filesystem::exists(in_filepath1)) 
                {
                    return { in_filepath1, in_filepath2 };
                } 
                else 
                {
                    throw XGDException(ErrCode::FS_EXISTS, HERE(), "Missing first part of split file: " + in_filepath.string());
                }
            }
        }
    }
    return { in_filepath };
}

bool InputHelper::is_part_2_file(const std::filesystem::path& path) 
{
    std::string stem_str = path.stem().string();

    if (stem_str.size() > 2 && stem_str.substr(stem_str.size() - 2) == ".2") 
    {
        return true;
    }
    return false;
}

std::filesystem::path InputHelper::get_output_path(const std::filesystem::path& out_directory, TitleHelper& title_helper, const InputInfo& input_info)
{
    if (output_settings_.source_name && (output_settings_.file_type == FileType::ISO || output_settings_.file_type == FileType::ZAR))
    {
        const std::filesystem::path& source = input_info.source_path.empty() ? input_info.paths.front() : input_info.source_path;
        std::filesystem::path directory = output_directory_specified_ ? out_directory : source.parent_path();
        std::error_code error;
        std::string base = std::filesystem::is_directory(source, error) ? source.filename().string() : source.stem().string();
        std::string extension = output_settings_.file_type == FileType::ISO ? ".xiso.iso" : ".zar";
        return directory / (base + extension);
    }

    std::filesystem::path out_path = out_directory;

    switch (output_settings_.file_type)
    {
        case FileType::DIR:
            out_path /= title_helper.folder_name();
            break;
        case FileType::ISO:
            if (!output_settings_.xemu_paths) 
            {
                out_path /= title_helper.folder_name();
            } 
            out_path /= title_helper.iso_name() + ".iso";
            break;
        case FileType::CCI:
            out_path /= title_helper.folder_name();
            out_path /= title_helper.iso_name() + ".cci";
            break;
        case FileType::CSO:
            out_path /= title_helper.folder_name();
            out_path /= title_helper.iso_name() + ".cso";
            break;
        case FileType::ZAR:
            out_path /= title_helper.iso_name() + ".zar";
            break;
        case FileType::XBE:
            out_path /= "default.xbe";
            break;
        case FileType::GoD:
            out_path /= title_helper.god_folder_name();
            break;
        default:
            throw XGDException(ErrCode::MISC, HERE(), "Invalid output file type");
    }
    return out_path;
}

OutputSettings InputHelper::get_auto_output_settings(const AutoFormat auto_format) 
{
    OutputSettings output_settings;

    switch (auto_format) 
    {
        case AutoFormat::OGXBOX:
            output_settings.file_type = FileType::DIR;
            output_settings.allowed_media_patch = true;
            output_settings.rename_xbe = true;
            break;
        case AutoFormat::XBOX360:
            output_settings.file_type = FileType::GoD;
            output_settings.scrub_type = ScrubType::FULL;
            break;
        case AutoFormat::XENIA:
        case AutoFormat::XEMU:
            output_settings.file_type = FileType::ISO;
            output_settings.scrub_type = ScrubType::FULL;
            output_settings.scrub_fallback = true;
            output_settings.attach_xbe = false;
            output_settings.allowed_media_patch = false;
            output_settings.split = false;
            output_settings.xemu_paths = true;
            break;
        default:
            break;
    }

    return output_settings;
}

void InputHelper::remove_duplicate_infos(std::vector<InputInfo>& input_infos)
{
    if (input_infos.size() < 2)
    {
        return;
    }

    std::sort(input_infos.begin(), input_infos.end(), [](const InputInfo& a, const InputInfo& b) 
    {
        return a.paths.front() < b.paths.front();
    });

    auto it = input_infos.begin();

    while (it != input_infos.end() - 1) 
    {
        if (it->paths.front() == (it + 1)->paths.front()) 
        {
            it = input_infos.erase(it);
        } 
        else 
        {
            ++it;
        }
    }
}