find_package(Boost COMPONENTS unit_test_framework program_options system thread system timer program_options random filesystem regex signals REQUIRED)
find_package(Git QUIET)
find_package(Monitoring REQUIRED)
find_package(Configuration REQUIRED)
find_package(Common REQUIRED)
find_package(InfoLogger REQUIRED)
find_package(ReadoutCard REQUIRED)
find_package(ZeroMQ REQUIRED)
find_package(Numa)

find_package(FairRoot)
if (FAIRROOT_FOUND)
    find_package(FairMQInFairRoot) # DEPRECATED: This looks for FairMQ embedded in old FairRoot versions,
    # before FairMQ and FairLogger have moved to separate repos.
    # Remove this line, once we require FairMQ 1.2+.
    if(NOT FairMQInFairRoot_FOUND) # DEPRECATED: Remove this condition, once we require FairMQ 1.2+
        find_package(FairMQ REQUIRED)
        find_package(FairLogger REQUIRED)
    endif()
    # this should go away when fairrot provides a proper Find script or proper config scripts
    # See : http://www.cmake.org/cmake/help/v3.0/command/link_directories.html
    link_directories(${FAIRROOT_LIBRARY_DIR})
    ADD_DEFINITIONS(-DWITH_FAIRMQ)
    get_target_property(_boost_incdir Boost::boost INTERFACE_INCLUDE_DIRECTORIES)

    if(FairMQInFairRoot_FOUND)
        # DEPRECATED: Remove this case, once we require FairMQ 1.2+
        get_target_property(_fairmq_incdir FairRoot::FairMQ INTERFACE_INCLUDE_DIRECTORIES)
        o2_define_bucket(NAME fairmq_bucket
                DEPENDENCIES FairRoot::FairMQ
                INCLUDE_DIRECTORIES ${_boost_incdir} ${_fairmq_incdir}
                )
    else()
        get_target_property(_fairmq_incdir FairMQ::FairMQ INTERFACE_INCLUDE_DIRECTORIES)
        get_target_property(_fairlogger_incdir FairLogger::FairLogger INTERFACE_INCLUDE_DIRECTORIES)
        o2_define_bucket(NAME fairmq_bucket
                DEPENDENCIES FairMQ::FairMQ
                INCLUDE_DIRECTORIES ${_boost_incdir} ${_fairmq_incdir} ${_fairlogger_incdir}
                )
        #set(_fairlogger_incdir)
    endif()
else (FAIRROOT_FOUND)
    message(WARNING "FairRoot not found, corresponding classes will not be compiled.")
    o2_define_bucket(NAME fairmq_bucket
            INCLUDE_DIRECTORIES ${_boost_incdir}
            )
endif (FAIRROOT_FOUND)

if (Numa_FOUND)
  ADD_DEFINITIONS(-DWITH_NUMA)
else (Numa_FOUND)
  message(WARNING "Numa not found, corresponding features will be disabled.")
  set(Numa_LIBRARIES "")
  set(Numa_INCLUDE_DIRS "")
endif (Numa_FOUND)

ADD_DEFINITIONS(-DWITH_DATASAMPLING)

o2_define_bucket(
        NAME
        o2_readout_bucket

        DEPENDENCIES
        pthread
        ${Boost_LOG_LIBRARY}
        ${Boost_THREAD_LIBRARY}
        ${Boost_SYSTEM_LIBRARY}
        ${Boost_PROGRAM_OPTIONS_LIBRARY}
        ${Configuration_LIBRARIES}
        ${Monitoring_LIBRARIES}
        ${Common_LIBRARIES}
        ${InfoLogger_LIBRARIES}
        ${ReadoutCard_LIBRARIES}
        ${DataSampling_LIBRARIES}
        ${Numa_LIBRARIES}

        SYSTEMINCLUDE_DIRECTORIES
        ${Boost_INCLUDE_DIRS}
        ${Monitoring_INCLUDE_DIRS}
        ${InfoLogger_INCLUDE_DIRS}
        ${ReadoutCard_INCLUDE_DIRS}
        ${DataSampling_INCLUDE_DIRS}
        ${Configuration_INCLUDE_DIRS}
        ${Numa_INCLUDE_DIRS}
)


o2_define_bucket(
        NAME
        o2_readout_with_fair

        DEPENDENCIES
        o2_readout_bucket
        ${ROOT_LIBRARIES}
        fairmq_bucket

        SYSTEMINCLUDE_DIRECTORIES
        ${FAIRROOT_INCLUDE_DIR}
        ${FAIRROOT_INCLUDE_DIR}/fairmq
)
