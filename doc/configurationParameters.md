# Readout configuration parameters reference

## Sections
The readout configuration is split in different sections. Some sections have a unique instance (single name), some may have multiple instances (prefix-[instance name], shown as prefix-\* in this document).

The *readout* section defines global parameters.
The *bank-\** section defines parameters for the memory banks.
The *equipment-\** section defines parameters for the readout equipments.
The *consumer-\** section defines parameters for the readout data consumers.
The *receiverFMQ-\** section defines parameters for the test receiverFMQ.exe program, and usually kept at the same place as readout parameters for coherency.

Please see the provided example configuration files to see how they are usually arranged.

## Parameter types

The following table describes the types used in the configuration, giving:
- Name: the name by which this type is referenced in this documentation.
- Input: the type necessary to store the input value in the configuration system.
- Output: the type used in readout, to make use of the value.
- Notes: extra information on the type and conversion process, if applicable.

| Name| Input | Output | Notes |
| -- | -- | -- | -- |
| int | int | int | |
| double | double | double | |
| string | string | string | |
| bytes | string | long long | Input string is converted to a 64-bit integer value allowing usual "base units" in the suffix (k,M,G,T), as powers of 1024. Input can be decimal (1.5M is valid, will give 1.5\*1024\*1024). |

## Parameters description

Parameters without a default value are usually mandatory in the corresponding section.
Some parameters may be set for all instances of a given section [sectionName-\*], some may apply only to the instances of a given type [sectionName-type-\*].
The parameters related to 3rd-party libraries are described here for convenience, but should be further checked in the corresponding source documentation. In particular, please refer to:
- the ReadoutCard library documentation for some of the equipment-rorc devices parameters: https://github.com/AliceO2Group/ReadoutCard
- the Monitoring library documentation for some of the consumer-stats device parameters: https://github.com/AliceO2Group/Monitoring
- the FairMQ library documentation for some of the consumer-FairMQChannel device parameters: https://github.com/FairRootGroup/FairMQ

