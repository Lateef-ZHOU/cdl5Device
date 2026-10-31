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
