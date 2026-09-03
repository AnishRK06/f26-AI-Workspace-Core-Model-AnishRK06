cmake_minimum_required(VERSION 3.10)
project(AIWorkspaceM0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(aiworkspace
    src/Document.cpp
    src/Prompt.cpp
    src/Message.cpp
    src/Workspace.cpp
)
target_include_directories(aiworkspace PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(aiworkspace PRIVATE -Wall -Wextra -Wpedantic)

add_executable(main src/main.cpp)
target_link_libraries(main PRIVATE aiworkspace)

add_executable(testworkspace tests/testworkspace.cpp)
target_link_libraries(testworkspace PRIVATE aiworkspace)

enable_testing()
add_test(NAME testworkspace COMMAND testworkspace)
