# Try to find the gperftools package include dirs and libraries
# Author: Sylvain Chapeland
#
# This script will set the following variables:
#  gperftools_FOUND - System has gperftools
#  gperftools_INCLUDE_DIRS - The gperftools include directories
#  gperftools_LIBRARIES - The libraries needed to use gperftools
#
# This script can use the following variables:
#  gperftools_ROOT - Installation root to tell this module where to look.


find_path(
  gperftools_INCLUDE_DIRS NAMES gperftools/profiler.h
  PATHS ${gperftools_ROOT} ${gperftools_ROOT}/include)
  
find_library(
  gperftools_LIBRARIES NAMES profiler tcmalloc
  PATHS ${gperftools_ROOT} ${gperftools_ROOT}/lib)
  
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  gperftools DEFAULT_MSG gperftools_INCLUDE_DIRS gperftools_LIBRARIES)

if(${gperftools_FOUND})
    message(
    STATUS
    "Found gperftools (include: ${gperftools_INCLUDE_DIRS} library: ${gperftools_LIBRARIES})")    
endif()

mark_as_advanced(gperftools_INCLUDE_DIRS gperftools_LIBRARIES)
