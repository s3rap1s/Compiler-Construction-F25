cmake_minimum_required(VERSION 3.16)
project(ILangCompiler CXX)

# Set the default build type
if(NOT CMAKE_BUILD_TYPE)
    set(default_build_type "Debug")
    set(CMAKE_BUILD_TYPE ${default_build_type} CACHE STRING "Choose the build type." FORCE)
endif()
message(STATUS "${CMAKE_PROJECT_NAME} Build Type: ${CMAKE_BUILD_TYPE}")

# C++ standard settings
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler options
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

set(LLVM_INCLUDE_DIRS "/usr/include/llvm")
set(LLVM_LIBRARIES LLVM-21)

# main program
add_executable(main
    lexer/lexer.cpp
    lexer/token_printer.cpp
    lexer/iterator.cpp

    parser/parser.cpp
    parser/syntax_error.cpp
    parser/tree_printer.cpp
    parser/ast.cpp

    analyzer/analyzer.cpp
    analyzer/symbol_table.cpp
    analyzer/constexpr_calc.cpp

    codegen/codegen.cpp

    main.cpp
)

# setup
target_include_directories(main PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${LLVM_INCLUDE_DIRS}
)

# Link with LLVM
target_link_libraries(main ${LLVM_LIBRARIES})
