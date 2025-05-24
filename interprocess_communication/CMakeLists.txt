cmake_minimum_required(VERSION 3.10)
project(message_slot_module C)

set(CMAKE_C_STANDARD 99)

# Use installed (non-matching) kernel headers for code insight
include_directories(
        ${CMAKE_SOURCE_DIR}
        /usr/src/linux-headers-6.8.0-60/include
        /usr/src/linux-headers-6.8.0-60/include/uapi
)

add_executable(dummy_parser
        message_slot.c
        message_reader.c
        message_sender.c
        Makefile
)
