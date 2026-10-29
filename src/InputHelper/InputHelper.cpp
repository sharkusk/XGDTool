#include "ImageReader/ImageReader.h"
#include "InputHelper/InputHelper.h"
#include "Executable/AttachXbeTool.h"
#include "Stfs/StfsReader.h"
#include "Zip/ZipArchive.h"

namespace {

struct TempCleanup
{
    std::filesystem::path path;
    ~TempCleanup()
    {
        if (path.empty())
        {
            return;
        }
        std::error_code error;
        std::filesystem::remove_all(path, error);
        if (path.has_parent_path() && path.parent_path().filename() == "_temp")
        {
            std::filesystem::remove(path.parent_path(), error);
        }
    }
};

}

InputHelper::InputHelper(std::filesystem::path in_path, std::filesystem::path out_directory, OutputSettings output_settings)
    :   output_directory_(out_directory), 
        output_settings_((output_settings.auto_format != AutoFormat::NONE) ? get_auto_output_settings(output_settings.auto_format) : output_settings),
        output_directory_specified_(!out_directory.empty())
{
    if (!output_directory_specified_ && !uses_source_destination())
    {
        output_directory_ = in_path.parent_path() / "XGDTool_Output";
    }

    if (!output_directory_.empty())
    {
        output_directory_ = std::filesystem::absolute(output_directory_);
    }
    add_input(in_path, true);
}

InputHelper::InputHelper(std::vector<std::filesystem::path> in_paths, std::filesystem::path out_directory, OutputSettings output_settings)
    :   output_directory_(out_directory), 
        output_settings_((output_settings.auto_format != AutoFormat::NONE) ? get_auto_output_settings(output_settings.auto_format) : output_settings),
        output_directory_specified_(!out_directory.empty())
{
    if (!output_directory_specified_ && !uses_source_destination())
    {
        output_directory_ = in_paths.front().parent_path() / "XGDTool_Output";
    }

    if (!output_directory_.empty())
    {
        output_directory_ = std::filesystem::absolute(output_directory_);
    }

    for (const auto& in_path : in_paths) 
    {
        add_input(in_path, true);
    }
}

void InputHelper::push_input(FileType file_type, const std::filesystem::path& path)
{
    InputInfo info;
    info.file_type = file_type;

    if (file_type == FileType::XBLA && std::filesystem::is_directory(path))
    {
        std::vector<std::filesystem::path> packages = StfsReader::find_packages(path, 6);
        if (packages.size() != 1)
        {
            return;
        }
        info.paths = { packages.front() };
        info.name_hint = path.filename();
    }
    else if (std::filesystem::is_regular_file(path))
    {
        info.paths = find_split_filepaths(path);
        if (file_type == FileType::XBLA)
        {
            info.name_hint = path.stem();
        }
    }
    else
    {
        info.paths = { path };
    }

    info.source_path = naming_source_.empty() ? path : naming_source_;
    input_infos_.push_back(std::move(info));
}

bool InputHelper::uses_source_destination() const
{
    return output_settings_.auto_format == AutoFormat::NONE && output_settings_.file_type == FileType::UNKNOWN;
}

void InputHelper::add_input(const std::filesystem::path& in_path, bool top_level) 
{
    if (in_path.empty() || !std::filesystem::exists(in_path)) 
    {
        return;
    }

    if (top_level && !output_directory_specified_ && output_directory_.empty())
    {
        FileType type = get_filetype(in_path);
        bool batch = std::filesystem::is_directory(in_path) && type == FileType::UNKNOWN;
        output_directory_ = std::filesystem::absolute(batch ? in_path : in_path.parent_path());
    }

    if (std::filesystem::is_regular_file(in_path) && ZipArchive::is_zip(in_path))
    {
        std::filesystem::path saved_naming = naming_source_;
        if (naming_source_.empty())
        {
            naming_source_ = in_path;
        }

        std::filesystem::path dest = output_directory_ / "_zip" / in_path.stem();
        int suffix = 2;
        while (std::filesystem::exists(dest))
        {
            dest = output_directory_ / "_zip" / (in_path.stem().string() + "-" + std::to_string(suffix));
            ++suffix;
        }

        try
        {
            std::filesystem::create_directories(dest);
        }
        catch (const std::filesystem::filesystem_error& error)
        {
            throw XGDException(ErrCode::FS_MKDIR, HERE(), error.what());
        }

        ZipArchive::extract(in_path, dest);
        temp_directories_.push_back(dest);
        add_input(dest);
        naming_source_ = saved_naming;
        return;
    }

    FileType in_file_type = get_filetype(in_path);

    if (in_file_type == FileType::UNKNOWN) 
    {
        if (!std::filesystem::is_directory(in_path))
        {
            return;
        }

        bool added = false;
        for (const auto& entry : std::filesystem::directory_iterator(in_path)) 
        {
            if (entry.is_regular_file() && ZipArchive::is_zip(entry.path()))
            {
                add_input(entry.path());
                added = true;
                continue;
            }

            FileType child_type = get_filetype(entry.path());
            if (child_type == FileType::UNKNOWN)
            {
                continue;
            }
            if (entry.is_regular_file() && !is_part_2_file(entry.path()))
            {
                push_input(child_type, entry.path());
                added = true;
            }
            else if (entry.is_directory())
            {
                push_input(child_type, entry.path());
                added = true;
            }
        }
        if (added)
        {
            remove_duplicate_infos(input_infos_);
        }
        return;
    }

    push_input(in_file_type, in_path);
    remove_duplicate_infos(input_infos_);
}

