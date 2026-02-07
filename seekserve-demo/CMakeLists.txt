add_executable(seekserve-demo
    src/main.cpp
)

target_link_libraries(seekserve-demo
    PRIVATE
        seekserve-core
        seekserve-serve
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)
