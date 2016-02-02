cmake_minimum_required(VERSION 3.4)
project(ble-udp-bridge)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
include(FindGettext)
include(GNUInstallDirs)
include(GetGitRevisionDescription)
git_describe(PACKAGE_VERSION)
file(GLOB c3listener_SRC
    "src/*.c"
)
add_definitions(-DSYSCONFDIR=\"${CMAKE_INSTALL_FULL_SYSCONFDIR}\" -DPACKAGE_VERSION=\"${PACKAGE_VERSION}\")
add_executable(c3listener ${c3listener_SRC})
target_link_libraries(c3listener m config bluetooth)
