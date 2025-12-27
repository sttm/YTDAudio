cmake_minimum_required(VERSION 3.16)

# ImGui library
add_library(imgui STATIC
    imgui.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    backends/imgui_impl_sdl2.cpp
    backends/imgui_impl_sdlrenderer2.cpp
)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/backends
)

# Find SDL2
find_package(SDL2 REQUIRED)

# Link SDL2
target_link_libraries(imgui PUBLIC SDL2::SDL2)

# Set properties
set_target_properties(imgui PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)


