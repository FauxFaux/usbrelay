cmake_minimum_required(VERSION 3.5)
project(usbrelay)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI hidapi-libusb REQUIRED)

if (CMAKE_COMPILER_IS_GNUCC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} \
     -std=gnu11 \
     -Wall -Wextra -pedantic \
     -fsanitize=address \
     -D_XOPEN_SOURCE=500 -D_DEFAULT_SOURCE")
endif(CMAKE_COMPILER_IS_GNUCC)

add_executable(usbrelay usbrelay.c)
target_link_libraries(usbrelay ${HIDAPI_LIBRARIES})
#target_include_directories(usbrelay ${HIDAPI_INCLUDE_DIRS})
#target_compile_options(usbrelay ${HIDAPI_CFLAGS_OTHER})
