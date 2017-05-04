find_package(Boost COMPONENTS unit_test_framework program_options REQUIRED)
find_package(Git QUIET)
find_package(FairRoot)
find_package(Monitoring REQUIRED)
find_package(Configuration REQUIRED)

if(FAIRROOT_FOUND)
    # this should go away when fairrot provides a proper Find script or proper config scripts
    # See : http://www.cmake.org/cmake/help/v3.0/command/link_directories.html
    link_directories(${FAIRROOT_LIBRARY_DIR})
    set(FAIRROOT_LIBRARIES Base FairMQ BaseMQ)
else(FAIRROOT_FOUND)
    message(WARNING "FairRoot not found, corresponding classes will not be compiled.")
endif(FAIRROOT_FOUND)

o2_define_bucket(
  NAME
  o2_readout_bucket

  DEPENDENCIES
  InfoLogger
  pthread
  DataFormat
  Common
  RORC
  ${Configuration_LIBRARIES}
  ${MONITORING_LIBRARIES}
  SYSTEMINCLUDE_DIRECTORIES
  ${Boost_INCLUDE_DIRS}
  ${MONITORING_INCLUDE_DIRS}
)


o2_define_bucket(
  NAME
  o2_readout_with_fair

  DEPENDENCIES
  o2_readout_bucket
  ${Boost_PROGRAM_OPTIONS_LIBRARY}
  ${Boost_LOG_LIBRARY}
  ${Boost_THREAD_LIBRARY}
  ${Boost_SYSTEM_LIBRARY}
  ${FAIRROOT_LIBRARIES}
  ${ROOT_LIBRARIES}

  SYSTEMINCLUDE_DIRECTORIES
  ${Boost_INCLUDE_DIRS}
  ${FAIRROOT_INCLUDE_DIR}
  ${FAIRROOT_INCLUDE_DIR}/fairmq
)

