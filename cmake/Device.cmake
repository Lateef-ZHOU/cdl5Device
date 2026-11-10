find_package(Qt6 6.11.2 REQUIRED COMPONENTS Quick Qml GuiPrivate ShaderTools)
set(CMAKE_AUTOMOC ON)
set(TASKCORE_WITH_QML ON)
set(TASKCORE_WITH_PCAP_FORMS ON)
if(NOT TARGET Lidar::Algorithm)
    FetchContent_Declare(lidar_algorithm
        GIT_REPOSITORY https://github.com/Lateef-ZHOU/Algorithm.git
        GIT_TAG a877748d660805cdb1244f30c44172caf08d72ff)
    FetchContent_MakeAvailable(lidar_algorithm)
endif()
if(NOT TARGET Lidar::TaskUI)
    FetchContent_Declare(lidar_chartcore
        GIT_REPOSITORY https://github.com/Lateef-ZHOU/ChartCore.git
        GIT_TAG 296a52bb13c41639a7909d170a5372647ad6ee69)
    FetchContent_Declare(lidar_taskcore
        GIT_REPOSITORY https://github.com/Lateef-ZHOU/TaskCore.git
        GIT_TAG 5c75a25bfe462830a31a2d69fe2cdd308d6f967d)
    FetchContent_MakeAvailable(lidar_taskcore)
endif()
set(import_paths)
foreach(module Lidar::UICore Lidar::ChartCore Lidar::TaskUI)
    get_target_property(output ${module} QT_QML_MODULE_OUTPUT_DIRECTORY)
    get_filename_component(import_root "${output}/../.." ABSOLUTE)
    list(APPEND import_paths "${import_root}")
endforeach()
set_source_files_properties(include/Cdl5Module.qml PROPERTIES QT_RESOURCE_ALIAS "Cdl5Module.qml")
qt_add_qml_module(LidarCdl5Device STATIC URI Lidar.Cdl5 VERSION 1.0
    CLASS_NAME LidarCdl5Plugin RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Lidar/Cdl5"
    DEPENDENCIES Lidar.UI Lidar.Charts Lidar.TaskUI IMPORT_PATH ${import_paths}
    SOURCES include/Configuration.h include/Device.h include/Collector.h include/Cdl5Protocol.h
    QML_FILES include/Cdl5Module.qml)
add_library(Lidar::Cdl5Device ALIAS LidarCdl5Device)
target_include_directories(LidarCdl5Device PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(LidarCdl5Device PUBLIC Lidar::TaskUI Lidar::Cdl5Udp Lidar::PcapForms)
target_compile_definitions(LidarCdl5Device PUBLIC QT_NO_KEYWORDS NOMINMAX)
if(MSVC)
    target_compile_options(LidarCdl5Device PRIVATE /W4 /utf-8)
    # Qt generated QML code can trigger C4702 inside Qt headers during optimization.
    get_target_property(sources LidarCdl5Device SOURCES)
    foreach(source IN LISTS sources)
        if(source MATCHES "/[.]rcc/qmlcache/")
            set_source_files_properties("${source}" PROPERTIES COMPILE_OPTIONS /wd4702)
        endif()
    endforeach()
else()
    target_compile_options(LidarCdl5Device PRIVATE -Wall -Wextra -Wpedantic)
endif()
