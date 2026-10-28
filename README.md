# XGDTool

This is a command-line-only fork of [wiredopposite/XGDTool](https://github.com/wiredopposite/XGDTool). It converts original Xbox and Xbox 360 discs between the usual image formats.

Each [release](https://github.com/sharkusk/XGDTool/releases) includes a CLI binary for all three platforms:

- `XGDTool-windows` (`XGDTool.exe`)
- `XGDTool-linux`
- `XGDTool-macos`

This fork does not build or ship the graphical interface. That program remains in the original repository.

This program is still in initial testing. Report issues in this repository's Issues tab.

## Features
- Supports conversion between following formats:
    - ISO / XISO
    - Extracted files (Xex / Xbe / HDD Ready)
    - GoD / Games on Demand
    - CCI
    - CSO
    - ZAR
- Seamless conversion, e.g. you can directly extract a GoD image, convert an ISO to ZAR archive, or extracted directory to CCI archive, without writing any temporary files. The only format requiring temporary files is ZAR when provided as input.
- Image scrubbing ("Partial Scrub"), gets rid of random padding and trims the output file to the shortest length possible.
- Image reauthoring ("Full Scrub"), completely rewrites the structure of the disc for the smallest possible output file.
- Image authoring, takes your extracted files and creates a new image with them.
- Multithreaded compression for CCI and CSO formats.
- Batch processing, a folder full of different game formats can be batch converted to a single format.
- Automatically finds split files when only one part is provided as an input path, assuming they're named in this format: ```name.1.extension``` ```name.2.extension```.
- Option to select your target app/machine (Xemu, Xenia, OG Xbox, Xbox 360) and let XGDTool decide which settings to use.
- Attach XBE generation for OG Xbox.
- Online database lookup for accurate file naming (can be disabled).

## CLI Usage

On Windows the program is `XGDTool.exe`. On Linux and macOS it is `XGDTool`.

```
XGDTool <output_format> <settings_flags> <input_path> <output_directory>
```

Settings flags and output directory are optional.

### Output format arguments (mutually exclusive)
- ```--extract```   Extracts all files to a directory
- ```--xiso```      Creates an Xiso image
- ```--god```       Creates a Games on Demand image/directory structure
- ```--cci```       Creates a CCI archive (automatically split if too large for Xbox)
- ```--cso```       Creates a CSO archive (automatically split if too large for Xbox)
- ```--zar```       Creates a ZAR archive
- ```--xbe```       Generates an attach XBE file, does not convert the input file
- ```--ogxbox```    Automatically choose format and settings for use with OG Xbox
- ```--xbox360```   Automatically choose format and settings for use with Xbox 360
- ```--xemu```      Automatically choose format and settings for use with Xemu
- ```--xenia```     Automatically choose format and settings for use with Xenia

Information:
- ```--list```      List contents of input file
- ```--version```   Print version information
- ```--help```      Print usage information

### Settings flags
These arguments can be stacked, though not all output formats will use them, in that case the option is ignored. If any conflicting settings are provided (e.g. full/partial scrub), the last one will be used. 
- ```--partial-scrub```  Scrubs and trims the output image, random padding data is removed.
- ```--full-scrub```     Completely reauthor the resulting image, this will produce the smallest file possible.
- ```--split```          Splits the resulting XISO file if it's too large for OG Xbox.
- ```--rename```         Patches the title field of resulting XBE files to one found in the database.
- ```--attach-xbe```     Generates an attach XBE file along with the output file.
- ```--am-patch```       Patches the "Allowed Media" field in resulting XBE files.
- ```--offline```        Disables online functionality.
- ```--debug```          Enable debug logging.
- ```--quiet```          Disable all logging except for warnings and errors.

## Build

Releases are built with `-DENABLE_GUI=OFF`. The commands below do the same. A 32-bit build would need changes in `CMakeLists.txt` and `cmake/setup_vcpkg.cmake`.

### Windows

With CMake and MSVC installed, configuration downloads and builds the dependencies through the vcpkg checkout in this repository. The first configure takes a while.

```
git clone --recursive https://github.com/sharkusk/XGDTool.git
cd XGDTool
cmake -S . -B build -DENABLE_GUI=OFF -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Linux

Install a C++ compiler, CMake, and the libraries below. This has been built with Clang and GCC.

Ubuntu / Debian
```
sudo apt update
sudo apt-get install build-essential cmake pkg-config liblz4-dev libzstd-dev libssl-dev libcurl4-openssl-dev
```

Fedora
```
sudo dnf install gcc-c++ cmake pkgconf libzstd-devel lz4-devel openssl-devel libcurl-devel
```

```
git clone --recursive https://github.com/sharkusk/XGDTool.git
cd XGDTool
cmake -S . -B build -DENABLE_GUI=OFF
cmake --build build
```

### macOS

```
brew install cmake pkg-config lz4 zstd openssl
git clone --recursive https://github.com/sharkusk/XGDTool.git
cd XGDTool
cmake -S . -B build -DENABLE_GUI=OFF -DOPENSSL_ROOT_DIR="$(brew --prefix openssl)"
cmake --build build
```

## Notes

* On a case-insensitive filesystem (mounted NTFS, or the default APFS volume on macOS), extraction can collide when two disc entries differ only by case. A case-sensitive volume avoids that.
* Not every option has been tested on Linux and macOS. Creating a ZAR archive from a GoD folder works.