| Section | Parameter name  | Type | Default value | Description |
|--|--|--|--|--|
| bank-* | enabled | int | 1 | Enable (1) or disable (0) the memory bank. | 
| bank-* | numaNode | int | -1| Numa node where memory should be allocated. -1 means unspecified (system will choose). | 
| bank-* | size | bytes | | Size of the memory bank, in bytes. | 
| bank-* | type | string| | Support used to allocate memory. Possible values: malloc, MemoryMappedFile. | 
| consumer-* | consumerOutput | string |  | Name of the consumer where the output of this consumer (if any) should be pushed. | 
| consumer-* | consumerType | string |  | The type of consumer to be instanciated. One of:stats, FairMQDevice, DataSampling, FairMQChannel, fileRecorder, checker, processor, tcp. | 
| consumer-* | enabled | int | 1 | Enable (value=1) or disable (value=0) the consumer. | 
| consumer-* | filterLinksInclude | string |  | Defines a filter based on link ids. Only data belonging to the links in this list (coma separated values) are accepted. If empty, all link ids are fine. |
| consumer-* | filterLinksExclude | string |  | Defines a filter based on link ids. All data belonging to the links in this list (coma separated values) are rejected. |
| consumer-* | filterEquipmentIdsInclude | string |  | Defines a filter based on equipment ids. Only data belonging to the equipment ids in this list (coma separated values) are accepted. If empty, all equipment ids are fine. |
| consumer-* | filterEquipmentIdsExclude | string |  | Defines a filter based on equipment ids. All data belonging to the equipment in this list (coma separated values) are rejected. |
| consumer-* | stopOnError | int | 0 | If 1, readout will stop automatically on consumer error. | 
| consumer-data-sampling-* | address | string | ipc:///tmp/readout-pipe-1 | Address of the data sampling. | 
| consumer-FairMQChannel-* | disableSending | int | 0 | If set, no data is output to FMQ channel. Used for performance test to create FMQ shared memory segment without pushing the data. | 
| consumer-FairMQChannel-* | enableRawFormat | int | 0 | If 0, data is pushed 1 STF header + 1 part per HBF. If 1, data is pushed in raw format without STF headers, 1 FMQ message per data page. If 2, format is 1 STF header + 1 part per data page.| 
| consumer-FairMQChannel-* | fmq-address | string | ipc:///tmp/pipe-readout | Address of the FMQ channel. Depends on transportType. c.f. FairMQ::FairMQChannel.h | 
| consumer-FairMQChannel-* | fmq-name | string | readout | Name of the FMQ channel. c.f. FairMQ::FairMQChannel.h | 
| consumer-FairMQChannel-* | fmq-progOptions | string |  | Additional FMQ program options parameters, as a comma-separated list of key=value pairs. | 
| consumer-FairMQChannel-* | fmq-transport | string | shmem | Name of the FMQ transport. Typically: zeromq or shmem. c.f. FairMQ::FairMQChannel.h | 
| consumer-FairMQChannel-* | fmq-type | string | pair | Type of the FMQ channel. Typically: pair. c.f. FairMQ::FairMQChannel.h | 
| consumer-FairMQChannel-* | memoryBankName | string |  | Name of the memory bank to crete (if any) and use. This consumer has the special property of being able to provide memory banks to readout, as the ones defined in bank-*. It creates a memory region optimized for selected transport and to be used for readout device DMA. | 
| consumer-FairMQChannel-* | memoryPoolPageSize | bytes | 128k | c.f. same parameter in bank-*. | configuration 
| consumer-FairMQChannel-* | sessionName | string | default | Name of the FMQ session. c.f. FairMQ::FairMQChannel.h | 
| consumer-FairMQChannel-* | unmanagedMemorySize | bytes |  | Size of the memory region to be created. c.f. FairMQ::FairMQUnmanagedRegion.h. If not set, no special FMQ memory region is created. | 
| consumer-fileRecorder-* | bytesMax | bytes | 0 | Maximum number of bytes to write to each file. Data pages are never truncated, so if writing the full page would exceed this limit, no data from that page is written at all and file is closed. If zero (default), no maximum size set.| 
| consumer-fileRecorder-* | dataBlockHeaderEnabled | int | 0 | Enable (1) or disable (0) the writing to file of the internal readout header (Readout DataBlock.h) between the data pages, to easily navigate through the file without RDH decoding. If disabled, the raw data pages received from CRU are written without further formatting. | 
| consumer-fileRecorder-* | dropEmptyHBFrames | int | 0 | If 1, memory pages are scanned and empty HBframes are discarded, i.e. couples of packets which contain only RDH, the first one with pagesCounter=0 and the second with stop bit set. This setting does not change the content of in-memory data pages, other consumers would still get full data pages with empty packets. This setting is meant to reduce the amount of data recorded for continuous detectors in triggered mode.| 
| consumer-fileRecorder-* | fileName | string | | Path to the file where to record data. The following variables are replaced at runtime: ${XXX} -> get variable XXX from environment, %t -> unix timestamp (seconds since epoch), %T -> formatted date/time, %i -> equipment ID of each data chunk (used to write data from different equipments to different output files), %l -> link ID (used to write data from different links to different output files). | 
| consumer-fileRecorder-* | filesMax | int | 1 | If 1 (default), file splitting is disabled: file is closed whenever a limit is reached on a given recording stream. Otherwise, file splitting is enabled: whenever the current file reaches a limit, it is closed an new one is created (with an incremental name). If <=0, an unlimited number of incremental chunks can be created. If non-zero, it defines the maximum number of chunks. The file name is suffixed with chunk number (by default, ".001, .002, ..." at the end of the file name. One may use "%c" in the file name to define where this incremental file counter is printed. | 
| consumer-fileRecorder-* | pagesMax | int | 0 | Maximum number of data pages accepted by recorder. If zero (default), no maximum set.| 
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
| consumer-tcp-* | host | string | localhost | Remote server IP name to connect to. | 
| consumer-tcp-* | ncx | int | 1 | Number of parallel streams (and threads) to use. The port number specified in 'port' parameter will be increased by 1 for each extra connection. | 
| consumer-tcp-* | port | int | 10001 | Remote server TCP port number to connect to. | 
| equipment-* | blockAlign | bytes | 2M | Alignment of the beginning of the big memory block from which the pool is created. Pool will start at a multiple of this value. Each page will then begin at a multiple of memoryPoolPageSize from the beginning of big block. | 
| equipment-* | consoleStatsUpdateTime | double | 0 | If set, number of seconds between printing statistics on console. | 
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
| equipment-* | outputFifoSize | int | -1 | Size of output fifo (number of pages). If -1, set to the same value as memoryPoolNumberOfPages (this ensures that nothing can block the equipment while there are free pages). | 
| equipment-* | rdhCheckEnabled | int | 0 | If set, data pages are parsed and RDH headers checked. Errors are reported in logs. | 
| equipment-* | rdhDumpEnabled | int | 0 | If set, data pages are parsed and RDH headers summary printed. Setting a negative number will print only the first N RDH.| 
| equipment-* | rdhDumpErrorEnabled | int | 1 | If set, a log message is printed for each RDH header error found.| 
| equipment-* | rdhDumpWarningEnabled | int | 0 | If set, a log message is printed for each RDH header warning found.| 
| equipment-* | rdhUseFirstInPageEnabled | int | 0 | If set, the first RDH in each data page is used to populate readout headers (e.g. linkId).| 
| equipment-* | stopOnError | int | 0 | If 1, readout will stop automatically on equipment error. | 
| equipment-* | TFperiod | int | 256 | Duration of a timeframe, in number of LHC orbits. | 
| equipment-cruemulator-* | cruBlockSize | int | 8192 | Size of a RDH block. | 
| equipment-cruemulator-* | EmptyHbRatio | double | 0 | Fraction of empty HBframes, to simulate triggered detectors. | 
| equipment-cruemulator-* | feeId | int | 0 | Front-End Electronics Id, used for FEE Id field in RDH. | 
| equipment-cruemulator-* | HBperiod | int | 1 | Interval between 2 HeartBeat triggers, in number of LHC orbits. | 
| equipment-cruemulator-* | linkId | int | 0 | Id of first link. If numberOfLinks>1, ids will range from linkId to linkId+numberOfLinks-1. | 
| equipment-cruemulator-* | maxBlocksPerPage | int | 0 | [obsolete- not used]. Maximum number of blocks per page. | 
| equipment-cruemulator-* | numberOfLinks | int | 1 | Number of GBT links simulated by equipment. | 
| equipment-cruemulator-* | PayloadSize | int | 64k | Maximum payload size for each trigger. Actual size is randomized, and then split in a number of (cruBlockSize) packets. | 
| equipment-dummy-* | eventMaxSize | bytes | 128k | Maximum size of randomly generated event. | 
| equipment-dummy-* | eventMinSize | bytes | 128k | Minimum size of randomly generated event. | 
| equipment-dummy-* | fillData | int | 0 | Pattern used to fill data page: (0) no pattern used, data page is left untouched, with whatever values were in memory (1) incremental byte pattern (2) incremental word pattern, with one random word out of 5. | 
| equipment-player-* | autoChunk | int | 0 | When set, the file is replayed once, and cut automatically in data pages compatible with memory bank settings and RDH information. In this mode the preLoad and fillPage options have no effect. | 
| equipment-player-* | autoChunkLoop | int | 0 | When set, the file is replayed in loops. Trigger orbit counter in RDH are modified for iterations after the first one, so that they keep increasing. | 
| equipment-player-* | filePath | string | | Path of file containing data to be injected in readout. | 
| equipment-player-* | fillPage | int | 1 | If 1, content of data file is copied multiple time in each data page until page is full (or almost full: on the last iteration, there is no partial copy if remaining space is smaller than full file size). If 0, data file is copied exactly once in each data page. | 
| equipment-player-* | preLoad | int | 1 | If 1, data pages preloaded with file content on startup. If 0, data is copied at runtime. | 
| equipment-rorc-* | cardId | string | | ID of the board to be used. Typically, a PCI bus device id. c.f. AliceO2::roc::Parameters. | 
| equipment-rorc-* | channelNumber | int | 0 | Channel number of the board to be used. Typically 0 for CRU, or 1-6 for CRORC. c.f. AliceO2::roc::Parameters. | 
| equipment-rorc-* | cleanPageBeforeUse | int | 0 | If set, data pages are filled with zero before being given for writing by device. Slow, but usefull to readout incomplete pages (driver currently does not return correctly number of bytes written in page. | 
| equipment-rorc-* | dataSource | string | Internal | This parameter selects the data source used by ReadoutCard, c.f. AliceO2::roc::Parameters. It can be for CRU one of Fee, Ddg, Internal and for CRORC one of Fee, SIU, DIU, Internal. | 
| equipment-rorc-* | debugStatsEnabled | int | 0 | If set, enable extra statistics about internal buffers status. (printed to stdout when stopping) | 
| equipment-rorc-* | firmwareCheckEnabled | int | 1 | If set, RORC driver checks compatibility with detected firmware. Use 0 to bypass this check (eg new fw version not yet recognized by ReadoutCard version). | 
| equipment-zmq-* | address | string | | Address of remote server to connect, eg tcp://remoteHost:12345. | 
| equipment-zmq-* | timeframeClientUrl | string | | The address to be used to retrieve current timeframe. When set, data is published only once for each TF id published by remote server. | 
| readout | aggregatorSliceTimeout | double | 0 | When set, slices (groups) of pages are flushed if not updated after given timeout (otherwise closed only on beginning of next TF, or on stop). | 
| readout | aggregatorStfTimeout | double | 0 | When set, subtimeframes are buffered until timeout (otherwise, sent immediately and independently for each data source). | 
| readout | disableAggregatorSlicing | int | 0 | When set, the aggregator slicing is disabled, data pages are passed through without grouping/slicing. | 
| readout | exitTimeout | double | -1 | Time in seconds after which the program exits automatically. -1 for unlimited. | 
| readout | flushEquipmentTimeout | double | 1 | Time in seconds to wait for data once the equipments are stopped. 0 means stop immediately. | 
| readout | logbookApiToken | string | | The token to be used for the logbook API. | 
| readout | logbookEnabled | int | 0 | When set, the logbook is enabled and populated with readout stats at runtime. | 
| readout | logbookUpdateInterval | int | 30 | Amount of time (in seconds) between logbook publish updates. | 
| readout | logbookUrl | string | | The address to be used for the logbook API. | 
| readout | memoryPoolStatsEnabled | int | 0 | Global debugging flag to enable statistics on memory pool usage (printed to stdout when pool released). | 
| readout | rate | double | -1 | Data rate limit, per equipment, in Hertz. -1 for unlimited. | 
| readout | timeframeServerUrl | string | | The address to be used to publish current timeframe, e.g. to be used as reference clock for other readout instances. | 
| receiverFMQ | channelAddress | string | ipc:///tmp/pipe-readout | c.f. parameter with same name in consumer-FairMQchannel-* | 
| receiverFMQ | channelName | string | readout | c.f. parameter with same name in consumer-FairMQchannel-* | 
| receiverFMQ | channelType | string | pair | c.f. parameter with same name in consumer-FairMQchannel-* | 
| receiverFMQ | decodingMode | string | none | Decoding mode of the readout FMQ output stream. Possible values: none (no decoding), stfHbf, stfSuperpage | 
| receiverFMQ | dumpRDH | int | 0 | When set, the RDH of data received are printed (needs decodingMode=readout).| 
| receiverFMQ | dumpTF | int | 0 | When set, a message is printed when a new timeframe is received. If the value is bigger than one, this specifies a periodic interval between TF print after the first one. (e.g. 100 would print TF 1, 100, 200, etc). | 
| receiverFMQ | dumpSTF | int | 0 | When set, the STF header of data received are printed (needs decodingMode=stfHbf).|
| receiverFMQ | transportType | string | shmem | c.f. parameter with same name in consumer-FairMQchannel-* | 
