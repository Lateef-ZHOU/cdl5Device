cmake_minimum_required(VERSION 3.28)
project(cdl5Device LANGUAGES CXX)

option(CDL5_WITH_UDP "Enable the Windows/Linux UDP transport from HandleCore" OFF)
option(CDL5_WITH_DMA "Enable the Linux DMA acquisition Handle" OFF)
option(CDL5_WITH_DEVICE "Enable the UDP acquisition residence and QML page" OFF)
if(CDL5_WITH_DEVICE)
    set(CDL5_WITH_UDP ON)
endif()

if(CDL5_WITH_DMA AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "CDL5 DMA requires Linux; use CDL5_WITH_UDP on Windows")
endif()
if(CDL5_WITH_DMA)
    set(CDL5_DMA_PROXY_SOURCE_DIR "" CACHE PATH "DmaProxy checkout containing the shared ioctl header")
    if(NOT EXISTS "${CDL5_DMA_PROXY_SOURCE_DIR}/cdl5-dma-proxy.h")
        message(FATAL_ERROR "Set CDL5_DMA_PROXY_SOURCE_DIR to DmaProxy; do not copy its ABI header")
    endif()
endif()

if(CDL5_WITH_UDP OR CDL5_WITH_DMA)
    include(cmake/Dependencies.cmake)
else()
    message(STATUS "cdl5Device: transports disabled; enable CDL5_WITH_UDP and/or CDL5_WITH_DMA")
endif()

# UDP has no CDL5-specific transport behavior. Consumers use PcapHandle directly.
if(CDL5_WITH_UDP)
    add_library(Cdl5Udp INTERFACE)
    add_library(Lidar::Cdl5Udp ALIAS Cdl5Udp)
    target_link_libraries(Cdl5Udp INTERFACE Lidar::Pcap)
    target_include_directories(Cdl5Udp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(Cdl5Udp INTERFACE QT_NO_KEYWORDS)
endif()

if(CDL5_WITH_DMA)
    add_library(Cdl5Dma STATIC include/Cdl5/AcquisitionHandle.h)
    set_target_properties(Cdl5Dma PROPERTIES AUTOMOC ON)
    add_library(Lidar::Cdl5Dma ALIAS Cdl5Dma)
    target_include_directories(Cdl5Dma PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CDL5_DMA_PROXY_SOURCE_DIR}")
    target_sources(Cdl5Dma PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include/Cdl5/DmaConnection.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/Cdl5/DmaHandle.h")
    target_link_libraries(Cdl5Dma PUBLIC Lidar::HandleCore)
    target_compile_definitions(Cdl5Dma PUBLIC QT_NO_KEYWORDS _FILE_OFFSET_BITS=64)
    target_compile_options(Cdl5Dma PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(CDL5_WITH_DEVICE)
    include(cmake/Device.cmake)
endif()

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
