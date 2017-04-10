find_package(Boost COMPONENTS unit_test_framework program_options REQUIRED)
find_package(Git QUIET)
find_package(FairRoot)
if(FAIRROOT_FOUND)
    link_directories(${FAIRROOT_LIBRARY_DIR})
    find_package(ROOT REQUIRED)
    find_package(Boost COMPONENTS unit_test_framework program_options log thread system REQUIRED)
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
  rorc
  DataSampling
  DataCollector

  SYSTEMINCLUDE_DIRECTORIES
  ${Boost_INCLUDE_DIRS}
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
  Base
  FairMQ
  BaseMQ
  fairmq_logger
  ${ROOT_LIBRARIES}

  SYSTEMINCLUDE_DIRECTORIES
  ${Boost_INCLUDE_DIRS}
)

