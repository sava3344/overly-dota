cmake_minimum_required(VERSION 3.20)
project(OverlayLua LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Lua REQUIRED)

add_library(overlay_lua SHARED src/overlay_lua.cpp)
target_include_directories(overlay_lua PRIVATE ${LUA_INCLUDE_DIR})
target_link_libraries(overlay_lua PRIVATE ${LUA_LIBRARIES} user32 gdi32 comdlg32)
target_compile_definitions(overlay_lua PRIVATE UNICODE _UNICODE)
target_compile_options(overlay_lua PRIVATE /utf-8)

# Static CRT: no VC++ Redistributable needed on the target machine
set_property(TARGET overlay_lua PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
