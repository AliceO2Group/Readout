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
  LANGUAGES NONE
)

# simplified build mode for doc only
if(ONLYDOC)
  add_subdirectory(doc)
  return()
endif()

# global compilation options
enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ensure hard warning checks
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror -Wall -Wextra -Wno-deprecated-declarations")
# -Wno-unused-variable -Wno-unused-parameter -Wno-unused-private-field
# -g -O0

# ensure debug symbols always included
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

# by default, build optimized release code
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# define project directories
# build-time binaries
set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}/bin")
# local sources
set(SOURCE_DIR src)
# installation of libraries
include(GNUInstallDirs)
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
find_package(ReadoutCard)
find_package(Configuration)
find_package(Numa)
find_package(FairMQ)
find_package(ZLIB)
#find_package(RDMA)
find_package(Occ)
find_package(BookkeepingApi)
find_package(ZMQ)
find_package(MySQL)
#find_package(gperftools)
find_package(SDL)

# extract include directories from targets
get_target_property(InfoLogger_INCLUDE_DIRS AliceO2::InfoLogger INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(Common_INCLUDE_DIRS AliceO2::Common INTERFACE_INCLUDE_DIRECTORIES)
if (ReadoutCard_FOUND)
  get_target_property(ReadoutCard_INCLUDE_DIRS AliceO2::ReadoutCard INTERFACE_INCLUDE_DIRECTORIES)
endif()
get_target_property(Monitoring_INCLUDE_DIRS AliceO2::Monitoring INTERFACE_INCLUDE_DIRECTORIES)
if (Configuration_FOUND)
  get_target_property(Configuration_INCLUDE_DIRS AliceO2::Configuration INTERFACE_INCLUDE_DIRECTORIES)
endif()

# check FairMQ
# FairMQ::FairMQ target does not provide the transitive dependency on FairLogger and Boost, so we have to add them manually
find_package(Boost COMPONENTS container thread regex date_time)
find_package(FairLogger)
if ((NOT FairMQ_FOUND) OR (NOT FairLogger_FOUND))
  message(STATUS "FairMQ or FairLogger not found, corresponding features disabled")
else ()
  message(STATUS "FairMQ found: ${FairMQ_DIR}")
  message(STATUS "FairLogger found: ${FairLogger_ROOT}")
  foreach(dep IN LISTS FairLogger_PACKAGE_DEPENDENCIES)
    find_package(${dep} ${FairLogger_${dep}_VERSION} REQUIRED)
  endforeach()
  get_target_property(FairMQ_INCLUDE_DIRS FairMQ::FairMQ INTERFACE_INCLUDE_DIRECTORIES)
  get_target_property(FairLogger_INCLUDE_DIRS FairLogger::FairLogger INTERFACE_INCLUDE_DIRECTORIES)
  if (TARGET fmt::fmt) 
    get_target_property(fmt_INCLUDE_DIRS fmt::fmt INTERFACE_INCLUDE_DIRECTORIES)
  endif ()
  list(APPEND FairMQ_INCLUDE_DIRS ${FairLogger_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS} ${fmt_INCLUDE_DIRS})
endif ()

# check OCC
if (Occ_FOUND)
  message(STATUS "Control-OCCPlugin found")
  get_target_property(Occ_INCLUDE_DIRS AliceO2::Occ INTERFACE_INCLUDE_DIRECTORIES)
else ()
  message(STATUS "Control-OCCPlugin not found, corresponding features will be disabled.")
endif(Occ_FOUND)

# check Bookkeeping
if (BookkeepingApi_FOUND)
  get_target_property(BookkeepingApi_INCLUDES AliceO2::BookkeepingApi INTERFACE_INCLUDE_DIRECTORIES)
  get_target_property(BookkeepingApi_LIBRARIES AliceO2::BookkeepingApi INTERFACE_LINK_LIBRARIES)
  message(STATUS "Bookkeeping found: ${BookkeepingApi_INCLUDES} ${BookkeepingApi_LIBRARIES}")
else ()
  message(STATUS "Bookkeeping not found, corresponding features will be disabled.")
endif(BookkeepingApi_FOUND)

# check ZMQ
if(${ZMQ_FOUND})
    message(
    STATUS
    "ZMQ found: ${ZMQ_INCLUDE_DIRS} ${ZMQ_LIBRARIES}")
else ()
  message(STATUS "ZMQ not found, corresponding features will be disabled.")
endif()

# check MySQL
if(${MYSQL_FOUND})
    message(
    STATUS
    "MySQL found: ${MYSQL_INCLUDE_DIRS} ${MYSQL_LIBRARIES}")
else ()
  message(STATUS "MySQL not found, corresponding features will be disabled.")
endif()

# check Gperftools
if(${gperftools_FOUND})
  message(STATUS "gperftools enabled")
endif()

# check SDL
if(SDL_FOUND)
  message(
    STATUS
    "SDL found: ${SDL_INCLUDE_DIRS} ${SDL_LIBRARIES}")
  else ()
    message(STATUS "SDL not found, corresponding features will be disabled.")
endif()


# add flags to enable optional features in Readout, based on available dependencies
add_compile_definitions($<$<BOOL:${Numa_FOUND}>:WITH_NUMA> $<$<BOOL:${RDMA_FOUND}>:WITH_RDMA> $<$<BOOL:${Configuration_FOUND}>:WITH_CONFIG> $<$<BOOL:${FairMQ_FOUND}>:WITH_FAIRMQ> $<$<BOOL:${Occ_FOUND}>:WITH_OCC> $<$<BOOL:${BookkeepingApi_FOUND}>:WITH_LOGBOOK> $<$<BOOL:${ZMQ_FOUND}>:WITH_ZMQ> $<$<BOOL:${MYSQL_FOUND}>:WITH_DB> $<$<BOOL:${ReadoutCard_FOUND}>:WITH_READOUTCARD> $<$<BOOL:${gperftools_FOUND}>:WITH_GPERFTOOLS> $<$<BOOL:${SDL_FOUND}>:WITH_SDL>)

# define include directories
set(READOUT_INCLUDE_DIRS
 ${Boost_INCLUDE_DIRS} 
 ${InfoLogger_INCLUDE_DIRS}
 ${ReadoutCard_INCLUDE_DIRS}
 ${Common_INCLUDE_DIRS}
 ${Monitoring_INCLUDE_DIRS}
 ${Configuration_INCLUDE_DIRS}
 ${FairMQ_INCLUDE_DIRS}
 ${Occ_INCLUDE_DIRS}
 ${BookkeepingApi_INCLUDES}
)

# define liraries to be linked
set(READOUT_LINK_LIBRARIES
  AliceO2::InfoLogger
  AliceO2::Common
  AliceO2::Monitoring
  $<$<BOOL:${ReadoutCard_FOUND}>:AliceO2::ReadoutCard>
  $<$<BOOL:${Configuration_FOUND}>:AliceO2::Configuration>
  $<$<BOOL:${FairMQ_FOUND}>:FairMQ::FairMQ>
  $<$<BOOL:${Occ_FOUND}>:AliceO2::Occ>
  $<$<BOOL:${BookkeepingApi_FOUND}>:AliceO2::BookkeepingApi>
)
if(Numa_FOUND)
  list(APPEND READOUT_LINK_LIBRARIES ${Numa_LIBRARIES})
endif()
if(RDMA_FOUND)
  list(APPEND READOUT_LINK_LIBRARIES ${RDMA_LIBRARIES})
endif()
if(ZMQ_FOUND)
  list(APPEND READOUT_INCLUDE_DIRS ${ZMQ_INCLUDE_DIRS}) 
  list(APPEND READOUT_LINK_LIBRARIES ${ZMQ_LIBRARIES})
endif()
if(MYSQL_FOUND)
  list(APPEND READOUT_INCLUDE_DIRS ${MYSQL_INCLUDE_DIRS}) 
  list(APPEND READOUT_LINK_LIBRARIES ${MYSQL_LIBRARIES})
endif()
if(gperftools_FOUND)
  list(APPEND READOUT_LINK_LIBRARIES ${gperftools_LIBRARIES})
endif()

# some systems don't need an explicit library to have dlopen()
if (CMAKE_DL_LIBS)
  list(APPEND READOUT_LINK_LIBRARIES ${CMAKE_DL_LIBS})
endif()

####################################
# Handle RPATH
####################################

# Build targets with install rpath on Mac to dramatically speed up installation
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}" isSystemDir)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	if("${isSystemDir}" STREQUAL "-1")
		set(CMAKE_INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
	endif()
	set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif()
unset(isSystemDir)

###################################################
# group readout features in (reusable) objects
###################################################

# equipments
add_library(
        objReadoutEquipment OBJECT
        ${SOURCE_DIR}/ReadoutEquipment.cxx
        ${SOURCE_DIR}/ReadoutEquipmentDummy.cxx
        $<$<BOOL:${ReadoutCard_FOUND}>:${SOURCE_DIR}/ReadoutEquipmentRORC.cxx>
        ${SOURCE_DIR}/ReadoutEquipmentCruEmulator.cxx
        ${SOURCE_DIR}/ReadoutEquipmentPlayer.cxx
        $<$<BOOL:${ZMQ_FOUND}>:${SOURCE_DIR}/ReadoutEquipmentZmq.cxx>
)
target_include_directories(objReadoutEquipment PRIVATE ${READOUT_INCLUDE_DIRS})

# aggregators
add_library(
        objReadoutAggregator OBJECT
        ${SOURCE_DIR}/DataBlockAggregator.cxx
)
target_include_directories(objReadoutAggregator PRIVATE ${READOUT_INCLUDE_DIRS})

# utilities, including memory handling
add_library(
        objReadoutUtils OBJECT
        ${SOURCE_DIR}/readoutInfoLogger.cxx
        ${SOURCE_DIR}/ReadoutUtils.cxx
        ${SOURCE_DIR}/RdhUtils.cxx
        ${SOURCE_DIR}/CounterStats.cxx
        ${SOURCE_DIR}/MemoryHandler.cxx
	${SOURCE_DIR}/SocketTx.cxx
        ${SOURCE_DIR}/MemoryBank.cxx
        ${SOURCE_DIR}/MemoryBankManager.cxx
        ${SOURCE_DIR}/MemoryPagesPool.cxx
	${SOURCE_DIR}/ReadoutMonitoringQueue.cxx
	$<$<BOOL:${ZMQ_FOUND}>:${SOURCE_DIR}/ZmqServer.cxx>
	$<$<BOOL:${ZMQ_FOUND}>:${SOURCE_DIR}/ZmqClient.cxx>
)
target_include_directories(objReadoutUtils PRIVATE ${READOUT_INCLUDE_DIRS})

# internal readout database
if (MYSQL_FOUND)
  add_library(
          objReadoutDatabase OBJECT
	  ${SOURCE_DIR}/ReadoutDatabase.cxx
  )
  target_include_directories(objReadoutDatabase PRIVATE ${READOUT_INCLUDE_DIRS} ${MYSQL_INCLUDE_DIRS})
endif()

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
        $<$<BOOL:${ZMQ_FOUND}>:${SOURCE_DIR}/ConsumerZmq.cxx>
)
target_include_directories(objReadoutConsumers PRIVATE ${READOUT_INCLUDE_DIRS})


