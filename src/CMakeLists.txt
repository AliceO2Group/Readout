set(APP_HEADERS
)

set(APP_SOURCES
  mainReadout.cxx
)

# Set the build version. It will be used in the name of the lib, with corresponding
# symlinks created. SOVERSION could also be specified for api version. 
set_target_properties(${LIBRARY_NAME} PROPERTIES
  VERSION ${VERSION}  # ${VERSION} was defined in the main CMakeLists.
  FRAMEWORK FALSE
  PUBLIC_HEADER "${LIB_HEADERS}" # specify the public headers
)

if (FAIRROOT_FOUND) 
  ADD_DEFINITIONS(-DWITH_FAIRMQ)
  link_directories(${FAIRROOT_LIBRARY_DIR})
  set(FAIRROOT_LIBRARIES ${Boost_PROGRAM_OPTIONS_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_THREAD_LIBRARY} ${Boost_SYSTEM_LIBRARY} Base FairMQ BaseMQ fairmq_logger ${ROOT_LIBRARIES})
endif()

# This is a make target, so you can do a "make projArunner"
set(APPLICATION_NAME readout.exe)
add_executable(${APPLICATION_NAME} ${APP_SOURCES})

# Links the target exe (projArunner) against the library (libprojA) and other required libraries
target_link_libraries(${APPLICATION_NAME} ${LIBRARY_NAME} ${FAIRROOT_LIBRARIES} InfoLogger-static pthread DataFormat Common rorc DataSampling DataCollector)


# Says how and where to install software
install(TARGETS ${LIBRARY_NAME} ${APPLICATION_NAME}
  EXPORT ${PROJECT_NAME}Targets            # for downstream dependencies
  ARCHIVE DESTINATION lib COMPONENT libs   # static lib
  RUNTIME DESTINATION bin COMPONENT libs   # binaries
  LIBRARY DESTINATION lib COMPONENT libs   # shared lib
  PUBLIC_HEADER DESTINATION include/${PROJECT_NAME} COMPONENT devel   # headers (note the different component -> different package)
  FRAMEWORK DESTINATION bin COMPONENT libs # for mac
)