void InputHelper::process_all() 
{
    failed_inputs_.clear();

    for (auto& input_info : input_infos_) 
    {
        process_single(input_info);
    }

    for (const auto& temp_directory : temp_directories_)
    {
        std::error_code error;
        std::filesystem::remove_all(temp_directory, error);
        if (temp_directory.has_parent_path() && temp_directory.parent_path().filename() == "_zip")
        {
            std::filesystem::remove(temp_directory.parent_path(), error);
        }
    }
    temp_directories_.clear();
}

void InputHelper::process_single(InputInfo input_info)
{
    OutputSettings saved_settings = output_settings_;

    try 
    {
        if (output_settings_.auto_format == AutoFormat::NONE && output_settings_.file_type == FileType::UNKNOWN)
        {
            infer_default_output(input_info);
        }

        XGDLog() << "Processing: " << input_info.paths.front().string() + ((input_info.paths.size() > 1) ? (" and " + input_info.paths.back().string()) : "") << "\n";
        
        std::vector<std::filesystem::path> out_paths;

        switch (output_settings_.file_type) 
        {
            case FileType::UNKNOWN:
                throw XGDException(ErrCode::ISO_INVALID, HERE(), "Unknown output file type");
            case FileType::DIR:
                out_paths = create_dir(input_info);
                break;
            case FileType::XBE:
                out_paths = create_attach_xbe(input_info);
                break;
            case FileType::LIST:
                list_files(input_info);
                break;
            default:
                out_paths = create_image(input_info);
                break;
        } 

        if (!out_paths.empty())
        {
            XGDLog() << "Successfully created: " << out_paths.front().string() + ((out_paths.size() > 1) ? (" and " + out_paths.back().string()) : "") << "\n";
        }
    } 
    catch (const XGDException& e) 
    {
        reset_processor();
        failed_inputs_.insert(failed_inputs_.end(), input_info.paths.begin(), input_info.paths.end());
        XGDLog(Error) << e.what() << "\n";
    }
    catch (const std::exception& e) 
    {
        reset_processor();
        failed_inputs_.insert(failed_inputs_.end(), input_info.paths.begin(), input_info.paths.end());
        XGDLog(Error) << e.what() << "\n";
    }

    output_settings_ = saved_settings;
}

std::vector<std::filesystem::path> InputHelper::create_image(InputInfo& input_info)
{
    TempCleanup cleanup;

    if (input_info.file_type == FileType::ZAR) 
    {
        cleanup.path = extract_temp_zar(input_info.paths.front());
        input_info.paths = { cleanup.path };
        input_info.file_type = FileType::DIR;
    }
    else if (input_info.file_type == FileType::XBLA)
    {
        std::filesystem::path hint = input_info.name_hint.empty() ? input_info.paths.front().stem() : input_info.name_hint;
        cleanup.path = output_directory_ / "_temp" / hint;
        try
        {
            std::filesystem::create_directories(cleanup.path);
        }
        catch (const std::filesystem::filesystem_error& error)
        {
            throw XGDException(ErrCode::FS_MKDIR, HERE(), error.what());
        }
        StfsReader reader(input_info.paths.front());
        reader.extract(cleanup.path);
        input_info.paths = { cleanup.path };
        input_info.file_type = FileType::DIR;
    }
    else if (input_info.file_type == FileType::XBE)
    {
        throw XGDException(ErrCode::ISO_INVALID, HERE(), "Cannot create image from XBE file");
    }

    std::unique_ptr<TitleHelper> title_helper;
    std::shared_ptr<ImageReader> image_reader;

    switch (input_info.file_type) 
    {
        case FileType::DIR:
            title_helper = std::make_unique<TitleHelper>(input_info.paths.front(), output_settings_.offline_mode);
            break;
        default:
            image_reader = ImageReader::create_instance(input_info.file_type, input_info.paths);
            title_helper = std::make_unique<TitleHelper>(image_reader, output_settings_.offline_mode);
            break;
    }

    std::filesystem::path out_path = get_output_path(output_directory_, *title_helper, input_info);

    switch (input_info.file_type) 
    {
        case FileType::DIR:
            image_writer_ = ImageWriter::create_instance(input_info.paths.front(), *title_helper, output_settings_);
            break;
        default:
            image_writer_ = ImageWriter::create_instance(image_reader, *title_helper, output_settings_);
            break;
    }

    std::vector<std::filesystem::path> final_out_paths = image_writer_->convert(out_path);
    
    reset_processor();

    if (output_settings_.attach_xbe && title_helper->platform() == Platform::OGX)
    {
        AttachXbeTool attach_xbe_tool(*title_helper);
        attach_xbe_tool.generate_attach_xbe(final_out_paths.front().parent_path() / "default.xbe");
    }

    return final_out_paths;
}

