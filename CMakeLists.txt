cmake_minimum_required(VERSION 3.15)
project(Fast365 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(fast365
    src/main.cpp
    src/docx.cpp
    src/xml.cpp
    src/zip.cpp
    src/inflate.cpp
    src/html_util.cpp
)

if(MSVC)
    target_compile_options(fast365 PRIVATE /W4)
    target_compile_definitions(fast365 PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_options(fast365 PRIVATE /STACK:8388608)
else()
    target_compile_options(fast365 PRIVATE -Wall -Wextra)
endif()
