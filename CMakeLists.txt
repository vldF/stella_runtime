cmake_minimum_required(VERSION 3.27)
project(stella_runtime C)

set(CMAKE_C_STANDARD 11)

include_directories(
        {$PROJECT_SOURCE_DIR}/stella
        {$PROJECT_SOURCE_DIR}
)

add_executable(stella_runtime
        ./stella/gc.c
        ./stella/gc.h
        ./stella/runtime.c
        ./stella/runtime.h
        out.c)
