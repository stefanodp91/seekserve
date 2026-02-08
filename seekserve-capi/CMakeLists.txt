if(SEEKSERVE_CAPI_STATIC)
    add_library(seekserve-capi STATIC
        src/seekserve_c.cpp
    )
else()
    add_library(seekserve-capi SHARED
        src/seekserve_c.cpp
    )
endif()

target_include_directories(seekserve-capi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(seekserve-capi
    PRIVATE
        seekserve-core
        seekserve-serve
)

set_target_properties(seekserve-capi PROPERTIES
    OUTPUT_NAME "seekserve"
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
)
