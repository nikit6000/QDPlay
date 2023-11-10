#Project version minimum
cmake_minimum_required(VERSION 3.16.4)

#Project name
project(QDPlay VERSION 1.0.0)

include_directories(/usr/local/include)
link_directories(/usr/local/lib)

#source files directory

set(PROJECT_SRC 
    "src/main.c"
    "src/usb_accessory/usb_accessory.c"
    "src/usb_accessory/usb_accessory_worker.c"
    "src/usb_accessory/usb_accessory_message_processor.c"
    "src/hu_message.c"
    "src/utils/crc16.c"
    "src/utils/common_utils.c"
)

set(PROJECT_INC_DIR
    "inc"
)

include_directories(${PROJECT_INC_DIR})
set(ALL_FILES ${PROJECT_SRC})

add_executable(${PROJECT_NAME} ${ALL_FILES})
target_link_libraries(${PROJECT_NAME} usbgx pthread)