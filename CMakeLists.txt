cmake_minimum_required(VERSION 2.8)
project(ble-udp-bridge)
if (CMAKE_VERSION VERSION_LESS "3.1")
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
      set (CMAKE_C_FLAGS "--std=gnu99 ${CMAKE_C_FLAGS}")
    endif ()
else ()
  set (CMAKE_C_STANDARD 99)
endif ()

option(DEBUG "Enable debug and disable optimization" OFF)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")

if(DEBUG)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g -Werror")
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
