cmake_minimum_required(VERSION 3.16)
project(ILangCompiler CXX)

# Set the default build type
if(NOT CMAKE_BUILD_TYPE)
    set(default_build_type "Debug")

    message(STATUS "${CMAKE_PROJECT_NAME} Set the build type to `${default_build_type}` as none was specified.")
    set(CMAKE_BUILD_TYPE ${default_build_type} CACHE STRING "Choose the build type." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
message(STATUS "${CMAKE_PROJECT_NAME} Build Type: ${CMAKE_BUILD_TYPE}")

# options
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra)

# main program
add_executable(main
    lexer/lexer.cpp
    lexer/token_printer.cpp
    lexer/iterator.cpp

    parser/parser.cpp
    parser/syntax_error.cpp
    parser/tree_printer.cpp

    analyzer/analyzer.cpp
    analyzer/symbol_table.cpp

    main.cpp
)

# setup
target_include_directories(main PRIVATE
    ${CMAKE_SOURCE_DIR}
)
