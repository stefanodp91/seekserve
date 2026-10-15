cmake_minimum_required(VERSION 3.21)
project(SeekServe VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- Options ---
option(SEEKSERVE_ENABLE_WEBTORRENT "Build libtorrent with WebTorrent (WebRTC)" ON)
option(SEEKSERVE_BUILD_TESTS "Build tests" ON)
option(SEEKSERVE_BUILD_DEMO "Build demo CLI" ON)
option(SEEKSERVE_BUILD_CAPI "Build C API library" ON)
option(SEEKSERVE_CAPI_STATIC "Build C API as static library (for iOS XCFramework)" OFF)

# --- libtorrent (submodule) ---
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(deprecated-functions OFF CACHE BOOL "" FORCE)
if(SEEKSERVE_ENABLE_WEBTORRENT)
    set(webtorrent ON CACHE BOOL "" FORCE)
endif()
# seekserve's patches to libtorrent (patches/libtorrent, app BUG-202), applied
# to the submodule's sources before they are built, once: a patch already there
# is left as it is, and one that applies neither way stops the configure.
find_package(Git REQUIRED)
file(GLOB SEEKSERVE_LIBTORRENT_PATCHES "${CMAKE_CURRENT_SOURCE_DIR}/patches/libtorrent/*.patch")
list(SORT SEEKSERVE_LIBTORRENT_PATCHES)
foreach(patch IN LISTS SEEKSERVE_LIBTORRENT_PATCHES)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} apply --reverse --check ${patch}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/extern/libtorrent
        RESULT_VARIABLE patch_there OUTPUT_QUIET ERROR_QUIET)
    if(NOT patch_there EQUAL 0)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} apply ${patch}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/extern/libtorrent
            RESULT_VARIABLE patch_applied)
        if(NOT patch_applied EQUAL 0)
            message(FATAL_ERROR "seekserve: ${patch} does not apply to extern/libtorrent")
        endif()
        message(STATUS "seekserve: applied ${patch}")
    endif()
endforeach()

add_subdirectory(extern/libtorrent EXCLUDE_FROM_ALL)

# --- vcpkg-managed dependencies ---
find_package(Boost REQUIRED COMPONENTS system)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

# --- Subdirectories ---
add_subdirectory(seekserve-core)
add_subdirectory(seekserve-serve)

if(SEEKSERVE_BUILD_CAPI)
    add_subdirectory(seekserve-capi)
endif()

if(SEEKSERVE_BUILD_DEMO)
    add_subdirectory(seekserve-demo)
endif()

if(SEEKSERVE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
