function(cdl5_probe target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 15)
endfunction()

if(CDL5_WITH_UDP)
    add_executable(cdl5_udp_probe udp.cpp)
    target_link_libraries(cdl5_udp_probe PRIVATE Lidar::Cdl5Udp)
    cdl5_probe(cdl5_udp_probe)
    if(WIN32)
        set_tests_properties(cdl5_udp_probe PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:HandlePcapDependency>")
    endif()
endif()

if(CDL5_WITH_DMA)
    add_executable(cdl5_dma_capture_probe dma_capture.cpp)
    target_link_libraries(cdl5_dma_capture_probe PRIVATE Lidar::Cdl5Dma)
    target_link_options(cdl5_dma_capture_probe PRIVATE
        -Wl,--wrap=open64 -Wl,--wrap=close -Wl,--wrap=ioctl
        -Wl,--wrap=mmap64 -Wl,--wrap=munmap -Wl,--wrap=lseek64 -Wl,--wrap=read)
    cdl5_probe(cdl5_dma_capture_probe)
    add_executable(cdl5_dma_connection_probe dma_connection.cpp)
    target_link_libraries(cdl5_dma_connection_probe PRIVATE Lidar::Cdl5Dma)
    target_link_options(cdl5_dma_connection_probe PRIVATE
        -Wl,--wrap=open64 -Wl,--wrap=close -Wl,--wrap=ioctl
        -Wl,--wrap=mmap64 -Wl,--wrap=munmap -Wl,--wrap=sysconf)
    cdl5_probe(cdl5_dma_connection_probe)
    foreach(header IN ITEMS DmaConnection DmaHandle)
        file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${header}_alone.cpp"
            CONTENT "#include <Cdl5/${header}.h>\n")
        target_sources(cdl5_dma_connection_probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${header}_alone.cpp")
    endforeach()
endif()

if(CDL5_WITH_DEVICE)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(cdl5_pipeline_probe pipeline.cpp
            "${lidar_handle_core_SOURCE_DIR}/tests/pcap/FakePcap.cpp")
        target_include_directories(cdl5_pipeline_probe PRIVATE "${lidar_handle_core_SOURCE_DIR}/tests/pcap")
        target_link_libraries(cdl5_pipeline_probe PRIVATE Lidar::Cdl5Device)
        cdl5_probe(cdl5_pipeline_probe)
    endif()
    foreach(header IN ITEMS Device Collector Protocol)
        file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Cdl5${header}_alone.cpp"
            CONTENT "#include <Cdl5/${header}.h>\n")
        add_library(cdl5_${header}_header OBJECT "${CMAKE_CURRENT_BINARY_DIR}/Cdl5${header}_alone.cpp")
        target_link_libraries(cdl5_${header}_header PRIVATE Lidar::Cdl5Device)
    endforeach()
    add_executable(cdl5_collector_probe collector.cpp)
    target_link_libraries(cdl5_collector_probe PRIVATE Lidar::Cdl5Device)
    cdl5_probe(cdl5_collector_probe)
    add_executable(cdl5_page_probe smoke.cpp)
    target_link_libraries(cdl5_page_probe PRIVATE Lidar::Cdl5Device LidarCdl5Deviceplugin
        LidarTaskUIplugin LidarUICoreplugin LidarChartCoreplugin)
    cdl5_probe(cdl5_page_probe)
    foreach(probe IN ITEMS cdl5_collector_probe cdl5_page_probe)
        set_tests_properties(${probe} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=null"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        if(WIN32)
            set_property(TEST ${probe} APPEND PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:HandlePcapDependency>")
        endif()
    endforeach()
endif()
