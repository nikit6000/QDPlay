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
    "src/usb_accessory/usb_active_accessory.c"
    "src/hu_message.c"
    "src/utils/crc16.c"
    "src/utils/common_utils.c"
    "src/utils/cJSON.c"

    # V1 messages

    "src/messages/v1/app_message.c"
    "src/messages/v1/speech.c"
    "src/messages/v1/version.c"
    "src/messages/v1/app_status.c"
    "src/messages/v1/upgrade.c"
    "src/messages/v1/mirror_support.c"
    "src/messages/v1/land_mode.c"
    "src/messages/v1/screen_header.c"
    "src/messages/v1/heart_beat_v1.c"

    # V2 messages

    "src/messages/v2/message_heart_beat_v2.c"
    "src/messages/v2/message_phone_info_v2.c"
    "src/messages/v2/message_car_info_parameters_v2.c"
    "src/messages/v2/message_cmd_response_v2.c"
    "src/messages/v2/message_video_ctrl_parameters_v2.c"
    "src/messages/v2/message_video_support_prarameters_v2.c"
    "src/messages/v2/message_video_frame_v2.c"
    "src/messages/v2/message_update_status_v2.c"
    "src/messages/v2/message_touch_event_v2.c"

    # Services

    "src/services/video_receiver/video_receiver.c"
    "src/services/messaging_service/messaging_service.c"
	"src/logging/logging.c"

    # V1 message processor

    "src/usb_accessory/message_processor/v1/message_processor_v1.c"

    # V2 message processor

    "src/usb_accessory/message_processor/v2/message_processor_v2.c"
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

install(
    TARGETS
    ${PROJECT_NAME}
    DESTINATION
    bin
)