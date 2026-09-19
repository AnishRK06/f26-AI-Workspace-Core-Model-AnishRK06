cmake_minimum_required(VERSION 3.16)
project(ai_workspace_m1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(aiws_m1
    src/document.cpp
    src/workspace.cpp
    src/text_processor.cpp
    src/chunker.cpp
    src/corpus_index.cpp
    src/retrieval_engine.cpp
    src/context_builder.cpp
    src/processing_core.cpp
)
target_include_directories(aiws_m1 PUBLIC include)
target_compile_options(aiws_m1 PRIVATE -Wall -Wextra -Wpedantic)

add_executable(public_tests tests/public_tests.cpp)
target_link_libraries(public_tests PRIVATE aiws_m1)
target_compile_options(public_tests PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME public_tests COMMAND public_tests)
