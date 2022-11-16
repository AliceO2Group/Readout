#!/usr/bin/wish
# This tool allows editing of readout configuration


# get config file to be edited
set configFile [lindex $argv 0]
if {[string length $configFile]==0} {
  puts "Please provide a configuration file"
  exit 1
}
#puts "Using configuration $configFile"


# Static description of readout configuration parameters, as defined in configurationParameters.md
set configurationParametersDescriptor {
| Section | Parameter name  | Type | Default value | Description |
|--|--|--|--|--|
| bank-* | enabled | int | 1 | Enable (1) or disable (0) the memory bank. |
| bank-* | numaNode | int | -1| Numa node where memory should be allocated. -1 means unspecified (system will choose). |
| bank-* | size | bytes | | Size of the memory bank, in bytes. |
| bank-* | type | string| | Support used to allocate memory. Possible values: malloc, MemoryMappedFile. |
| consumer-* | consumerOutput | string |  | Name of the consumer where the output of this consumer (if any) should be pushed. |
| consumer-* | consumerType | string |  | The type of consumer to be instanciated. One of:stats, FairMQDevice, DataSampling, FairMQChannel, fileRecorder, checker, processor, tcp. |
| consumer-* | enabled | int | 1 | Enable (value=1) or disable (value=0) the consumer. |
| consumer-* | filterEquipmentIdsExclude | string |  | Defines a filter based on equipment ids. All data belonging to the equipments in this list (coma separated values) are rejected. |
| consumer-* | filterEquipmentIdsInclude | string |  | Defines a filter based on equipment ids. Only data belonging to the equipments in this list (coma separated values) are accepted. If empty, all equipment ids are fine. |
| consumer-* | filterLinksExclude | string |  | Defines a filter based on link ids. All data belonging to the links in this list (coma separated values) are rejected. |
| consumer-* | filterLinksInclude | string |  | Defines a filter based on link ids. Only data belonging to the links in this list (coma separated values) are accepted. If empty, all link ids are fine. |
| consumer-* | name | string| | Name used to identify this consumer (in logs). By default, it takes the name of the configuration section, consumer-xxx |
| consumer-* | numaNode | int | -1 | If set (>=0), memory / thread will try to use given NUMA node. |
| consumer-* | stopOnError | int | 0 | If 1, readout will stop automatically on consumer error. |
| consumer-data-sampling-* | address | string | ipc:///tmp/readout-pipe-1 | Address of the data sampling. |
| consumer-FairMQChannel-* | checkResources | string | | Check beforehand if unmanaged region would fit in given list of resources. Comma-separated list of items to be checked: eg /dev/shm, MemFree, MemAvailable. (any filesystem path, and any /proc/meminfo entry).|
| consumer-FairMQChannel-* | disableSending | int | 0 | If set, no data is output to FMQ channel. Used for performance test to create FMQ shared memory segment without pushing the data. |
| consumer-FairMQChannel-* | enablePackedCopy | int | 1 | If set, the same superpage may be reused (space allowing) for the copy of multiple HBF (instead of a separate one for each copy). This allows a reduced memoryPoolNumberOfPages. |
| consumer-FairMQChannel-* | enableRawFormat | int | 0 | If 0, data is pushed 1 STF header + 1 part per HBF. If 1, data is pushed in raw format without STF headers, 1 FMQ message per data page. If 2, format is 1 STF header + 1 part per data page.|
| consumer-FairMQChannel-* | fmq-address | string | ipc:///tmp/pipe-readout | Address of the FMQ channel. Depends on transportType. c.f. FairMQ::FairMQChannel.h |
| consumer-FairMQChannel-* | fmq-name | string | readout | Name of the FMQ channel. c.f. FairMQ::FairMQChannel.h |
| consumer-FairMQChannel-* | fmq-progOptions | string |  | Additional FMQ program options parameters, as a comma-separated list of key=value pairs. |
| consumer-FairMQChannel-* | fmq-transport | string | shmem | Name of the FMQ transport. Typically: zeromq or shmem. c.f. FairMQ::FairMQChannel.h |
| consumer-FairMQChannel-* | fmq-type | string | pair | Type of the FMQ channel. Typically: pair. c.f. FairMQ::FairMQChannel.h |
| consumer-FairMQChannel-* | memoryBankName | string |  | Name of the memory bank to crete (if any) and use. This consumer has the special property of being able to provide memory banks to readout, as the ones defined in bank-*. It creates a memory region optimized for selected transport and to be used for readout device DMA. |
| consumer-FairMQChannel-* | memoryPoolNumberOfPages | int | 100 | c.f. same parameter in bank-*. |
| consumer-FairMQChannel-* | memoryPoolPageSize | bytes | 128k | c.f. same parameter in bank-*. |
| consumer-FairMQChannel-* | sessionName | string | default | Name of the FMQ session. c.f. FairMQ::FairMQChannel.h |
| consumer-FairMQChannel-* | threads | int | 0 | If set, a pool of thread is created for the data processing. |
| consumer-FairMQChannel-* | unmanagedMemorySize | bytes |  | Size of the memory region to be created. c.f. FairMQ::FairMQUnmanagedRegion.h. If not set, no special FMQ memory region is created. |
| consumer-fileRecorder-* | bytesMax | bytes | 0 | Maximum number of bytes to write to each file. Data pages are never truncated, so if writing the full page would exceed this limit, no data from that page is written at all and file is closed. If zero (default), no maximum size set.|
| consumer-fileRecorder-* | dataBlockHeaderEnabled | int | 0 | Enable (1) or disable (0) the writing to file of the internal readout header (Readout DataBlock.h) between the data pages, to easily navigate through the file without RDH decoding. If disabled, the raw data pages received from CRU are written without further formatting. |
| consumer-fileRecorder-* | dropEmptyHBFrames | int | 0 | If 1, memory pages are scanned and empty HBframes are discarded, i.e. couples of packets which contain only RDH, the first one with pagesCounter=0 and the second with stop bit set. This setting does not change the content of in-memory data pages, other consumers would still get full data pages with empty packets. This setting is meant to reduce the amount of data recorded for continuous detectors in triggered mode.|
| consumer-fileRecorder-* | fileName | string | | Path to the file where to record data. The following variables are replaced at runtime: ${XXX} -> get variable XXX from environment, %t -> unix timestamp (seconds since epoch), %T -> formatted date/time, %i -> equipment ID of each data chunk (used to write data from different equipments to different output files), %l -> link ID (used to write data from different links to different output files). |
| consumer-fileRecorder-* | filesMax | int | 1 | If 1 (default), file splitting is disabled: file is closed whenever a limit is reached on a given recording stream. Otherwise, file splitting is enabled: whenever the current file reaches a limit, it is closed an new one is created (with an incremental name). If <=0, an unlimited number of incremental chunks can be created. If non-zero, it defines the maximum number of chunks. The file name is suffixed with chunk number (by default, ".001, .002, ..." at the end of the file name. One may use "%f" in the file name to define where this incremental file counter is printed. |
| consumer-fileRecorder-* | pagesMax | int | 0 | Maximum number of data pages accepted by recorder. If zero (default), no maximum set.|
| consumer-fileRecorder-* | tfMax | int | 0 | Maximum number of timeframes accepted by recorder. If zero (default), no maximum set.|
| consumer-processor-* | ensurePageOrder | int | 0 | If set, ensures that data pages goes out of the processing pool in same order as input (which is not guaranteed with multithreading otherwise). This option adds latency. |
| consumer-processor-* | libraryPath | string | | Path to the library file providing the processBlock() function to be used. |
| consumer-processor-* | numberOfThreads | int | 1 | Number of threads running the processBlock() function in parallel. |
| consumer-processor-* | threadIdleSleepTime | int | 1000 | Sleep time (microseconds) of inactive thread, before polling for next data. |
| consumer-processor-* | threadInputFifoSize | int | 10 | Size of input FIFO, where pending data are waiting to be processed. |
| consumer-rdma-* | host | string | localhost | Remote server IP name to connect to. |
| consumer-rdma-* | port | int | 10001 | Remote server TCP port number to connect to. |
| consumer-stats-* | consoleUpdate | int | 0 | If non-zero, periodic updates also output on the log console (at rate defined in monitoringUpdatePeriod). If zero, periodic log output is disabled. |
| consumer-stats-* | monitoringEnabled | int | 0 | Enable (1) or disable (0) readout monitoring. |
| consumer-stats-* | monitoringUpdatePeriod | double | 10 | Period of readout monitoring updates, in seconds. |
| consumer-stats-* | monitoringURI | string | | URI to connect O2 monitoring service. c.f. o2::monitoring. |
| consumer-stats-* | processMonitoringInterval | int | 0 | Period of process monitoring updates (O2 standard metrics). If zero (default), disabled.|
| consumer-stats-* | zmqPublishAddress | string | | If defined, readout statistics are also published periodically (at rate defined in monitoringUpdatePeriod) to a ZMQ server. Suggested value: tcp://127.0.0.1:6008 (for use by o2-readout-monitor). |
| consumer-tcp-* | host | string | localhost | Remote server IP name to connect to. |
| consumer-tcp-* | ncx | int | 1 | Number of parallel streams (and threads) to use. The port number specified in 'port' parameter will be increased by 1 for each extra connection. |
| consumer-tcp-* | port | int | 10001 | Remote server TCP port number to connect to. |
| consumer-zmq-* | address | string| tcp://127.0.0.1:50001 | ZMQ address where to publish (PUB) data pages, eg ipc://@readout-eventDump |
| consumer-zmq-* | maxRate | int| 0 | Maximum number of pages to publish per second. The associated memory copy has an impact on cpu load, so this should be limited when one does not use all the data (eg for eventDump). |
| consumer-zmq-* | pagesPerBurst | int | 1 | Number of consecutive pages guaranteed to be part of each publish sequence. The maxRate limit is checked at the end of each burst. |
| consumer-zmq-* | zmqOptions | string |  | Additional ZMQ options, as a comma-separated list of key=value pairs. Possible keys: ZMQ_CONFLATE, ZMQ_IO_THREADS, ZMQ_LINGER, ZMQ_SNDBUF, ZMQ_SNDHWM, ZMQ_SNDTIMEO. |
| equipment-* | blockAlign | bytes | 2M | Alignment of the beginning of the big memory block from which the pool is created. Pool will start at a multiple of this value. Each page will then begin at a multiple of memoryPoolPageSize from the beginning of big block. |
| equipment-* | consoleStatsUpdateTime | double | 0 | If set, number of seconds between printing statistics on console. |
| equipment-* | ctpMode | int | 0 | If set, the detector field (CTP run mask) is checked. Incoming data is discarded until a new bit is set, and discarded again after this bit is unset. Automatically implies rdhCheckDetectorField=1 and rdhCheckDetectorField=1. |
| equipment-* | dataPagesLogPath | string |  | Path where to save a summary of each data pages generated by equipment. |
| equipment-* | debugFirstPages | int | 0 | If set, print debug information for first (given number of) data pages readout. |
| equipment-* | disableOutput | int | 0 | If non-zero, data generated by this equipment is discarded immediately and is not pushed to output fifo of readout thread. Used for testing. |
| equipment-* | enabled | int | 1 | Enable (value=1) or disable (value=0) the equipment. |
| equipment-* | equipmentType | string |  | The type of equipment to be instanciated. One of: dummy, rorc, cruEmulator |
| equipment-* | firstPageOffset | bytes | | Offset of the first page, in bytes from the beginning of the memory pool. If not set (recommended), will start at memoryPoolPageSize (one free page is kept before the first usable page for readout internal use). |
| equipment-* | id | int| | Optional. Number used to identify equipment (used e.g. in file recording). Range 1-65535.|
| equipment-* | idleSleepTime | int | 200 | Thread idle sleep time, in microseconds. |
| equipment-* | memoryBankName | string | | Name of bank to be used. By default, it uses the first available bank declared. |
| equipment-* | memoryPoolNumberOfPages | int | | Number of pages to be created for this equipment, taken from the chosen memory bank. The bank should have enough free space to accomodate (memoryPoolNumberOfPages + 1) * memoryPoolPageSize bytes. |
| equipment-* | memoryPoolPageSize | bytes | | Size of each memory page to be created. Some space might be kept in each page for internal readout usage. |
| equipment-* | name | string| | Name used to identify this equipment (in logs). By default, it takes the name of the configuration section, equipment-xxx |
| equipment-* | numaNode | string | auto | If set, memory / thread will try to use given NUMA node. If "auto", will try to guess it for given equipment (eg ROC). |
| equipment-* | outputFifoSize | int | -1 | Size of output fifo (number of pages). If -1, set to the same value as memoryPoolNumberOfPages (this ensures that nothing can block the equipment while there are free pages). |
| equipment-* | rdhCheckDetectorField | int | 0 | If set, the detector field is checked and changes reported. |
| equipment-* | rdhCheckEnabled | int | 0 | If set, data pages are parsed and RDH headers checked. Errors are reported in logs. |
| equipment-* | rdhCheckFirstOrbit | int | 1 | If set, it is checked that the first orbit of all equipments and links is the same. If not, run is stopped. |
| equipment-* | rdhDumpEnabled | int | 0 | If set, data pages are parsed and RDH headers summary printed on console. Setting a negative number will print only the first N pages.|
| equipment-* | rdhDumpErrorEnabled | int | 1 | If set, a log message is printed for each RDH header error found.|
| equipment-* | rdhDumpFirstInPageEnabled | int | 0 | If set, the first RDH in each data page is logged. Setting a negative number will printit only for the first N pages. |
| equipment-* | rdhDumpWarningEnabled | int | 1 | If set, a log message is printed for each RDH header warning found.|
| equipment-* | rdhUseFirstInPageEnabled | int | 0 or 1 | If set, the first RDH in each data page is used to populate readout headers (e.g. linkId). Default is 1 for  equipments generating data with RDH, 0 otherwsise. |
| equipment-* | saveErrorPagesMax | int | 0 | If set, pages found with data error are saved to disk up to given maximum. |
| equipment-* | saveErrorPagesPath | string |  | Path where to save data pages with errors (when feature enabled). |
| equipment-* | stopOnError | int | 0 | If 1, readout will stop automatically on equipment error. |
| equipment-* | TFperiod | int | 128 | Duration of a timeframe, in number of LHC orbits. |
| equipment-* | verbose | int | 0 | If set, extra debug messages may be logged. |
| equipment-cruemulator-* | cruBlockSize | int | 8192 | Size of a RDH block. |
| equipment-cruemulator-* | cruId | int | 0 | CRU Id, used for CRU Id field in RDH. |
| equipment-cruemulator-* | dpwId | int | 0 | CRU end-point Id (data path wrapper id), used for DPW Id field in RDH. |
| equipment-cruemulator-* | EmptyHbRatio | double | 0 | Fraction of empty HBframes, to simulate triggered detectors. |
| equipment-cruemulator-* | feeId | int | 0 | Front-End Electronics Id, used for FEE Id field in RDH. |
| equipment-cruemulator-* | HBperiod | int | 1 | Interval between 2 HeartBeat triggers, in number of LHC orbits. |
| equipment-cruemulator-* | linkId | int | 0 | Id of first link. If numberOfLinks>1, ids will range from linkId to linkId+numberOfLinks-1. |
| equipment-cruemulator-* | maxBlocksPerPage | int | 0 | [obsolete- not used]. Maximum number of blocks per page. |
| equipment-cruemulator-* | numberOfLinks | int | 1 | Number of GBT links simulated by equipment. |
| equipment-cruemulator-* | PayloadSize | int | 64k | Maximum payload size for each trigger. Actual size is randomized, and then split in a number of (cruBlockSize) packets. |
| equipment-cruemulator-* | systemId | int | 19 | System Id, used for System Id field in RDH. By default, using the TEST code. |
| equipment-cruemulator-* | triggerRate | double | 0 | If set, the HB frame rate is limited to given value in Hz (1 HBF per data page). |
| equipment-dummy-* | eventMaxSize | bytes | 128k | Maximum size of randomly generated event. |
| equipment-dummy-* | eventMinSize | bytes | 128k | Minimum size of randomly generated event. |
| equipment-dummy-* | fillData | int | 0 | Pattern used to fill data page: (0) no pattern used, data page is left untouched, with whatever values were in memory (1) incremental byte pattern (2) incremental word pattern, with one random word out of 5. |
| equipment-player-* | autoChunk | int | 0 | When set, the file is replayed once, and cut automatically in data pages compatible with memory bank settings and RDH information. In this mode the preLoad and fillPage options have no effect. |
| equipment-player-* | autoChunkLoop | int | 0 | When set, the file is replayed in loops. If value is negative, only that number of loop is executed (-5 -> 5x replay). |
| equipment-player-* | filePath | string | | Path of file containing data to be injected in readout. |
| equipment-player-* | fillPage | int | 1 | If 1, content of data file is copied multiple time in each data page until page is full (or almost full: on the last iteration, there is no partial copy if remaining space is smaller than full file size). If 0, data file is copied exactly once in each data page. |
| equipment-player-* | preLoad | int | 1 | If 1, data pages preloaded with file content on startup. If 0, data is copied at runtime. |
| equipment-player-* | updateOrbits | int | 1 | When set, trigger orbit counters in all RDH are modified for iterations after the first one (in file loop replay mode), so that they keep increasing. |
| equipment-rorc-* | cardId | string | | ID of the board to be used. Typically, a PCI bus device id. c.f. AliceO2::roc::Parameters. |
| equipment-rorc-* | channelNumber | int | 0 | Channel number of the board to be used. Typically 0 for CRU, or 0-5 for CRORC. c.f. AliceO2::roc::Parameters. |
| equipment-rorc-* | cleanPageBeforeUse | int | 0 | If set, data pages are filled with zero before being given for writing by device. Slow, but usefull to readout incomplete pages (driver currently does not return correctly number of bytes written in page. |
| equipment-rorc-* | dataSource | string | Internal | This parameter selects the data source used by ReadoutCard, c.f. AliceO2::roc::Parameters. It can be for CRU one of Fee, Ddg, Internal and for CRORC one of Fee, SIU, DIU, Internal. |
| equipment-rorc-* | debugStatsEnabled | int | 0 | If set, enable extra statistics about internal buffers status. (printed to stdout when stopping) |
| equipment-rorc-* | firmwareCheckEnabled | int | 1 | If set, RORC driver checks compatibility with detected firmware. Use 0 to bypass this check (eg new fw version not yet recognized by ReadoutCard version). |
| equipment-zmq-* | address | string | | Address of remote server to connect, eg tcp://remoteHost:12345. |
| equipment-zmq-* | mode | string | stream | Possible values: stream (1 input ZMQ message = 1 output data page), snapshot (last ZMQ message = one output data page per TF). |
| equipment-zmq-* | timeframeClientUrl | string | | The address to be used to retrieve current timeframe. When set, data is published only once for each TF id published by remote server. |
| equipment-zmq-* | type | string | SUB | Type of ZMQ socket to use to get data (PULL, SUB). |
| readout | aggregatorSliceTimeout | double | 0 | When set, slices (groups) of pages are flushed if not updated after given timeout (otherwise closed only on beginning of next TF, or on stop). |
| readout | aggregatorStfTimeout | double | 0 | When set, subtimeframes are buffered until timeout (otherwise, sent immediately and independently for each data source). |
| readout | customCommands | string | | List of key=value pairs defining some custom shell commands to be executed at before/after state change commands. |
| readout | disableAggregatorSlicing | int | 0 | When set, the aggregator slicing is disabled, data pages are passed through without grouping/slicing. |
| readout | disableTimeframes | int | 0 | When set, all timeframe related features are disabled (this may supersede other config parameters). |
| readout | exitTimeout | double | -1 | Time in seconds after which the program exits automatically. -1 for unlimited. |
| readout | fairmqConsoleSeverity | int | -1 | Select amount of FMQ messages with fair::Logger::SetConsoleSeverity(). Value as defined in Severity enum defined from FairLogger/Logger.h. Use -1 to leave current setting. |
| readout | flushConsumerTimeout | double | 1 | Time in seconds to wait before stopping the consumers (ie wait allocated pages released). 0 means stop immediately. |
| readout | flushEquipmentTimeout | double | 1 | Time in seconds to wait for data once the equipments are stopped. 0 means stop immediately. |
| readout | logbookApiToken | string | | The token to be used for the logbook API. |
| readout | logbookEnabled | int | 0 | When set, the logbook is enabled and populated with readout stats at runtime. |
| readout | logbookUpdateInterval | int | 30 | Amount of time (in seconds) between logbook publish updates. |
| readout | logbookUrl | string | | The address to be used for the logbook API. |
| readout | maxMsgError | int | 0 | If non-zero, maximum number of error messages allowed while running. Readout stops when threshold is reached. |
| readout | maxMsgWarning | int | 0 | If non-zero, maximum number of error messages allowed while running. Readout stops when threshold is reached. |
| readout | memoryPoolStatsEnabled | int | 0 | Global debugging flag to enable statistics on memory pool usage (printed to stdout when pool released). |
| readout | rate | double | -1 | Data rate limit, per equipment, in Hertz. -1 for unlimited. |
| readout | tfRateLimit | double | 0 | When set, the output is limited to a given timeframe rate. |
| readout | timeframeServerUrl | string | | The address to be used to publish current timeframe, e.g. to be used as reference clock for other readout instances. |
| readout | timeStart | string | | In standalone mode, time at which to execute start. If not set, immediately. |
| readout | timeStop | string | | In standalone mode, time at which to execute stop. If not set, on int/term/quit signal. |
| readout-monitor | broadcastHost | string | | used by readout-status to connect to readout-monitor broadcast channel. |
| readout-monitor | broadcastPort | int | 0 | when set, the process will create a listening TCP port and broadcast statistics to connected clients. |
| readout-monitor | logFile | string | | when set, the process will log received metrics to a file. |
| readout-monitor | logFileHistory | int | 1 | defines the maximum number of previous log files to keep, when a maximum size is set. |
| readout-monitor | logFileMaxSize | int | 128 | defines the maximum size of log file (in MB). When reaching this threshold, the log file is rotated. |
| readout-monitor | monitorAddress | string | tcp://127.0.0.1:6008 | Address of the receiving ZeroMQ channel to receive readout statistics. |
| readout-monitor | outputFormat | int | 0 | 0: default, human readable. 1: raw bytes. |
| receiverFMQ | channelAddress | string | ipc:///tmp/pipe-readout | c.f. parameter with same name in consumer-FairMQchannel-* |
| receiverFMQ | channelName | string | readout | c.f. parameter with same name in consumer-FairMQchannel-* |
| receiverFMQ | channelType | string | pair | c.f. parameter with same name in consumer-FairMQchannel-* |
| receiverFMQ | decodingMode | string | none | Decoding mode of the readout FMQ output stream. Possible values: none (no decoding), stfHbf, stfSuperpage |
| receiverFMQ | dumpRDH | int | 0 | When set, the RDH of data received are printed (needs decodingMode=readout).|
| receiverFMQ | dumpSTF | int | 0 | When set, the STF header of data received are printed (needs decodingMode=stfHbf).|
| receiverFMQ | dumpTF | int | 0 | When set, a message is printed when a new timeframe is received. If the value is bigger than one, this specifies a periodic interval between TF print after the first one. (e.g. 100 would print TF 1, 100, 200, etc). |
| receiverFMQ | releaseDelay | double | 0 | When set, the messages received are not immediately released, but kept for specified time (s).|
| receiverFMQ | transportType | string | shmem | c.f. parameter with same name in consumer-FairMQchannel-* |
}



# parse config parameters descriptor
set sections {}
set ixLine 0
set isError 0
foreach line [split $configurationParametersDescriptor "\n"] {
  # split descriptor fields
  set ldef [split $line "|"]
  
  # the first lines are headers
  incr ixLine
  if {$ixLine==2} {
    # check this matches expectations
    set refDef "| Section | Parameter name  | Type | Default value | Description |"
    if {$line!=$refDef} {
      set isError 1
      break
    }
  }
  if {$ixLine<=3} {continue}
  if {[string length [string trim $line]]==0} {continue}
      
  # parse descriptor
  if {[llength $ldef]!=7} {
      set isError 1
      break
  }  
  set defSection [string trim [lindex $ldef 1]]
  set defName [string trim [lindex $ldef 2]]
  set defType [string trim [lindex $ldef 3]]
  set defDefault [string trim [lindex $ldef 4]]
  set defDescr [string trim [lindex $ldef 5]]
  
  # populate variables: sections sectionParams
  if {[lsearch $sections $defSection]<0} {
    lappend sections $defSection
  }    
  lappend sectionParams($defSection) $defName $defType $defDefault $defDescr
}
if {$isError} {
  puts "unexpected descriptor format line $ixLine: $line"
  exit 1
}



# define hierarchy of types
set types {}
foreach s $sections {
  set ss [split $s "-"]
  set k [string trim [lindex $ss 0]]
  if {$k=="receiverFMQ"} {continue}
  if {[lsearch $types $k]<0} {
    lappend types $k
    set subtype(${k}) {}
  }
  if {[llength $ss>2]} {
    set st [string trim [lindex $ss 1]]
    if {[string length $st]>1} {
      if {[lsearch $subtype(${k}) $st]<0} {
        lappend subtype(${k}) $st
      }    
    }
  }  
}
# print types hierarchy
if {0} {
  foreach t $types {
    puts "$t -> $subtype($t)"
  }
}



# read config file
foreach t $types {
  set cfg_${t} {}
  set lsections($t) {}
}
if {[file exists $configFile]} {
  package require inifile
  set iFile [::ini::open $configFile]
  foreach s [::ini::sections ${iFile}] {
    foreach t $types {
      if {[string match "${t}-*" $s]} {
	lappend cfg_${t} $s
	break
      }
    }  
  }
  if {0} {
    # print ini file content
    foreach s [::ini::sections ${iFile}] {
     puts "\[$s\]"
     foreach {k v} [::ini::get ${iFile} $s] {
       puts "\t$k = $v"
     }
    }
  }

  # populate in-memory config
  # variables:
  # for each type, lsections(type): list of sections belonging to this type
  # kvsection(section): list of list {key value} pairs belonging to this section

  foreach s [::ini::sections ${iFile}] {
    # which section does it belong to ?
    set ixt [lsearch $types [lindex [split $s "-"] 0]]
    if {$ixt<0} {
      puts "Unknown type for .ini section $s"
      continue
    }
    set t [lindex $types $ixt]
    lappend lsections($t) $s
    set kvsection($s) {}
    foreach {k v} [::ini::get ${iFile} $s] {
      # discard comments
      if {[string range $k 0 0]!="#"} {
	lappend kvsection($s) [list "$k" "$v"]
      }
    }
  }

  ::ini::close $iFile
} else {
  # create empty config with general params
  lappend lsections(readout) readout
  set kvsection(readout) {}
}


################################################################
# Create GUI
################################################################

# Create Window
wm title . "o2-readout configuration editor"
wm protocol . WM_DELETE_WINDOW cmd_Quit

set w .
set width 640
set height 280
set x [expr { ( [winfo vrootwidth  $w] - $width  ) / 2 }]
set y [expr { ( [winfo vrootheight $w] - $height ) / 2 }]
wm geometry $w ${width}x${height}+${x}+${y}


# Navigation buttons
frame .frMenu -borderwidth 2 -relief groove

# pair of name/label for menu buttons
# if label empty, same as name
set mainButtons {"General" "General parameters" "Banks" "Memory" "Equipments" ""  "Consumers" "" "Save" "" "Quit" ""}
set i 1
foreach {bt txt} $mainButtons {
  if {"$txt"==""} {
    set txt $bt
  }
#  grid [button .frMenu.bt${bt} -text "$txt"] -in .frMenu -row 1 -column $i -sticky ew
  pack [button .frMenu.bt${bt} -text "$txt"] -in .frMenu -expand 1 -side left -fill both
  set command "
  .frMenu.bt${bt} configure -command {
      display_tooltip \"\" 0 0
      destroy .frMain
      destroy .boxmsg
      foreach {bt txt} \$mainButtons {
	  .frMenu.bt\$bt configure -highlightbackground grey
      }
      .frMenu.bt${bt} configure -highlightbackground red
      frame .frMain      
      pack .frMain -fill both -padx 2 -pady 2 -expand true
      cmd_${bt}
      global current
      set current .frMenu.bt${bt}
  }
  "
  eval $command
  incr i
}

proc cmd_Quit {} {
  exit 0
}

proc cmd_General {} {
  cmd_section "readout"
}

proc cmd_Banks {} {
  cmd_instance "bank"
}

proc cmd_Equipments {} {
  cmd_instance "equipment"
}

proc cmd_Consumers {} {
  cmd_instance "consumer"
}

proc cmd_instance {type} {
  # create instance selector panel
  
  labelframe .frMain.fr_aslist -text "${type}-*" -padx 5 -pady 5
  frame .frMain.frSub_aslist
  listbox .frMain.aslist -xscrollcommand ".frMain.scrx_aslist set" -yscrollcommand ".frMain.scry_aslist set" -selectmode extended -exportselection no -height 6
  scrollbar .frMain.scrx_aslist -orient horizontal -command ".frMain.aslist xview" -width 10
  scrollbar .frMain.scry_aslist -orient vertical -command ".frMain.aslist yview" -width 10
  eval "button .frMain.add_aslist -text \"Add\" -command {add_section $type}"
  eval "button .frMain.rem_aslist -text \"Remove\" -command {rem_section $type}"
  eval "bind .frMain.aslist <<ListboxSelect>> {display_instance $type}"
     
  update_instances $type  
  
  pack .frMain.scrx_aslist -in .frMain.frSub_aslist -side bottom -fill x
  pack .frMain.scry_aslist -in .frMain.frSub_aslist -side right -fill y
  pack .frMain.aslist -in .frMain.frSub_aslist -fill both -expand 1
  pack .frMain.frSub_aslist -in .frMain.fr_aslist -fill both -expand 1
  pack .frMain.add_aslist .frMain.rem_aslist -in .frMain.fr_aslist -side left -expand 1 -fill x
  pack .frMain.fr_aslist -fill y -side left -padx 5 -pady 5
  cmd_section ""
}

proc update_instances {type} {
  # look for instances of given type and populate listbox
  global lsections
  .frMain.aslist delete 0 end
  foreach s $lsections($type) {
    .frMain.aslist insert end $s
  }
  destroy .frMain.frDetails
  destroy .frMain.scrolly
}

proc display_instance {type} {
  set ix [.frMain.aslist curselection]
  if {[llength $ix]!=1} {
    return
  }
  set name [.frMain.aslist get [lindex $ix 0]]
  cmd_section $name
}


proc rem_section {type} {
  global lsections
  set ix [.frMain.aslist curselection]
  if {[llength $ix]!=1} {
    return
  }
  set name [.frMain.aslist get [lindex $ix 0]]
  set ix [lsearch $lsections($type) $name]
  if {$ix<0} {
    tk_messageBox -message "Instance not found" -icon error -type ok
    return
  }
  set lsections($type) [lreplace $lsections($type) $ix $ix]
  update_instances $type
}

proc add_section {type} {
  # prompt for a new item of given type
  # and add it in table

  if {[winfo exists .boxmsg]} {return}
  toplevel .boxmsg
  label .boxmsg.l1 -text "Name:   ${type}-"
  entry .boxmsg.e -width 15
  
  # check if subtype needed
  global subtype
  if {[llength $subtype($type)]>0} {
    # create listbox
    label .boxmsg.l2 -text "Type:"
    listbox .boxmsg.st -height 4 -yscrollcommand " .boxmsg.scryt set"
    scrollbar .boxmsg.scryt -orient vertical -command " .boxmsg.st yview" -width 10
    foreach s $subtype($type) {
      .boxmsg.st insert end $s
    }
  }  
  scan [wm geometry .] "%dx%d%d%d" ww wh wx wy
  if {$wx>0} {
    set x [expr $wx + 100]
  } else {
    set x [expr $wx - 100]
  }
  if {$wy>0} {
    set y [expr $wy + $wh/2]
  } else {
    set y [expr $wy - $wh/2]
  }
  wm geometry .boxmsg [format "%+d%+d" $x $y]
  wm title .boxmsg "New $type"
  
  # create new item when button pressed
  eval "button .boxmsg.ok -text \"Ok\" -width 5 -command { add_section_ok \"$type\" }"   
  pack  .boxmsg.l1 .boxmsg.e -side left -padx 0 -pady 10
  if {[winfo exists .boxmsg.st]} {
    pack .boxmsg.l2 -side left -padx 10 -pady 10
    pack .boxmsg.st .boxmsg.scryt -side left -fill y -expand 1 -pady 10
  }
  pack .boxmsg.ok -padx 10 -side right -pady 10
  focus .boxmsg.e
}

proc add_section_ok {type} {
 
  # get name and add prefix
  set name "${type}-[.boxmsg.e get]"
  
  global lsections
  global kvsection
  if { [lsearch $lsections($type) $name] >=0 } {
    tk_messageBox -message "Name already used" -icon error -type ok
    return
  }
  set subtype ""
  if {[winfo exists .boxmsg.st]} {
    set subtypeix [.boxmsg.st curselection]
    if {[llength $subtypeix]==1} {
      set subtype [.boxmsg.st get [lindex $subtypeix 0]]
    }
    if {"$subtype"==""} {
      tk_messageBox -message "Please select a type" -icon error -type ok
      return
    }
  }
  
  lappend lsections($type) $name
  set kvsection($name) {} 
  if {"$subtype"!=""} {
    lappend kvsection($name) [list "${type}Type" "$subtype"]
  }
  update_instances $type
  .boxmsg.e selection range 0 end
  focus .boxmsg.e
  
  return 
}



proc handle_update {gname name op} {
    global currentSection
    global editwidgets
    set value [$editwidgets($name) get]
    global kvsection
    set ixk [lsearch -index 0 $kvsection($currentSection) $name]
    set item [list "${name}" "${value}"]
    if {$ixk<0} {
      lappend kvsection($currentSection) $item
    } else {
      set kvsection($currentSection) [lreplace $kvsection($currentSection) $ixk $ixk $item]
    }
    if {[string first "Type" $name]>=0} {
      # the subtype may have changed, update
      global currentSection
      global editwidgets
      set cmd "
      after 50 {
        display_instance $currentSection
	focus $editwidgets($name)
      }
      "
      eval $cmd
    }
    if {"$err"!=""} {
      puts "handle_update error: $err"
    }
}


proc cmd_section {section} {
  global sectionParams
  global lsections
  global kvsection
  global currentSection

  destroy .frMain.frDetails
  destroy .frMain.scrolly
  #frame .frMain.frDetails
  canvas .frMain.frDetails -yscrollcommand ".frMain.scrolly set"
  frame .frMain.frDetails.view
  pack .frMain.frDetails.view
  scrollbar .frMain.scrolly -orient vertical -command ".frMain.frDetails yview" -width 10
  pack .frMain.scrolly -fill y -side right -pady 5  
  set currentSection "$section"  
  
  if {$section==""} {return}

  set ix 1
  global editvars
  global editwidgets
  array unset editvars 
  
  foreach { name type default descr } [getMatchingSectionParams $section] {
    
    if {$name != ""} {
    # is it defined in config?
    set value ""
    set ixk [lsearch -index 0 $kvsection($section) $name]
    if {($ixk>=0)} {
      set value [lindex [lindex $kvsection($section) $ixk] 1]
    }

    label .frMain.frDetails.view.l${ix} -text "$name"
    entry .frMain.frDetails.view.e${ix} -textvariable editvars($name)
    set editwidgets($name) .frMain.frDetails.view.e${ix}
	
    if {"$value"!=""} {
      .frMain.frDetails.view.e${ix} insert 0 "$value"
    }
    grid .frMain.frDetails.view.l${ix} -column 1 -row $ix -sticky e
    grid .frMain.frDetails.view.e${ix} -column 2 -row $ix -sticky ew
    set tooltipTxt "Name:\t$name\nType:\t${type}\nDefault:\t$default\n\n$descr"
    set command "
    bind .frMain.frDetails.view.e${ix} <ButtonPress-3> {
        # absolute screen position of the click
        set x \[expr \[winfo rootx %W\] + %x\]
        set y \[expr \[winfo rooty %W\] + %y\]
        display_tooltip \"$tooltipTxt\" \$x \$y
    }
    bind .frMain.frDetails.view.e${ix} <Leave> {
        display_tooltip \"\" 0 0
    }
    bind .frMain.frDetails.view.e${ix} <FocusOut> {
        display_tooltip \"\" 0 0
    }
    "
    eval $command
    } else {
      label .frMain.frDetails.view.l${ix} -text ""
      grid .frMain.frDetails.view.l${ix} -column 1 -row $ix -sticky e
    }
    
    incr ix
  }
  trace add variable editvars write handle_update
      
  pack .frMain.frDetails -fill both -expand 1 -side right -padx 5 -pady 15
  
  update
  set width [winfo reqwidth .frMain.frDetails.view]
  set height [winfo reqheight .frMain.frDetails.view] 
  set w1 [winfo width .frMain.frDetails]
  set x 0
  if {$width<$w1} {
    set x [expr ($w1 - $width)/2]
  }
  .frMain.frDetails create window $x 0 -anchor nw -window .frMain.frDetails.view
  #.frMain.frDetails configure -scrollregion "0 0 1 $height"  
  .frMain.frDetails configure -scrollregion [.frMain.frDetails bbox all]
}



# flag to save in file default values:
# 0 -> not saved, parameter does not appear in file
# 1 -> save param with default from doc
# 2 -> same as 1), but commented out
set saveDefaults 2

proc cmd_Save {} {
  global types
  global lsections
  global kvsection
  global configFile
  global saveDefaults
  global sectionParams
  
  set iFile [open $configFile "w"]
  foreach t $types {
    foreach s $lsections($t) {
      puts $iFile "\[$s\]"
      foreach kv $kvsection($s) {
	puts $iFile "[lindex $kv 0]=[lindex $kv 1]"
      }
      
      # add default parameters
      set sp [getMatchingSectionParams $s]
      if {($saveDefaults)&&([llength $sp]>0)} {
        foreach { name type default descr } $sp {
	  if {$name==""} {continue}
          set ixk [lsearch -index 0 $kvsection($s) $name]
	  if {$ixk<0} {
	    if {$saveDefaults==1} {
	      puts $iFile "$name=$default"
	    } elseif {$saveDefaults==2} {
	      puts $iFile "\#$name=$default"
	    }
	  }
	}
      }
      
      puts $iFile ""
    }
  }  
  close $iFile
}



# return "wildcard" name of section from given type
proc getMatchingSectionParams {section} {
  global sectionParams
  global types
  global kvsection
  global lsections
  
  # look for section...
  set sp {}
  foreach t $types {
    foreach s $lsections($t) {
      if {$s==$section} {
        if {$t!="readout"} {
	  set xt "-*"
	} else {
	  set xt ""
	}
	# add all base params for this type
	catch {	
          foreach { name type default descr } $sectionParams(${t}${xt}) {
            set sp [concat $sp [list "$name" "$type" "$default" "$descr"]]
          }
	}
	
	# add separator
	set sp [concat $sp [list "" "" "" ""]]
	
	# add all params for corresponding sub-type
	# check if matching subparam type exists
	set ixk [lsearch -index 0 $kvsection($s) "${t}Type"]
	if {$ixk>=0} {
	  set vsubtype [lindex [lindex $kvsection($s) $ixk] 1]
	  # is this a valid subtype?
	  global subtype
	  if {[lsearch $subtype($t) $vsubtype]>=0} {
            foreach { name type default descr } $sectionParams(${t}-${vsubtype}${xt}) {
              set sp [concat $sp [list "$name" "$type" "$default" "$descr"]]
	    }
	  }
	}
		
	break
      }
    }
  }
  
  return $sp
}



# Display buttons
pack .frMenu -fill both -padx 2 -pady 2 -side top




##############################
# context help (right click)
##############################

toplevel .tooltip -bd 1 -background "lightyellow" -relief solid
#frame .tooltip.fr -width 400 -height 100 -padx 10 -pady 10 -background "lightyellow"
label .tooltip.txt -text "" -background "lightyellow" -wraplength 380 -justify left -border 1
pack .tooltip.txt -fill both -expand 1
#pack .tooltip.fr -expand 1
wm geometry .tooltip 600x160
wm state .tooltip withdrawn
wm transient .tooltip .
wm overrideredirect .tooltip 1

set tooltip_msg ""
proc display_tooltip {msg x y} {
  # if same tooltip already visible, close it
  global tooltip_msg
  if {$msg==$tooltip_msg} {
    set msg ""
  }
  set tooltip_msg $msg

  # close tooltip when no msg
  if {$msg==""} {
    # hide window
    wm state .tooltip withdrawn
    .tooltip.txt configure -text ""
    return
  }

  .tooltip.txt configure -text "$msg"
  
  incr x 15
  incr y -15
  update

  set w [expr [winfo reqwidth .tooltip] + 40]
  set h [expr [winfo reqheight .tooltip] + 40]
  
  wm geometry .tooltip ${w}x${h}+${x}+${y}
  wm state .tooltip normal
}

bind .tooltip <ButtonPress-3> {
  display_tooltip "" 0 0
}



# startup tab
.frMenu.btGeneral invoke
