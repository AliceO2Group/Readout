# Try to find the ZeroMQ package include dirs and libraries
# Author: Sylvain Chapeland
#
# This script will set the following variables:
#  ZMQ_FOUND - System has ZeroMQ
#  ZMQ_INCLUDE_DIRS - The ZeroMQ include directories
#  ZMQ_LIBRARIES - The libraries needed to use ZeroMQ
#
# This script can use the following variables:
#  ZMQ_ROOT - Installation root to tell this module where to look.

find_path(
  ZMQ_INCLUDE_DIRS NAMES zmq.h
  PATHS ${ZMQ_ROOT} ${ZMQ_ROOT}/include
  )
  
find_library(
  ZMQ_LIBRARIES NAMES zmq
  PATHS ${ZMQ_ROOT} ${ZMQ_ROOT}/lib
  )
 
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZMQ
  DEFAULT_MSG
  ZMQ_INCLUDE_DIRS
  ZMQ_LIBRARIES
)

if(${ZMQ_FOUND})
    message(
    STATUS
    "Found ZMQ using path = ${ZMQ_ROOT} (include: ${ZMQ_INCLUDE_DIRS} library: ${ZMQ_LIBRARIES})")    
endif()

list(APPEND ZMQ_LIBRARIES pthread)

mark_as_advanced(ZMQ_INCLUDE_DIRS ZMQ_LIBRARIES)
