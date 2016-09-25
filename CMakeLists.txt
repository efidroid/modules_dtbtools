cmake_minimum_required(VERSION 2.8)
project(dtbconvert)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -Wshadow -O0 -ggdb")

include_directories(
    include
)

# dtbtool
add_executable(dtbtool
    src/dtbtool.c
)

# qcdtextract
add_executable(qcdtextract
    src/qcdtextract.c
)
target_link_libraries(qcdtextract boot fdt z)
target_include_directories(qcdtextract PUBLIC
    ${HOST_LIBBOOT_SRC}/include_private
)

# fdtextract
add_executable(fdtextract
    src/fdtextract.c
)
target_link_libraries(fdtextract fdt)

# dtbefidroidify
add_executable(dtbefidroidify
    src/dtbefidroidify.c
)
target_link_libraries(dtbefidroidify boot fdt z)

# smemparse
add_executable(smemparse
    src/smemparse.c
)