# processor libraries

# ZLIB compression
find_package(ZLIB)
if (ZLIB_FOUND)
  add_library(
          O2ReadoutProcessorZlibCompress
          SHARED
          ${SOURCE_DIR}/ProcessorZlibCompress.cxx
  )
  target_include_directories(O2ReadoutProcessorZlibCompress PRIVATE ${READOUT_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS})
  target_link_libraries(O2ReadoutProcessorZlibCompress ZLIB::ZLIB)
  list(APPEND libraries O2ReadoutProcessorZlibCompress)
endif()

# LZ4 compression
find_library (LZ4_LIB lz4 ${LZ4_DIR}/lib)
find_path (LZ4_INCLUDE_DIR NAMES lz4.h PATHS ${LZ4_DIR}/include)
if ( LZ4_LIB AND LZ4_INCLUDE_DIR )
  message(STATUS "Found lz4 (library: ${LZ4_LIB} include: ${LZ4_INCLUDE_DIR})")
  add_library(
    O2ReadoutProcessorLZ4Compress
    SHARED
    ${SOURCE_DIR}/ProcessorLZ4Compress.cxx
  )
  target_include_directories(O2ReadoutProcessorLZ4Compress PRIVATE ${READOUT_INCLUDE_DIRS} ${LZ4_INCLUDE_DIR})
  target_link_libraries(O2ReadoutProcessorLZ4Compress ${LZ4_LIB})
  list(APPEND libraries O2ReadoutProcessorLZ4Compress)
  list(APPEND READOUT_INCLUDE_DIRS ${LZ4_INCLUDE_DIR})
  list(APPEND READOUT_LINK_LIBRARIES ${LZ4_LIB})
  set_property(TARGET O2ReadoutProcessorLZ4Compress PROPERTY POSITION_INDEPENDENT_CODE ON)
