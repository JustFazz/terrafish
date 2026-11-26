cmake_minimum_required(VERSION 3.22.1)
project(terraria_memreader LANGUAGES CXX)

add_executable(memreader memreader.cpp)

set_target_properties(memreader PROPERTIES
    OUTPUT_NAME "memreader"
    PREFIX "lib"
    SUFFIX ".so"
)

target_link_libraries(memreader log)
