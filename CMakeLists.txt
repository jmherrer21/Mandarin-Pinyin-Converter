cmake_minimum_required(VERSION 3.16)
project(mandarin-pdf-reader CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(unofficial-libmupdf CONFIG REQUIRED)

add_executable(mandarin-pdf-reader WIN32
    src/main.cpp
    src/dictionary.cpp
    src/segmenter.cpp
    src/pdf_extractor.cpp
    src/renderer.cpp
    src/tts.cpp
)

target_link_libraries(mandarin-pdf-reader PRIVATE unofficial::libmupdf::libmupdf)

target_include_directories(mandarin-pdf-reader PRIVATE
    deps/cppjieba/include
    deps/limonp/include
)

add_custom_command(TARGET mandarin-pdf-reader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/data"
        "$<TARGET_FILE_DIR:mandarin-pdf-reader>/data"
    COMMENT "Copying data/ next to binary"
)
