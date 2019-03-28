####################################
# cmake Readout
####################################

# cmake settings
CMAKE_MINIMUM_REQUIRED(VERSION 3.8.0 FATAL_ERROR)
cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})

# define project
project(
  Readout
  DESCRIPTION "O2 Readout process"
  LANGUAGES CXX
)

# global compilation options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# by default, build optimized release code
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# define project directories
# build-time binaries
set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}/bin")
# local sources
set(SOURCE_DIR src)
# installation of libraries
set(CMAKE_INSTALL_LIBDIR lib)

# add local modules directory
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# add subdirectories
add_subdirectory(doc)



###################################################
# dependencies
###################################################

# dependencies to build Readout
find_package(Common REQUIRED)
find_package(InfoLogger REQUIRED)
find_package(Monitoring REQUIRED)
find_package(ReadoutCard REQUIRED)
find_package(Configuration)
find_package(Numa)
find_package(FairMQ)
find_package(ZLIB)
#find_package(RDMA)

# extract include directories from targets
get_target_property(InfoLogger_INCLUDE_DIRS AliceO2::InfoLogger INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(Common_INCLUDE_DIRS AliceO2::Common INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(ReadoutCard_INCLUDE_DIRS AliceO2::ReadoutCard INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(Monitoring_INCLUDE_DIRS AliceO2::Monitoring INTERFACE_INCLUDE_DIRECTORIES)
if (Configuration_FOUND)
  get_target_property(Configuration_INCLUDE_DIRS AliceO2::Configuration INTERFACE_INCLUDE_DIRECTORIES)
endif()

# check FairMQ
# FairMQ::FairMQ target does not provide the transitive dependency on FairLogger and Boost, so we have to add them manually
find_package(Boost COMPONENTS container thread regex date_time signals )
find_package(FairLogger)
if ((NOT FairMQ_FOUND) OR (NOT FairLogger_FOUND))
  message(STATUS "FairMQ or FairLogger not found, corresponding features disabled")
else ()
  message(STATUS "FairMQ found: ${FairMQ_ROOT}")
  message(STATUS "FairLogger found: ${FairLogger_ROOT}")
  get_target_property(FairMQ_INCLUDE_DIRS FairMQ::FairMQ INTERFACE_INCLUDE_DIRECTORIES)
  get_target_property(FairLogger_INCLUDE_DIRS FairLogger::FairLogger INTERFACE_INCLUDE_DIRECTORIES)
  list(APPEND FairMQ_INCLUDE_DIRS ${FairLogger_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
endif ()

# add flags to enable optional features in Readout, based on available dependencies
add_compile_definitions($<$<BOOL:${Numa_FOUND}>:WITH_NUMA> $<$<BOOL:${RDMA_FOUND}>:WITH_RDMA> $<$<BOOL:${Configuration_FOUND}>:WITH_CONFIG> $<$<BOOL:${FairMQ_FOUND}>:WITH_FAIRMQ> )

#################
# temporary hacks to fix non-working AliceO2::ReadoutCard target
# library
if ("${ReadoutCard_LIBRARIES}" STREQUAL "")
	find_library (ReadoutCard_LIBRARIES ReadoutCard NO_DEFAULT_PATH PATHS "${ReadoutCard_INCLUDE_DIRS}/.." PATH_SUFFIXES "lib" "lib64")
	message(STATUS "ReadoutCard_LIBRARIES = ${ReadoutCard_LIBRARIES}")
endif()
#get_target_property(ReadoutCard_LIBS AliceO2::ReadoutCard INTERFACE_LINK_LIBRARIES)
#message("ReadoutCard_LIBS=${ReadoutCard_LIBS}")
# includes
list(APPEND ReadoutCard_INCLUDE_DIRS ${Boost_INCLUDE_DIRS})
#################


# define include directories
set(READOUT_INCLUDE_DIRS
 ${InfoLogger_INCLUDE_DIRS}
 ${ReadoutCard_INCLUDE_DIRS}
 ${Common_INCLUDE_DIRS}
 ${Monitoring_INCLUDE_DIRS}
 ${Configuration_INCLUDE_DIRS}
 ${FairMQ_INCLUDE_DIRS}
)

# define liraries to be linked
set(READOUT_LINK_LIBRARIES
  ${CMAKE_DL_LIBS}
  AliceO2::InfoLogger
  AliceO2::Common
  AliceO2::Monitoring
#  AliceO2::ReadoutCard
  ${ReadoutCard_LIBRARIES}
  $<$<BOOL:${Configuration_FOUND}>:AliceO2::Configuration>
  $<$<BOOL:${FairMQ_FOUND}>:FairMQ::FairMQ>
  ${Numa_LIBRARIES}
  ${RDMA_LIBRARIES}
)



###################################################
# group readout features in (reusable) objects
###################################################

# equipments
add_library(
        objReadoutEquipment OBJECT
        ${SOURCE_DIR}/ReadoutEquipment.cxx
        ${SOURCE_DIR}/ReadoutEquipmentDummy.cxx
        ${SOURCE_DIR}/ReadoutEquipmentRORC.cxx
        ${SOURCE_DIR}/ReadoutEquipmentCruEmulator.cxx
)
target_include_directories(objReadoutEquipment PRIVATE ${READOUT_INCLUDE_DIRS})

# aggregators
add_library(
        objReadoutAggregator OBJECT
        ${SOURCE_DIR}/DataBlockAggregator.cxx
)
target_include_directories(objReadoutAggregator PRIVATE ${READOUT_INCLUDE_DIRS})

# utilities
add_library(
        objReadoutUtils OBJECT
        ${SOURCE_DIR}/ReadoutUtils.cxx
        ${SOURCE_DIR}/RdhUtils.cxx
        ${SOURCE_DIR}/CounterStats.cxx
        ${SOURCE_DIR}/MemoryHandler.cxx
	${SOURCE_DIR}/SocketTx.cxx
)
target_include_directories(objReadoutUtils PRIVATE ${READOUT_INCLUDE_DIRS})

# memory handling
add_library(
        objMemUtils OBJECT
        ${SOURCE_DIR}/MemoryBank.cxx
        ${SOURCE_DIR}/MemoryBankManager.cxx
        ${SOURCE_DIR}/MemoryPagesPool.cxx
)
target_include_directories(objMemUtils PRIVATE ${READOUT_INCLUDE_DIRS})

# consumers
add_library(
        objReadoutConsumers OBJECT
)
target_sources(
	objReadoutConsumers
	PRIVATE
        ${SOURCE_DIR}/Consumer.cxx
        ${SOURCE_DIR}/ConsumerStats.cxx
        ${SOURCE_DIR}/ConsumerFileRecorder.cxx
        ${SOURCE_DIR}/ConsumerDataChecker.cxx
        ${SOURCE_DIR}/ConsumerDataProcessor.cxx
        ${SOURCE_DIR}/ConsumerTCP.cxx
	$<$<BOOL:${FairMQ_FOUND}>:${SOURCE_DIR}/ConsumerFMQ.cxx ${SOURCE_DIR}/ConsumerFMQchannel.cxx ${SOURCE_DIR}/ConsumerDataSampling.cxx>
	$<$<BOOL:${RDMA_FOUND}>:${SOURCE_DIR}/ConsumerRDMA.cxx>
)
target_include_directories(objReadoutConsumers PRIVATE ${READOUT_INCLUDE_DIRS})


# processor libraries
find_package(ZLIB)
if (ZLIB_FOUND)
  add_library(
          ProcessorZlibCompress
          SHARED
          ${SOURCE_DIR}/ProcessorZlibCompress.cxx
  )
  target_include_directories(ProcessorZlibCompress PRIVATE ${READOUT_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS})
  target_link_libraries(ProcessorZlibCompress ZLIB::ZLIB)
  list(APPEND libraries ProcessorZlibCompress)
endif()


###################################################
# executables
###################################################

# readout.exe : main executable
add_executable(
	readout.exe
	${SOURCE_DIR}/mainReadout.cxx
	$<TARGET_OBJECTS:objReadoutEquipment>
	$<TARGET_OBJECTS:objReadoutAggregator>
	$<TARGET_OBJECTS:objReadoutConsumers>
	$<TARGET_OBJECTS:objReadoutUtils>
	$<TARGET_OBJECTS:objMemUtils>
)

# a test FMQ device receiver
add_executable(
	receiverFMQ.exe
        ${SOURCE_DIR}/receiverFMQ.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)

# a test FMQ channel sender
add_executable(
	testTxFMQ.exe
        ${SOURCE_DIR}/testTxFMQ.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)

# a test FMQ channel receiver
add_executable(
	testRxFMQ.exe
        ${SOURCE_DIR}/testRxFMQ.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)

# a test to check memory banks
add_executable(
        testMemoryBanks.exe
        ${SOURCE_DIR}/testMemoryBanks.cxx
	$<TARGET_OBJECTS:objMemUtils>
)

# a RAW data file reader/checker
add_executable(
        readRaw.exe
        ${SOURCE_DIR}/readRaw.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)

# a minimal test program to readout a ROC device
add_executable(
	testROC.exe
        ${SOURCE_DIR}/testROC.cxx
	$<TARGET_OBJECTS:objMemUtils>
)

# a minimal test program to check Monitoring library
add_executable(
	testMonitor.exe
        ${SOURCE_DIR}/testMonitor.cxx
)

# disable some executables when corresponding dependencies not found
if (NOT FAIRMQ_FOUND)
	set_target_properties(testTxFMQ.exe testRxFMQ.exe PROPERTIES EXCLUDE_FROM_ALL 1)
endif ()

# set include and libraries for all
set(executables readout.exe receiverFMQ.exe testTxFMQ.exe testRxFMQ.exe testMemoryBanks.exe readRaw.exe testROC.exe testMonitor.exe)
foreach (exe ${executables})
	target_include_directories(${exe} PRIVATE ${READOUT_INCLUDE_DIRS})
	target_link_libraries(${exe} PRIVATE ${READOUT_LINK_LIBRARIES})
endforeach ()



###################################################
# files installation
###################################################

install(
        FILES readout.cfg
        DESTINATION ${CMAKE_INSTALL_PREFIX}/etc
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
)
	
install(
        FILES ${SOURCE_DIR}/RAWDataHeader.h
        DESTINATION ${CMAKE_INSTALL_PREFIX}/include/${MODULE_NAME}
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
)

foreach (exe ${executables})
	get_target_property(IS_EXCLUDED ${exe} EXCLUDE_FROM_ALL)
	if(NOT ${IS_EXCLUDED})
		install(TARGETS ${exe} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
	endif ()
endforeach ()

foreach (lib ${libraries})
	install(TARGETS ${lib} DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})
endforeach ()
