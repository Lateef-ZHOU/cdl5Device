include(FetchContent)
find_package(Qt6 6.8.3 REQUIRED COMPONENTS Core)

# Only the selected transport enables its dependencies; no UI or serial module.
if(NOT TARGET Lidar::HandleCore)
    if(CDL5_WITH_UDP)
        set(HANDLECORE_WITH_PCAP ON CACHE BOOL "Required by cdl5Device UDP" FORCE)
    endif()
    FetchContent_Declare(lidar_handle_core
        GIT_REPOSITORY https://github.com/Lateef-ZHOU/HandleCore.git
        GIT_TAG 113523f3a40ad2dc7c417d01326688b5bd79a51b)
    FetchContent_MakeAvailable(lidar_handle_core)
endif()
if(CDL5_WITH_UDP AND NOT TARGET Lidar::Pcap)
    message(FATAL_ERROR "Enable HANDLECORE_WITH_PCAP before configuring the existing HandleCore target")
endif()
