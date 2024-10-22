cmake_minimum_required(VERSION 3.27)
project(stella_runtime C)

set(CMAKE_C_STANDARD 11)

include_directories(.)

add_executable(stella_runtime
        gc.c
        gc.h
        runtime.c
        runtime.h)
