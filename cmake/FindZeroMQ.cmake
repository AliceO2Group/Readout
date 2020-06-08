# Try to find the ZeroMQ package include dirs and libraries
# Author: Sylvain Chapeland
#
# This script will set the following variables:
#  ZeroMQ_FOUND - System has ZeroMQ
#  ZeroMQ_INCLUDE_DIRS - The ZeroMQ include directories
#  ZeroMQ_LIBRARIES - The libraries needed to use ZeroMQ
#
# This script can use the following variables:
#  ZeroMQ_ROOT - Installation root to tell this module where to look.

find_path(
  ZeroMQ_INCLUDE_DIRS NAMES zmq.h
  PATHS ${ZeroMQ_ROOT} ${ZeroMQ_ROOT}/include
  NO_DEFAULT_PATH)
  
find_library(
  ZeroMQ_LIBRARIES NAMES zmq
  PATHS ${ZeroMQ_ROOT} ${ZeroMQ_ROOT}/lib
  NO_DEFAULT_PATH)
 
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZeroMQ
  REQUIRED_VARS
   ZeroMQ_INCLUDE_DIRS
   ZeroMQ_LIBRARIES
)

message(
    STATUS
    "CMAKE ZeroMQ search using path = ${ZeroMQ_ROOT}")    
message(
    STATUS
    "CMAKE ZeroMQ search lib= ${ZeroMQ_LIBRARIES} include=${ZeroMQ_INCLUDE_DIRS}")    

if(${ZeroMQ_FOUND})
    message(
    STATUS
    "Found ZeroMQ using path = ${ZeroMQ_ROOT} (include: ${ZeroMQ_INCLUDE_DIRS} library: ${ZeroMQ_LIBRARIES})")    
endif()

list(APPEND ZeroMQ_LIBRARIES pthread)

mark_as_advanced(ZeroMQ_INCLUDE_DIRS ZeroMQ_LIBRARIES)
