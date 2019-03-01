# Try to find the Numa package include dirs and libraries
# Author: Sylvain Chapeland
#
# This script will set the following variables:
#  Numa_FOUND - System has Numa
#  Numa_INCLUDE_DIRS - The Numa include directories
#  Numa_LIBRARIES - The libraries needed to use Numa
#
# This script can use the following variables:
#  Numa_ROOT - Installation root to tell this module where to look.


find_path(
  Numa_INCLUDE_DIRS NAMES numa.h
  PATHS ${Numa_ROOT} ${Numa_ROOT}/include)
  
find_library(
  Numa_LIBRARIES NAMES numa
  PATHS ${Numa_ROOT} ${Numa_ROOT}/lib)
  
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  Numa DEFAULT_MSG Numa_INCLUDE_DIRS Numa_LIBRARIES)

if(${Numa_FOUND})
    message(
    STATUS
    "Found Numa (include: ${Numa_INCLUDE_DIRS} library: ${Numa_LIBRARIES})")    
endif()

mark_as_advanced(Numa_INCLUDE_DIRS Numa_LIBRARIES)
