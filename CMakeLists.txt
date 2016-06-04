cmake_minimum_required(VERSION 2.8)
project(dtbconvert)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -Wshadow")

include_directories(
    include
)

# main
add_executable(qcdt2efidroiddts
    src/main.c
)

# dtbtool
add_executable(dtbtool
    src/dtbtool.c
)

# qcdtextract
add_executable(qcdtextract
    src/qcdtextract.c
)