std::vector<std::filesystem::path> InputHelper::create_dir(const InputInfo& input_info)
{
    if (input_info.file_type == FileType::DIR)
    {
        throw XGDException(ErrCode::ISO_INVALID, HERE(), "Cannot create directory from directory");
    }
    else if (input_info.file_type == FileType::XBE)
    {
        throw XGDException(ErrCode::ISO_INVALID, HERE(), "Cannot extract XBE file");
    }
    else if (input_info.file_type == FileType::ZAR)
    {
        zar_extractor_ = std::make_unique<ZARExtractor>(input_info.paths.front());
        zar_extractor_->extract(output_directory_ / input_info.paths.front().stem());
        reset_processor();
        return { output_directory_ / input_info.paths.front().stem() };
    }
    else if (input_info.file_type == FileType::XBLA)
    {
        std::filesystem::path name = input_info.name_hint.empty() ? input_info.paths.front().stem() : input_info.name_hint;
        std::filesystem::path out_path = output_directory_ / name;
        StfsReader reader(input_info.paths.front());
        reader.extract(out_path);
        return { out_path };
    }

    std::shared_ptr<ImageReader> image_reader = ImageReader::create_instance(input_info.file_type, input_info.paths);

    TitleHelper title_helper(image_reader, output_settings_.offline_mode);

    std::filesystem::path out_path = get_output_path(output_directory_, title_helper, input_info);

    image_extractor_ = std::make_unique<ImageExtractor>(*image_reader, title_helper, output_settings_.allowed_media_patch, output_settings_.rename_xbe);
    image_extractor_->extract(out_path);
    reset_processor();

    return { out_path };
}

std::vector<std::filesystem::path> InputHelper::create_attach_xbe(const InputInfo& input_info)
{
    if (input_info.file_type == FileType::DIR || input_info.file_type == FileType::ZAR || input_info.file_type == FileType::XBE || input_info.file_type == FileType::XBLA)
    {
        throw XGDException(ErrCode::ISO_INVALID, HERE(), "Cannot create attach XBE from input type");
    }

    std::shared_ptr<ImageReader> image_reader = ImageReader::create_instance(input_info.file_type, input_info.paths);

    if (image_reader->platform() != Platform::OGX)
    {
        throw XGDException(ErrCode::ISO_INVALID, HERE(), "Attach XBE can only be created for OGX images");
    }

    TitleHelper title_helper(image_reader, output_settings_.offline_mode);

    std::filesystem::path out_path = get_output_path(input_info.paths.front().parent_path(), title_helper, input_info);

    AttachXbeTool attach_xbe_tool(title_helper); 
    attach_xbe_tool.generate_attach_xbe(out_path);

    return { out_path };
}

void InputHelper::list_files(const InputInfo& input_info) 
{
    if (input_info.file_type == FileType::DIR) 
    {
        throw XGDException(ErrCode::ISO_INVALID, HERE(), "Cannot list files from directory");
    }

    XGDLog() << "Files in image:\n";

    if (input_info.file_type == FileType::ZAR) 
    {
        ZARExtractor zar_extractor(input_info.paths.front());
        zar_extractor.list_files();
        return;
    }

    if (input_info.file_type == FileType::XBLA)
    {
        StfsReader reader(input_info.paths.front());
        reader.list();
        return;
    }

    std::shared_ptr<ImageReader> image_reader = ImageReader::create_instance(input_info.file_type, input_info.paths);

    for (const auto& entry : image_reader->directory_entries()) 
    {
        if ((entry.header.attributes & Xiso::ATTRIBUTE_DIRECTORY) || entry.path.empty())
        {
            continue;
        }

        XGDLog() << entry.path.string() << " (" << entry.header.file_size << " bytes)\n";
    }
}

