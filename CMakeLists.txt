cmake_minimum_required(VERSION 3.4)
project(ble-udp-bridge)
include(FindGettext)
include(GNUInstallDirs)
file(GLOB c3listener_SRC
    "src/*.c"
)
add_definitions(-DSYSCONFDIR=\"${CMAKE_INSTALL_FULL_SYSCONFDIR}\" -DPACKAGE_VERSION=\"4.0.1\")
add_executable(c3listener ${c3listener_SRC})
target_link_libraries(c3listener m config bluetooth)
