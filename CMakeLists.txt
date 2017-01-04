cmake_minimum_required(VERSION 2.8)
project(c3listener)
if (CMAKE_VERSION VERSION_LESS "3.1")
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
      set (CMAKE_C_FLAGS "--std=gnu99 ${CMAKE_C_FLAGS}")
    endif ()
else ()
  set (CMAKE_C_STANDARD 99)
endif ()

option(DEBUG "Enable debug and disable optimization" OFF)
option(SANITIZE "Enable Address/Undefined Behaviour Sanitization" OFF)
option(FORTIFY "Enable security CFLAGS" ON)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic")

if(DEBUG)
  message(STATUS "DEBUG Build Enabled")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g -Werror")
else(DEBUG)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -flto")
endif(DEBUG)

if(SANITIZE)
  message(STATUS "SANITIZE Enabled")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address,undefined")
endif(SANITIZE)

if(FORTIFY)
  message(STATUS "FORTIFY Enabled")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wformat -Werror=format-security -fstack-protector -D_FORTIFY_SOURCE=1")
  set(CMAKE_LD_FLAGS "${CMAKE_LD_FLAGS} -Wl,-z,now -Wl,-z,relro")
endif(FORTIFY)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
include(GetGitRevisionDescription)
FIND_PACKAGE(Bluez REQUIRED)
FIND_PACKAGE(Config REQUIRED)
FIND_PACKAGE(LibEvent REQUIRED)
FIND_PACKAGE(LibJson-c REQUIRED)
#FIND_PACKAGE(LibEvhtp REQUIRED)
FIND_PACKAGE(LibUci)
FIND_PACKAGE(LibMagic)
#FIND_PACKAGE(OpenSSL REQUIRED)
git_describe(PACKAGE_VERSION)
file(GLOB c3listener_SRC
    "src/beacon.c"
    "src/ble.c"
    "src/config.c"
    "src/hash.c"
    "src/hostname.c"
    "src/http.c"
    "src/ipc.c"
    "src/ipc-privileged.c"
    "src/kalman.c"
    "src/log.c"
    "src/main.c"
    "src/report.c"
    "src/time_util.c"
    "src/udp.c"
)

if (UCI_FOUND)
  list (APPEND c3listener_SRC "src/uci.c")
else(LIBUCI_FOUND)
  message(STATUS "UCI Disabled")
endif (UCI_FOUND)

add_definitions(-DSYSCONFDIR=\"${CMAKE_INSTALL_PREFIX}/etc\"
                -DWEBROOT=\"${CMAKE_INSTALL_PREFIX}/share/c3listener/web\"
		-DPACKAGE_VERSION=\"${PACKAGE_VERSION}\" -D_GNU_SOURCE)
add_executable(c3listener ${c3listener_SRC})
target_link_libraries(c3listener m ${CONFIG_LIBRARY} ${BLUEZ_LIBRARY} ${JSONC_LIBRARY} ${LIBEVENT_LIB}
		      ${LIBEVHTP_LIB})

if (UCI_FOUND)
  target_link_libraries (c3listener ${UCI_LIBRARY})
  add_definitions(-DHAVE_UCI)
endif (UCI_FOUND)

if (LIBMAGIC_FOUND)
  include_directories(${LibMagic_INCLUDE_DIR})
  target_link_libraries (c3listener ${LibMagic_LIBRARY})
  add_definitions(-DHAVE_LIBMAGIC)
endif (LIBMAGIC_FOUND)

install (TARGETS c3listener DESTINATION bin)
install (FILES c3listener.conf DESTINATION etc)
install (DIRECTORY web DESTINATION share/c3listener
         FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
         DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