else()
  message(STATUS "lz4 not found")
endif()



# some systems don't like creating libs with undefined symbols
set(no_lookup_compilers "Clang" "AppleClang")
if (${CMAKE_CXX_COMPILER_ID} IN_LIST no_lookup_compilers)
  foreach(lib ${libraries})
    set_target_properties(${lib} PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
  endforeach()
endif()


###################################################
# executables
###################################################

# list of executables build (to be completed depending on dependencies found)
set(executables o2-readout-exe o2-readout-receiver o2-readout-test-fmq-tx o2-readout-test-fmq-rx o2-readout-test-fmq-perf-tx o2-readout-test-fmq-perf-rx o2-readout-test-memorybanks o2-readout-rawreader o2-readout-rawmerger o2-readout-test-lib-monitoring)

# o2-readout-exe : main executable
add_executable(
	o2-readout-exe
	${SOURCE_DIR}/mainReadout.cxx
        ${SOURCE_DIR}/ReadoutStats.cxx
	$<TARGET_OBJECTS:objReadoutEquipment>
	$<TARGET_OBJECTS:objReadoutAggregator>
	$<TARGET_OBJECTS:objReadoutConsumers>
	$<TARGET_OBJECTS:objReadoutUtils>
	$<$<BOOL:${MYSQL_FOUND}>:$<TARGET_OBJECTS:objReadoutDatabase>>
)

# a test FMQ device receiver
add_executable(
	o2-readout-receiver
        ${SOURCE_DIR}/receiverFMQ.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)

# a test FMQ channel sender
add_executable(
	o2-readout-test-fmq-tx
        ${SOURCE_DIR}/testTxFMQ.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)

# a test FMQ channel receiver
add_executable(
	o2-readout-test-fmq-rx
        ${SOURCE_DIR}/testRxFMQ.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)

# Tx/Rx FMQ channel performance testers
add_executable(
	o2-readout-test-fmq-perf-tx
        ${SOURCE_DIR}/testTxPerfFMQ.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)
add_executable(
	o2-readout-test-fmq-perf-rx
        ${SOURCE_DIR}/testRxPerfFMQ.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)

# a test for FMQ memory
add_executable(
	o2-readout-test-fmq-memory
        ${SOURCE_DIR}/testFmqMemory.cxx
)


# a test to check memory banks
add_executable(
        o2-readout-test-memorybanks
        ${SOURCE_DIR}/testMemoryBanks.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)

# a RAW data file reader/checker
add_executable(
        o2-readout-rawreader
        ${SOURCE_DIR}/readRaw.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)

# a RAW data file merger
add_executable(
        o2-readout-rawmerger
        ${SOURCE_DIR}/exeRawMerger.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)

# a minimal test program to readout a ROC device
if (ReadoutCard_FOUND)
add_executable(
	o2-readout-test-roc
        ${SOURCE_DIR}/testROC.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)
add_executable(
	o2-readout-test-roc-find
        ${SOURCE_DIR}/testROCfind.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)
endif()

# a test program to test ZMQ client
if (${ZMQ_FOUND})
add_executable(
	o2-readout-test-zmq-client
        ${SOURCE_DIR}/testZmqClient.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)
add_executable(
	o2-readout-test-zmq-server
        ${SOURCE_DIR}/testZmqServer.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)
add_executable(
	o2-readout-eventdump
        ${SOURCE_DIR}/eventDump.cxx
	$<TARGET_OBJECTS:objReadoutUtils>
)

# a process to monitor stats of remote readout processes
add_executable(
	o2-readout-monitor
        ${SOURCE_DIR}/readoutMonitor.cxx
	${SOURCE_DIR}/SocketRx.cxx
	$<$<BOOL:${ZMQ_FOUND}>:${SOURCE_DIR}/ZmqClient.cxx>
)
target_include_directories(o2-readout-monitor PRIVATE ${READOUT_INCLUDE_DIRS})
target_link_libraries(o2-readout-monitor PRIVATE AliceO2::InfoLogger AliceO2::Common ${ZMQ_LIBRARIES})

# a process to send synchronization signals for replay mode
add_executable(
	o2-readout-sync-server
	${SOURCE_DIR}/syncServer.cxx
)
target_include_directories(o2-readout-sync-server PRIVATE ${READOUT_INCLUDE_DIRS})
target_link_libraries(o2-readout-sync-server PRIVATE AliceO2::InfoLogger AliceO2::Common ${ZMQ_LIBRARIES})

add_executable(
	o2-readout-test-sync-client
        ${SOURCE_DIR}/testSyncClient.cxx
)
target_include_directories(o2-readout-test-sync-client PRIVATE ${READOUT_INCLUDE_DIRS})
target_link_libraries(o2-readout-test-sync-client PRIVATE AliceO2::InfoLogger AliceO2::Common ${ZMQ_LIBRARIES})



if(SDL_FOUND AND ZMQ_FOUND)
# a process to monitor memory of remote readout processes
add_executable(
	o2-readout-monitor-memory
        ${SOURCE_DIR}/readoutMemoryMonitor.cxx
)
target_include_directories(o2-readout-monitor-memory PRIVATE ${READOUT_INCLUDE_DIRS})
target_link_libraries(o2-readout-monitor-memory PRIVATE AliceO2::InfoLogger AliceO2::Common ${ZMQ_LIBRARIES} $<$<BOOL:${SDL_FOUND}>:${SDL_LIBRARIES}> SDL2)
list (APPEND executables o2-readout-monitor-memory)
endif()

# a process to admin readout database
if (MYSQL_FOUND)
add_executable(
	o2-readout-admin-db
        ${SOURCE_DIR}/readoutAdminDB.cxx
	$<TARGET_OBJECTS:objReadoutDatabase>
)
target_include_directories(o2-readout-admin-db PRIVATE ${READOUT_INCLUDE_DIRS})
list (APPEND executables o2-readout-admin-db)
endif()

endif()

# a minimal test program to check Monitoring library
add_executable(
	o2-readout-test-lib-monitoring
        ${SOURCE_DIR}/testMonitor.cxx
)

# a minimal test program to check Bookkeeping library
if (BookkeepingApi_FOUND)
add_executable(
        o2-readout-test-lib-bookkeeping
	${SOURCE_DIR}/testBookkeeping.cxx
)
endif()

# a test program to dispatch to connected clients copy of input stream
add_executable(
	o2-readout-test-net-dispatch
        ${SOURCE_DIR}/testNetDispatch.cxx
	${SOURCE_DIR}/SocketRx.cxx
)
target_link_libraries(o2-readout-test-net-dispatch PRIVATE AliceO2::InfoLogger AliceO2::Common)

# disable some executables when corresponding dependencies not found
if (NOT FairMQ_FOUND)
	set_target_properties(o2-readout-test-fmq-tx o2-readout-test-fmq-rx o2-readout-test-fmq-perf-tx o2-readout-test-fmq-perf-rx  o2-readout-test-fmq-memory PROPERTIES EXCLUDE_FROM_ALL 1)
endif ()

# set include and libraries for all
if (ReadoutCard_FOUND)
  list (APPEND executables o2-readout-test-roc o2-readout-test-roc-find)
endif()
if (${ZMQ_FOUND})
  list (APPEND executables o2-readout-test-zmq-client o2-readout-test-zmq-server o2-readout-eventdump)
endif()
if (BookkeepingApi_FOUND)
  list (APPEND executables o2-readout-test-lib-bookkeeping)
endif()
foreach (exe ${executables})
	target_include_directories(${exe} PRIVATE ${READOUT_INCLUDE_DIRS})
	target_link_libraries(${exe} PRIVATE ${READOUT_LINK_LIBRARIES})
endforeach ()
if (${ZMQ_FOUND})
  list (APPEND executables o2-readout-monitor)
endif()

# special minimal build for test-fmq-memory (to avoid grpc garbage from occ when using valgrind)
foreach (exe o2-readout-test-fmq-memory)
  list (APPEND executables ${exe})
  target_include_directories(${exe} PRIVATE ${READOUT_INCLUDE_DIRS})
  target_link_libraries(${exe} PRIVATE $<$<BOOL:${FairMQ_FOUND}>:FairMQ::FairMQ>)
endforeach()

###################################################
# files installation
###################################################

install(
        FILES readout.cfg readout-cru.cfg readout-dummy.cfg readout-player.cfg readout-dcs.cfg readout-qc.cfg readout-stfb-emu.cfg
        DESTINATION ${CMAKE_INSTALL_PREFIX}/etc/o2.d/readout
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

install(
	PROGRAMS src/readoutAutoConfigure.tcl
	DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
	RENAME o2-readout-config-generator
)

install(
	PROGRAMS src/readoutConfigEditor.tcl
	DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
	RENAME o2-readout-config-editor
)

install(
	PROGRAMS src/readoutStatus.tcl
	DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
	RENAME o2-readout-status
)

install(
	PROGRAMS src/readoutCommandLauncher.sh
	DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
	RENAME o2-readout-command-launcher
)
