#Project version minimum
cmake_minimum_required(VERSION 3.16.4)

#Project name
project(QDPlay VERSION 1.0.0)

include_directories(/usr/local/include)
link_directories(/usr/local/lib)

#source files directory

set(PROJECT_SRC 
    "src/main.c"
)

set(PROJECT_INC_DIR
    "inc"
)

include_directories(${PROJECT_INC_DIR})
set(ALL_FILES ${PROJECT_SRC})

add_executable(${PROJECT_NAME} ${ALL_FILES})
target_link_libraries(${PROJECT_NAME} usbgx)