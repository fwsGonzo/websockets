cmake_minimum_required(VERSION 2.8.9)
# IncludeOS install location
if (NOT DEFINED ENV{INCLUDEOS_PREFIX})
  set(ENV{INCLUDEOS_PREFIX} /usr/local)
endif()
include($ENV{INCLUDEOS_PREFIX}/includeos/pre.service.cmake)
project(service)

# Human-readable name of your service
set(SERVICE_NAME "WebSockets service")

# Name of your service binary
set(BINARY       "websockets")

# Source files to be linked with OS library parts to form bootable image
set(SOURCES
    service.cpp
    #tcp_smp.cpp
    #tls_smp_server.cpp
    #tls_smp_client.cpp
    #tls_smp_system.cpp
    #smp_tests.cpp
  )

# To add your own include paths:
set(LOCAL_INCLUDES )

# DRIVERS / PLUGINS:
set(DRIVERS
  virtionet
  vmxnet3
  #boot_logger
  )

set(PLUGINS vfs)

# include service build script
include($ENV{INCLUDEOS_PREFIX}/includeos/post.service.cmake)

# add AES-NI and SSE4.2
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2 -maes")

diskbuilder(drive)
