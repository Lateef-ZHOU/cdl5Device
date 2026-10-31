find_package(Qt6 6.11.2 REQUIRED COMPONENTS Quick Qml GuiPrivate ShaderTools)
set(CMAKE_AUTOMOC ON)
set(TASKCORE_WITH_QML ON)
set(TASKCORE_WITH_PCAP_FORMS ON)
if(NOT TARGET Lidar::TaskUI)
    FetchContent_Declare(lidar_taskcore
        GIT_REPOSITORY https://github.com/Lateef-ZHOU/TaskCore.git
        GIT_TAG a830298848737d33a8b451fb91ea18dce308a8a5)
    FetchContent_MakeAvailable(lidar_taskcore)
endif()
set(import_paths)
foreach(module Lidar::UICore Lidar::ChartCore Lidar::TaskUI)
    get_target_property(output ${module} QT_QML_MODULE_OUTPUT_DIRECTORY)
    get_filename_component(import_root "${output}/../.." ABSOLUTE)
    list(APPEND import_paths "${import_root}")
endforeach()
set_source_files_properties(include/Cdl5/Cdl5Module.qml PROPERTIES QT_RESOURCE_ALIAS Cdl5Module.qml)
qt_add_qml_module(LidarCdl5Device STATIC URI Lidar.Cdl5 VERSION 1.0
    CLASS_NAME LidarCdl5Plugin RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Lidar/Cdl5"
    DEPENDENCIES Lidar.UI Lidar.Charts Lidar.TaskUI IMPORT_PATH ${import_paths}
    SOURCES include/Cdl5/Device.h include/Cdl5/Collector.h include/Cdl5/Protocol.h
    QML_FILES include/Cdl5/Cdl5Module.qml)
add_library(Lidar::Cdl5Device ALIAS LidarCdl5Device)
target_include_directories(LidarCdl5Device PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/Cdl5")
target_link_libraries(LidarCdl5Device PUBLIC Lidar::TaskUI Lidar::Cdl5Udp Lidar::PcapForms)
target_compile_definitions(LidarCdl5Device PUBLIC QT_NO_KEYWORDS NOMINMAX)
if(MSVC)
    target_compile_options(LidarCdl5Device PRIVATE /W4 /utf-8)
else()
    target_compile_options(LidarCdl5Device PRIVATE -Wall -Wextra -Wpedantic)
endif()
