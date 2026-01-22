cmake_minimum_required(VERSION 3.13)
project(yyjson LANGUAGES C)

# Use STATIC (or OBJECT if you prefer no .lib/.a file)
add_library(yyjson STATIC
    yyjson.c
)

# Public includes so consumers can #include "yyjson.h"
target_include_directories(yyjson
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Optional but good practice: only expose necessary symbols if building shared (not needed for STATIC)
# target_compile_definitions(yyjson PRIVATE YYJSON_EXPORT=)

# If you want an alias (common style)
add_library(yyjson::yyjson ALIAS yyjson)