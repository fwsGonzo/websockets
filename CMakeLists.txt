cmake_minimum_required(VERSION 2.8.9)
project(service)

# IncludeOS install location
if (NOT DEFINED ENV{INCLUDEOS_PREFIX})
  set(ENV{INCLUDEOS_PREFIX} /usr/local)
endif()

# Human-readable name of your service
set(SERVICE_NAME "WebSockets service")

# Name of your service binary
set(BINARY       "websockets")

# Maximum memory can be hard-coded into the binary
set(MAX_MEM 256)

# Source files to be linked with OS library parts to form bootable image
set(SOURCES
    service.cpp
  )

# To add your own include paths:
set(LOCAL_INCLUDES )

# DRIVERS / PLUGINS:

set(DRIVERS
  virtionet
  silent_start
  #heap_debugging
  )

# include service build script
include($ENV{INCLUDEOS_PREFIX}/includeos/service.cmake)
