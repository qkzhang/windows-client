cmake_minimum_required(VERSION 3.8)
project(server)

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc")

IF (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    SET(option WIN32)
    SET(win32_LIBRARIES WS2_32.lib)
    #SET(defs -DUNICODE -D_UNICODE)
    link_libraries(WS2_32.lib)
ENDIF (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")

set(SOURCE_FILES main.cpp)
add_executable(server ${SOURCE_FILES})