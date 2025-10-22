cmake_minimum_required(VERSION 3.26)
project(cpp_kv)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(cpp_kv
        array_input_stream.cc
        copier.cc
        kv-client.cc
        kv-server.cc
        kv.pb.cc
)
