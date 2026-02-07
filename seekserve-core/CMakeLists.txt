add_library(seekserve-core STATIC
    src/error.cpp
    src/session_manager.cpp
    src/alert_dispatcher.cpp
    src/metadata_catalog.cpp
    src/byte_range_mapper.cpp
    src/piece_availability.cpp
    src/streaming_scheduler.cpp
    src/byte_source.cpp
    src/offline_cache.cpp
)

target_include_directories(seekserve-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(seekserve-core
    PUBLIC
        torrent-rasterbar
        Boost::system
        spdlog::spdlog
        unofficial::sqlite3::sqlite3
)
