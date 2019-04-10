# Try to find the include dirs and libraries for RDMA development (in particular: ibverbs, rdmacm)
# Author: Sylvain Chapeland
#
# This script will set the following variables:
#  RDMA_FOUND - System has RDMA
#  RDMA_INCLUDE_DIRS - The RDMA include directories
#  RDMA_LIBRARIES - The libraries needed to use RDMA
#
# This script can use the following variables:
#  RDMA_ROOT - Installation root to tell this module where to look.


# ibverbs
find_path(
  IBVERBS_INCLUDE_DIR NAMES infiniband/verbs.h
  PATHS ${RDMA_ROOT} ${RDMA_ROOT}/include)

find_library(
  IBVERBS_LIBRARY NAMES ibverbs
  PATHS ${RDMA_ROOT} ${RDMA_ROOT}/lib)


# rdmacm
find_path(
  RDMACMA_INCLUDE_DIR NAMES rdma/rdma_cma.h
  PATHS ${RDMA_ROOT} ${RDMA_ROOT}/include)

find_library(
  RDMACMA_LIBRARY NAMES rdmacm
  PATHS ${RDMA_ROOT} ${RDMA_ROOT}/lib)


# check all components found
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  RDMA DEFAULT_MSG IBVERBS_INCLUDE_DIR RDMACMA_INCLUDE_DIR IBVERBS_LIBRARY RDMACMA_LIBRARY)

# define RDMA variables
if(${RDMA_FOUND})
    set(RDMA_INCLUDE_DIRS ${IBVERBS_INCLUDE_DIR} ${RDMACMA_INCLUDE_DIR})
    list(REMOVE_DUPLICATES RDMA_INCLUDE_DIRS)


    set(RDMA_LIBRARIES ${IBVERBS_LIBRARY} ${RDMACMA_LIBRARY})
    list(REMOVE_DUPLICATES RDMA_LIBRARIES)

    message(
    STATUS
    "Found RDMA (include: ${RDMA_INCLUDE_DIRS} library: ${RDMA_LIBRARIES})")    
endif()

mark_as_advanced(RDMA_INCLUDE_DIRS RDMA_LIBRARIES)
