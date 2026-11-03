include(FetchContent)
find_package(Qt6 6.8.3 REQUIRED COMPONENTS Core)

# Only the selected transport enables its dependencies; no UI or serial module.
if(NOT TARGET Lidar::HandleCore)
    if(CDL5_WITH_UDP)
        set(HANDLECORE_WITH_PCAP ON CACHE BOOL "Required by cdl5Device UDP" FORCE)
    endif()
    FetchContent_Declare(lidar_handle_core
        GIT_REPOSITORY https://github.com/Lateef-ZHOU/HandleCore.git
        GIT_TAG 40b7dcac817089c4f5dfbefb20c3b39b188b9f70)
    FetchContent_MakeAvailable(lidar_handle_core)
endif()
if(CDL5_WITH_UDP AND NOT TARGET Lidar::Pcap)
    message(FATAL_ERROR "Enable HANDLECORE_WITH_PCAP before configuring the existing HandleCore target")
endif()
