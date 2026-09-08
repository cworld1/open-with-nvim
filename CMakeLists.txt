cmake_minimum_required(VERSION 3.21)
project(open_with_nvim VERSION 1.1.0 LANGUAGES C)

if(NOT WIN32)
    message(FATAL_ERROR "open-with-nvim requires a Windows-targeting compiler (MinGW or MSVC).")
endif()

enable_language(RC)
add_executable(nvim WIN32 main.c app.rc)
set_target_properties(nvim PROPERTIES
    C_STANDARD 23
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF
)
target_compile_definitions(nvim PRIVATE UNICODE _UNICODE)
target_include_directories(nvim PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(nvim PRIVATE shell32 user32)

if(MSVC)
    target_compile_options(nvim PRIVATE $<$<COMPILE_LANGUAGE:C>:/W4>)
else()
    target_compile_options(nvim PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra>)
    if(MINGW)
        target_link_options(nvim PRIVATE -municode)
    endif()
endif()
