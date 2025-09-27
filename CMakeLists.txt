cmake_minimum_required(VERSION 3.28)

# project / version
set(PROJECT_NAME "RedumperReport" CACHE STRING "Project name")
string(TIMESTAMP version_major "%Y")
string(TIMESTAMP version_minor "%m")
string(TIMESTAMP version_patch "%d")
set(PROJECT_VERSION_MAJOR ${version_major} CACHE STRING "Major version number")
set(PROJECT_VERSION_MINOR ${version_minor} CACHE STRING "Minor version number")
set(PROJECT_VERSION_PATCH ${version_patch} CACHE STRING "Patch version number")

set(PROJECT_VERSION_BUILD "LOCAL" CACHE STRING "Version patch")
project(${PROJECT_NAME} LANGUAGES CXX VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

set(CLANG_LINK_OPTIONS "" CACHE STRING "Extra LLVM/Clang linker options")

# build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()
set(CMAKE_CONFIGURATION_TYPES ${CMAKE_BUILD_TYPE} CACHE INTERNAL "Active configuration" FORCE)

# packaging
set(CPACK_GENERATOR "ZIP")
# add build number to a package file name if it was provided
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(system_suffix__ "arm64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(system_suffix__ "x64")
else()
    set(system_suffix__ "x86")
endif()
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}_build${PROJECT_VERSION_BUILD}-${CMAKE_SYSTEM_NAME}-${system_suffix__}")
include(CPack)

# platform
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PROJECT_TARGET_LINUX 1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PROJECT_TARGET_WINDOWS 1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PROJECT_TARGET_MACOSX 1)
else()
    message(FATAL_ERROR "Unknown target platform")
endif()

# C/C++
set(CMAKE_CXX_STANDARD 20)
add_definitions(-DPROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
                -DPROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
                -DPROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
                -DPROJECT_VERSION_BUILD=${PROJECT_VERSION_BUILD})

option(CLANG_USE_LIBCPP "Link to libc++ instead of system default" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(CLANG_USE_LIBCPP)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
        add_link_options(-stdlib=libc++)
    endif()

    add_link_options(${CLANG_LINK_OPTIONS})
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(-Wall -Wextra -Werror)

    # remove after https://gcc.gnu.org/bugzilla/show_bug.cgi?id=99426 is fixed
    add_compile_options(--param=ggc-min-expand=10000)
endif()
if(MSVC)
    # disable modules inlining
    # wait until Microsoft actually fixes this and remove the option, more information:
    # https://developercommunity.visualstudio.com/t/VS-1750-Compiler-is-Broken/10288409
    # https://developercommunity.visualstudio.com/t/Modules-ICE-when-using-cout-inside-of-/10299789
    # https://developercommunity.visualstudio.com/t/C-Modules:-Internal-Compiler-Error-on-/10377473
    add_compile_options(/dxifcInlineFunctions-)

    # build MT configuration by default
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    # multithreaded build
    add_compile_options(/MP)

    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -D_NTSCSI_USER_MODE_)

    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    # flatten source lists
    file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)
    foreach(i IN LISTS files)
        get_filename_component(source_path "${i}" PATH)
        string(REPLACE "/" "\\" source_path "${source_path}")
        source_group("${source_path}" FILES ${i})
    endforeach()
endif()

add_executable(redumper_report)
target_sources(redumper_report
    PUBLIC
    "main.cc"
	"redumper/utils/throw_line.hh"
    "redumper_report.manifest"
    PRIVATE FILE_SET cxx_modules TYPE CXX_MODULES FILES
    "common.ixx"
    "cd_common.ixx"
    "dvd_common.ixx"
    "redumper/cd/cd.ixx"
    "redumper/cd/cdrom.ixx"
    "redumper/cd/scrambler.ixx"
    "redumper/cd/subcode.ixx"
    "redumper/cd/toc.ixx"
    "redumper/crc/crc.ixx"
    "redumper/crc/crc16_gsm.ixx"
    "redumper/filesystem/iso9660/iso9660.ixx"
    "redumper/filesystem/iso9660/iso9660_browser.ixx"
    "redumper/filesystem/iso9660/iso9660_defs.ixx"
    "redumper/filesystem/iso9660/iso9660_entry.ixx"
    "redumper/filesystem/iso9660/iso9660_map.ixx"
    "redumper/hash/block_hasher.ixx"
    "redumper/hash/sha1.ixx"
    "redumper/readers/image_bin_reader.ixx"
    "redumper/readers/image_iso_reader.ixx"
    "redumper/readers/image_scram_reader.ixx"
    "redumper/readers/image_simple_reader.ixx"
    "redumper/readers/data_reader.ixx"
    "redumper/scsi/mmc.ixx"
    "redumper/utils/endian.ixx"
    "redumper/utils/file_io.ixx"
    "redumper/utils/hex_bin.ixx"
    "redumper/utils/logger.ixx"
    "redumper/utils/misc.ixx"
    "redumper/utils/strings.ixx"
    "redumper_report.ixx"
)

target_include_directories(redumper_report
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
	"redumper"
	"redumper/utils"
)

if(PROJECT_TARGET_MACOSX)
    find_library(CORE_FOUNDATION CoreFoundation REQUIRED)
    find_library(IO_KIT IOKit REQUIRED)
    set(libs ${libs}
        ${CORE_FOUNDATION}
        ${IO_KIT}
    )
endif()

install(TARGETS redumper_report DESTINATION "bin")

