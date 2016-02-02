cmake_minimum_required(VERSION 2.6)
project(ble-udp-bridge)
option(DEBUG "Enable debug and disable optimization" OFF)

if(DEBUG)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g")
else(DEBUG)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os")
endif(DEBUG)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
include(GetGitRevisionDescription)
FIND_PACKAGE(Bluez REQUIRED)
FIND_PACKAGE(Config REQUIRED)
git_describe(PACKAGE_VERSION)
file(GLOB c3listener_SRC
    "src/*.c"
)
add_definitions(-DSYSCONFDIR=\"${CMAKE_INSTALL_PREFIX}/etc\" -DPACKAGE_VERSION=\"${PACKAGE_VERSION}\")
add_executable(c3listener ${c3listener_SRC})
target_link_libraries(c3listener m ${CONFIG_LIBRARY} ${BLUEZ_LIBRARY})
