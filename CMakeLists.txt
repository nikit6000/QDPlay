#Project version minimum
cmake_minimum_required(VERSION 3.16.4)

#Project name
project(QDPlay VERSION 1.0.0)

# the `pkg_check_modules` function is created with this call
find_package(PkgConfig REQUIRED) 

pkg_check_modules(GLIB2 REQUIRED glib-2.0)

#source files directory

set(PROJECT_SRC 
    "src/main.c"
    "src/usb_accessory/usb_accessory.c"
    "src/usb_accessory/usb_accessory_worker.c"
    "src/usb_accessory/usb_accessory_message_processor.c"
    "src/hu_message.c"
    "src/utils/crc16.c"
    "src/utils/common_utils.c"
    "src/messages/app_message.c"
    "src/messages/speech.c"
    "src/messages/version.c"
    "src/messages/app_status.c"
    "src/messages/upgrade.c"
    "src/messages/mirror_support.c"
    "src/messages/land_mode.c"
    "src/messages/screen_header.c"
    "src/video/video_receiver.c"
	"src/logging/logging.c"
)

set(PROJECT_INC_DIR
    "inc"
)

include_directories(
	${PROJECT_INC_DIR}
    ${GLIB2_INCLUDE_DIRS} 
)

link_directories(
    ${GLIB2_LIBRARY_DIRS}
)

set(
	ALL_FILES 
	${PROJECT_SRC}
)

add_executable(
	${PROJECT_NAME} 
	${ALL_FILES}
)

target_link_libraries(
	${PROJECT_NAME} 
	${GLIB2_LIBRARIES}
	usbgx 
	pthread
)