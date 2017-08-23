# - Try to find the O2 ReadoutCard package include dirs and libraries
# Author: Barthelemy von Haller
#
# This script will set the following variables:
#  ReadoutCard_FOUND - System has ReadoutCard
#  ReadoutCard_INCLUDE_DIRS - The ReadoutCard include directories
#  ReadoutCard_LIBRARIES - The libraries needed to use ReadoutCard
#  ReadoutCard_DEFINITIONS - Compiler switches required for using ReadoutCard
#
# This script can use the following variables:
#  ReadoutCard_ROOT - Installation root to tell this module where to look. (it tries LD_LIBRARY_PATH otherwise)

# Init
include(FindPackageHandleStandardArgs)

# find includes
find_path(READOUTCARD_INCLUDE_DIR ReadoutCard.h
        HINTS ${ReadoutCard_ROOT}/include ENV LD_LIBRARY_PATH PATH_SUFFIXES "../include/ReadoutCard" "../../include/ReadoutCard" )
# Remove the final "ReadoutCard"
get_filename_component(READOUTCARD_INCLUDE_DIR ${READOUTCARD_INCLUDE_DIR} DIRECTORY)
set(ReadoutCard_INCLUDE_DIRS ${READOUTCARD_INCLUDE_DIR})

# find library
find_library(READOUTCARD_LIBRARY NAMES ReadoutCard HINTS ${ReadoutCard_ROOT}/lib ENV LD_LIBRARY_PATH)
set(ReadoutCard_LIBRARIES ${READOUTCARD_LIBRARY})

# handle the QUIETLY and REQUIRED arguments and set READOUTCARD_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(ReadoutCard  "ReadoutCard could not be found. Install package ReadoutCard or set ReadoutCard_ROOT to its root installation directory."
        READOUTCARD_LIBRARY READOUTCARD_INCLUDE_DIR)

if(${ReadoutCard_ROOT})
    message(STATUS "ReadoutCard found : ${ReadoutCard_LIBRARIES}")
endif()

mark_as_advanced(READOUTCARD_INCLUDE_DIR READOUTCARD_LIBRARY)
