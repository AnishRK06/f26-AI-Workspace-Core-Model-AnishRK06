cmake_minimum_required(VERSION 3.16)
project(ai_workspace_m4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# M4 runs the corpus build on a std::thread, so everything needs pthreads
find_package(Threads REQUIRED)

# Test is new in M4: QSignalSpy / QTRY_VERIFY for the GUI + threading tests
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

# moc generates the hidden code behind Q_OBJECT / signals / slots. the gui
# headers are listed below so moc sees them
set(CMAKE_AUTOMOC ON)

# M1/M2 backend, unchanged. still the only place processing happens
add_library(aiws_m2
    src/document.cpp
    src/workspace.cpp
    src/text_processor.cpp
    src/chunker.cpp
    src/corpus_index.cpp
    src/retrieval_engine.cpp
    src/context_builder.cpp
    src/processing_core.cpp
)
target_include_directories(aiws_m2 PUBLIC include)
target_link_libraries(aiws_m2 PUBLIC Threads::Threads)
target_compile_options(aiws_m2 PRIVATE -Wall -Wextra -Wpedantic)

# CR-1 wire format + socket read/write helpers. its own small library because
# both the app (IPC server) and aiws_send (tools/) need it, and aiws_send
# shouldn't have to link Qt or the GUI to get it
add_library(aiws_ipc
    src/ipc_protocol.cpp
    include/aiws/ipc_protocol.hpp
)
target_include_directories(aiws_ipc PUBLIC include)
target_compile_options(aiws_ipc PRIVATE -Wall -Wextra -Wpedantic)

# everything except main(). in M3 this was all inside the executable, but M4
# tests (and the autograder's tests) need to link MainWindow directly, so the
# GUI moves into its own library. main.cpp stays out of it on purpose: no
# other code is allowed to depend on it
add_library(aiws_app STATIC
    src/main_window.cpp
    src/workspace_widget.cpp
    src/search_widget.cpp
    src/context_widget.cpp
    src/term_inspector_widget.cpp
    src/search_result_model.cpp
    src/corpus_build_job.cpp
    include/aiws/gui/main_window.hpp
    include/aiws/gui/resettable.hpp
    include/aiws/gui/workspace_widget.hpp
    include/aiws/gui/search_widget.hpp
    include/aiws/gui/context_widget.hpp
    include/aiws/gui/term_inspector_widget.hpp
    include/aiws/gui/search_result_model.hpp
    include/aiws/gui/corpus_build_job.hpp
)
target_include_directories(aiws_app PUBLIC include)
target_link_libraries(aiws_app PUBLIC aiws_m2 aiws_ipc Qt6::Widgets)
target_compile_options(aiws_app PRIVATE -Wall -Wextra -Wpedantic)

# the app itself is now just main() + the library above
add_executable(ai_workspace_m4 src/main.cpp)
target_link_libraries(ai_workspace_m4 PRIVATE aiws_app)
target_compile_options(ai_workspace_m4 PRIVATE -Wall -Wextra -Wpedantic)

# tests
enable_testing()

# course-provided M2 tests, unchanged
add_executable(public_tests tests/public_tests.cpp)
target_link_libraries(public_tests PRIVATE aiws_m2)
target_compile_options(public_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME public_tests COMMAND $<TARGET_FILE:public_tests>)

# student tests. links aiws_app (not just aiws_m2) so it can drive MainWindow
# through add_document_file() and its buttons. has to pass on the normal build
# and fail within 60 s on each fault build, hence the TIMEOUT. offscreen so it
# runs with no display (Docker / autograder)
add_executable(student_tests tests/student_tests.cpp)
target_link_libraries(student_tests PRIVATE aiws_app Qt6::Test)
target_compile_options(student_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME student_tests COMMAND $<TARGET_FILE:student_tests>)
set_tests_properties(student_tests PROPERTIES TIMEOUT 60 ENVIRONMENT QT_QPA_PLATFORM=offscreen)

# CR-1: aiws_send gets added here once tools/aiws_send.cpp exists