std::filesystem::path InputHelper::extract_temp_zar(const std::filesystem::path& in_path)
{
    std::filesystem::path temp_path = output_directory_ / "_temp";

    try 
    {
        std::filesystem::create_directories(temp_path);
    } 
    catch (const std::filesystem::filesystem_error& e) 
    {
        XGDException(ErrCode::FS_MKDIR, HERE(), e.what());
    }
    
    ZARExtractor zar_extractor(in_path);
    zar_extractor.extract(temp_path);

    return temp_path;
}

void InputHelper::cancel_processing() 
{
    if (image_writer_) 
    {
        image_writer_->cancel_processing();
    }
    if (image_extractor_) 
    {
        image_extractor_->cancel_processing();
    }
    if (zar_extractor_) 
    {
        zar_extractor_->cancel_processing();
    }
}

void InputHelper::pause_processing() 
{
    if (image_writer_) 
    {
        image_writer_->pause_processing();
    }
    if (image_extractor_) 
    {
        image_extractor_->pause_processing();
    }
    if (zar_extractor_) 
    {
        zar_extractor_->pause_processing();
    }
}

void InputHelper::resume_processing() 
{
    if (image_writer_) 
    {
        image_writer_->resume_processing();
    }
    if (image_extractor_) 
    {
        image_extractor_->resume_processing();
    }
    if (zar_extractor_) 
    {
        zar_extractor_->resume_processing();
    }
}

void InputHelper::infer_default_output(const InputInfo& input_info)
{
    Platform platform = platform_of(input_info);

    if (platform == Platform::X360)
    {
        output_settings_.file_type = FileType::ZAR;
        output_settings_.source_name = true;
        XGDLog() << "Default output: ZAR" << XGDLog::Endl;
        return;
    }

    if (platform == Platform::OGX)
    {
        output_settings_.file_type = FileType::ISO;
        output_settings_.source_name = true;
        if (output_settings_.scrub_type == ScrubType::NONE)
        {
            output_settings_.scrub_type = ScrubType::FULL;
            output_settings_.scrub_fallback = true;
        }
        const char* scrub_name = output_settings_.scrub_type == ScrubType::PARTIAL ? "partial-scrub" : "full-scrub";
        XGDLog() << "Default output: " << scrub_name << " XISO" << XGDLog::Endl;
        return;
    }

    throw XGDException(ErrCode::ISO_INVALID, HERE(), "Cannot choose an output format for this input");
}

Platform InputHelper::platform_of(const InputInfo& input_info)
{
    switch (input_info.file_type)
    {
        case FileType::XBLA:
        case FileType::GoD:
            return Platform::X360;
        case FileType::XBE:
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Cannot create an image from an XBE file");
        case FileType::ZAR:
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Pass an output format for a ZAR input");
        case FileType::DIR:
        {
            bool found_xex = false;
            bool found_xbe = false;
            for (const auto& entry : std::filesystem::directory_iterator(input_info.paths.front()))
            {
                if (!entry.is_regular_file())
                {
                    continue;
                }
                if (has_extension(entry.path(), ".xex"))
                {
                    found_xex = true;
                }
                else if (has_extension(entry.path(), ".xbe"))
                {
                    found_xbe = true;
                }
            }
            if (found_xex)
            {
                return Platform::X360;
            }
            if (found_xbe)
            {
                return Platform::OGX;
            }
            throw XGDException(ErrCode::MISC, HERE(), "Unknown platform");
        }
        case FileType::ISO:
        case FileType::CCI:
        case FileType::CSO:
        {
            std::shared_ptr<ImageReader> image_reader = ImageReader::create_instance(input_info.file_type, input_info.paths);
            return image_reader->platform();
        }
        default:
            throw XGDException(ErrCode::ISO_INVALID, HERE(), "Cannot choose an output format for this input");
    }
}

void InputHelper::reset_processor() 
{
    if (image_writer_) 
    {
        image_writer_.reset();
    }
    if (image_extractor_) 
    {
        image_extractor_.reset();
    }
    if (zar_extractor_) 
    {
        zar_extractor_.reset();
    }
}