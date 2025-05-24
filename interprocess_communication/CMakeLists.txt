cmake_minimum_required(VERSION 3.10)
project(message_slot_module C)

set(CMAKE_C_STANDARD 99)

include_directories(
        ${CMAKE_SOURCE_DIR}
        "C:/Users/User/Documents/OS/share/linux-internals-reimplemented/interprocess_communication"
        "C:/Users/User/Documents/OS/share/linux-internals-reimplemented/interprocess_communication/linux_headers" # if applicable
)

add_executable(message_slot_dummy
        message_slot.c
        message_reader.c
        message_sender.c
)
