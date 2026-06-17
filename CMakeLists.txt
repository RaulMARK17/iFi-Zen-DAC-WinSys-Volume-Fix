cmake_minimum_required(VERSION 3.10)
project(ifi_volume_sync CXX)

# Require C++17 standard for modern language features
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files list
set(SOURCES
    main.cpp
    VolumeSync.h
    VolumeSync.cpp
)

# Build a WIN32 GUI application (WinMain subsystem) to prevent CMD window popups
add_executable(ifi_volume_sync WIN32 ${SOURCES})

# Link essential Windows COM libraries
target_link_libraries(ifi_volume_sync PRIVATE
    ole32
    uuid
    shell32
)
