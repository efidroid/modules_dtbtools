cmake_minimum_required(VERSION 2.8)
project(dtbconvert)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -Wshadow")

include_directories(
    include
)

# main
add_executable(dtbconvert
    src/main.c
)
target_link_libraries(dtbconvert fdt)
