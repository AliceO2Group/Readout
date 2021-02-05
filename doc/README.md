Readout is the executable reading out the data from readout cards on the FLPs.


# Architecture

Readout is a multi-threaded process implementing a data flow connecting
together threads of different types through FIFO buffers.
Each thread runs in an independent loop. A main thread takes care to distribute data between threads and to
synchronize them on process startup and shutdown.
The actual runtime components are instanciated on startup from a configuration file.

The basic workflow consists of several steps:
1) data is produced by readout equipments
2) a data aggregator aggregates their output based on common data block IDs
3) result is distributed to consumers. Data is shared, each consumer may use (read-only)
the incoming data before releasing it. Pushing data to consumers is by default a blocking operation
(if FIFO full, readout waits until being able to push).

Backpressure is applied upstream between the readout threads
(output FIFO of step N-1 is not emptied any more when input FIFO of step N full).


## Readout equipments

Keeping the same vocabulary as in ALICE DAQ DATE software,
a readout equipment is a data source (typically, from a hardware device).
It is a thread running in a loop, populating an output FIFO with new data.

The base class ReadoutEquipment is derived in different types:
- ReadoutEquipmentDummy : a dummy software generator to push data to
memory without hardware readout card.
- ReadoutEquipmentRORC : the readout class able to readout CRORC and CRU
devices, using the ReadoutCard library DmaChannelInterface for readout.
- ReadoutEquipmentCruEmulator : a class emulating CRU data, with realistic
LHC clock rates.
- ReadoutEquipmentPlayer: a class to inject data from a file.
- ReadoutEquipmentZmq: a class to inject data from a remotely ZeroMQ publishing service (e.g. the DCS ADAPOS server).


## Aggregator

This is a simple loop putting in the same vector data with matching ID.
It expected for each equipment to have a monotonic increase of ID (but not necesseraly
continuous numbering).


## Consumers

The consumers are threads making use of the data. The following have been implemented:
- ConsumerStats : keeps count of number and size of blocks produced
by readout. Counters can be published to O2 Monitoring system.
- ConsumerFileRecorder : writes the readout data to a file
- ConsumerDataChecker : checks data content (header, payload). Implemented for
CRU internal data generator.
- ConsumerDataSampling : pushes data through the DataSampling interface
- ConsumerFMQ : pushes data outside readout process as a FairMQ device.
- ConsumerFairMQChannel : pushes data outside readout process as a FairMQ channel - with the WP5 format.
  This consumer may also create shared memory banks (see Memory management) to be used by equipments.
- ConsumerTCP: pushes the raw data payload by TCP/IP socket(s). This is meant to be used for network tests, not for production (FMQ is the supported O2 transport mechanism).
- ConsumerRDMA: pushes the raw data payload by RDMA with ibVerbs library. This is meant to be used for network tests, not for production (FMQ is the supported O2 transport mechanism).
- ConsumerDataProcessor: allows to call a user-provided function (dynamically loaded at runtime from library) on each data page produced by readout.
  See ConsumerDataProcessor.cxx for function footprint and ProcessorZlibCompress.cxx for example compression implementation.
  Note that the option 'consumerOutput' can be useful to forward the result of this processing function to another consumer (e.g. file recorder, transport, etc).
  The following processor libraries are provided with readout: ProcessorZlibCompress, ProcessorLZ4Compress.
  
They all follow the interface defined in the base Consumer Class.


# Memory management

Readout creates on startup some banks (big blocks of memory),
which are then used to create pools of data pages which are filled at runtime
by the readout equipments (and put back in the same pool after use).

The memory layout is explicitely defined in the configuration file.

In practice, you will define one or more memory blocks to be used
by the equipments. Each block is configured in a section named [bank-...]
(e.g. [bank-a1]), specifying its type (e.g. type=malloc or type=MemoryMappedFile),
size (e.g. size=256M or size=4G) and optionally NUMA node to be used
(e.g. numaNode=1).

The special consumer 'FairMQChannel' may also create a memory bank,
allocated from the FMQ "unmanaged shared memory" feature, before the other banks
are created (and hence, being the first one, being used by default by equipments).

Each equipment will then create its private data pages pool from
a given bank. This is done in the corresponding equipment configuration section
with number (memoryPoolNumberOfPages=1000) and size of each page (memoryPoolPageSize=512k),
and which bank to use (memoryBankName=bank-a1). By default of a bank name,
readout will try to create the pool from the first bank available.
Several memory pools can be created from the same memory bank, if space allows.
There should be enough space in the pool for memoryPoolNumberOfPages+1 pages, as some space is reserved for metadata.
In other words, a 1GB bank can accomodate only 1023 x 1MB pages. Page alignment settings may also reduce the usable
space further.

The number of pages allocated for an equipment should be large enough to accomodate data of at least 2 subtimeframes
Otherwise, the slicing into timeframes can not work, as it holds the data of current subtimeframe until it is complete
(i.e. when data from next timeframe starts to reach it). If necessary (mostly for test/debug purpose), the slicer can
be disabled by setting in the configuration the global parameter disableAggregatorSlicing=1.

In practice, you should allocate enough buffer for several seconds, i.e. hundred(s) of subtimeframes.
When you define the number of pages, take into account that the CRU stores data of different links in different pages.
So the number of pages needed increases with the number of links used.


# Data format
Readout uses the data format defined in FlpPrototype for internal indexing of the pages.
The content (payload) of the pages is not affected, and follows the RDH specification
if using a CRU equipment.

The (internal) base data type is a vector of header+payload pairs.
In practice, it deals with different object types:
- DataBlock : header+payload pair
- DataBlockContainer : object storing a DataBlock, specialized depending on
underlying MemPool, with ad-hoc release callback.
- DataSet : a vector of DataBlockContainer
- DataSetReference : a shared pointer to a DataSet object


# Configuration

A reference of readout configuration parameters is available in [a separate document](configurationParameters.md).

Readout is configured with a ".ini"-formatted file. A documented example file is provided with the source code
and distribution. 

Comments can be added by starting a line with the &#35; sign. Inline comments (&#35; later in the line) are not accepted.
The configuration file is separated in sections, starting with a section name within brackets [...].
Each section consists of a list of key=value pairs.

Each readout component is configured in a different file section.
The section name is used to get the type of the component to be instanciated.
Equipments should be prefixed as [equipment-...].
Consumers should be prefixed as [consumer-...].
General settings are defined in section [readout].

To setup a new readout configuration starting from the provided file, the basic steps are:
- define a memory layout suitable for your readout.
- define the equipments to be used, and their parameters (in particular, the memory bank they should use.
If not specified, readout will use the first one available).

A command line utility is provided to generate a configuration file corresponding
to current detected system configuration (memory, NUMA, CRU):

  ```
  Usage: readoutAutoConfigure
  List of options:
      -o filePath: path to a configuration file to be written.
                   If none provided, template is printed on console (after system config info).
  ```    

A graphical configuration editor is provided to view and edit readout configuration files.
Documentation of the parameters is available by right-clicking in any value field.

  ```
  Usage: readoutConfigEditor myConfigFile.cfg
  ```    

Some of the configuration parameters may be overwritten at runtime by OCC, when readout is launched from AliECS. For example, the FairMQ parameters for the StfBuilder connection are set dynamically in the configure step. This is done for the consumer with matching fmq-name=readout.


# Usage

Readout can be launched from a terminal. It takes as argument the name of the configuration file to be used:

 `readout.exe file://path/to/my/readout.cfg`

This parameter can also be a URI for the O2 Configuration backend
(with as optional extra parameter the entry point in the configuration tree, by default empty, i.e. top of the tree)

  `readout.exe ini://path/to/my/readout.ini`
  `readout.exe consul://localhost:8500 /readout`

Readout implements the [state machine](https://github.com/AliceO2Group/Control/tree/master/occ#the-occ-state-machine) defined by the O2 control system (OCC).

By default, Readout starts as a standalone process, and automatically executes the state transitions to start taking data.
It stays in the "running" state until an exit condition occurs, which can be one of:
  - external signal received (SIGTERM/SIGQUIT/SIGINT), which can e.g. be triggered by interactive CTRL+C or kill command
  - timeout, if configured (see [exitTimeout](configurationParameters.md) parameter).
It then stops data taking, releases the resources, and exits.

When the environment variable OCC_CONTROL_PORT is defined, Readout is controlled by OCC, and waits external control commands to change state.
To launch Readout in this mode, simply set the variable, e.g. 
  `export OCC_CONTROL_PORT=47100`
For testing this mode, one can use the [peanut](https://github.com/AliceO2Group/Control/tree/master/occ#the-occ-state-machine) utility to send commands.

It is also possible to run directly the readout state machine interactively (with keyboard commands to request state transitions).
To do so, set environment variable O2_READOUT_INTERACTIVE before starting the process. When defined, this supersedes the OCC mode.

Readout logs are written to the InfoLogger system. When Readout is started from a terminal, the logs are printed to the console 
(unless INFOLOGGER_MODE environment variable already specifies a log output).


# Utilities

The following utilities are also available:

- **readRaw.exe**
 Provides means to check/display content of data files recorded with readout (consumerType=fileRecorder). To be usable with readRaw.exe, these files must be created with the
 consumer option dataBlockHeaderEnabled=1, so that file content can be accessed page-by-page.
  
  ```
  Usage: readRaw.exe [rawFilePath] [options]
  List of options:
       filePath=(string) : path to file
       dataBlockEnabled=0|1: specify if file is with/without internal readout data block headers
       dumpRDH=0|1 : dump the RDH headers
       validateRDH=0|1 : check the RDH headers
       checkContinuousTriggerOrder=0|1 : check trigger order     
       dumpDataBlockHeader=0|1 : dump the data block headers (internal readout headers)
       dumpData=(int) : dump the data pages. If -1, all bytes. Otherwise, the first bytes only, as specified.
  ```
   
- **libProcessorLZ4Compress**
 To be used in a processor consumer. Allows to compress in real time the data
 with [LZ4 algorithm](https://github.com/lz4/lz4). Output can be saved to file using consumerOutput parameter.
 Such files are compliant with lz4 format and can be decoded from the command line with e.g. 
    `lz4 -d /tmp/data.raw.lz4 /tmp/data.raw`
 The configured data page size of all active equipments should not exceed 4MB if LZ4 recording is enabled
 (this is the maximum allowed LZ4 frame size after compression, so in practice make it even smaller in case
 data is not compressed effectively).
 Here is an example readout configuration snippet
 
  ```
  [consumer-lz4]
  consumerType=processor
  libraryPath=libProcessorLZ4Compress.so
  numberOfThreads=4
  consumerOutput=consumer-rec-lz4

  [consumer-rec-lz4]
  consumerType=fileRecorder
  fileName=/tmp/data.raw.lz4
  ```
  
- **eventDump.exe**
 Provide means to check/display content of online data taken with readout. It needs a consumer defined in readout ocnfig with consumerType=zmq and an address (e.g. address=ipc:///tmp/readout-out).
 The interactive client can then be started with parameter port=(same port has given in consumer address) and pageSize=(maximum super page size configured for readout equipments).
 Available keyboard commands are: (s) start page dump continuous (d) stop page dump (n) dump next page only (x) exit.
 Each page received is printed in the console (selection of RDH fields only, no payload).
 Optional parameters: (key=value format)
 - port: ZMQ address to connect.
 - pageSize: maximum page size to be received.
 - maxRdhPerPage: number of RDH to print for each page (0 = all).


# Recorded file format

There is no particular formatting for the files recorded by readout.exe/ConsumerFileRecorder,
this is a simple binary dump of the data stream received from the CRU(s) or other equipments,
the memory data pages are saved to disk one by one continuously (without separator).

So by default, for CRU equipments, the file strictly follows the format documented for the RDHv6 (RDH-FLP section)
available at https://gitlab.cern.ch/AliceO2Group/wp6-doc/-/blob/master/rdh/RDHv6.md

Pages of the different CRUs might be interleaved in an unspecified order.

The recorded stream may also be compressed with LZ4, if configured to do so
(by a ConsumerDataProcessor/libProcessorLZ4Compress consumer),
in which case there is one [LZ4 frame](https://github.com/lz4/lz4/blob/master/doc/lz4_Frame_format.md) per original data page
and the file can be decompressed with standard lz4 tool (e.g. unlz4) to get back
to the original format above.

Optionally, the data pages in recorded file might be interleaved with internal readout headers
(see readout option dataBlockHeaderEnabled and header struct defined in 
[Common/DataBlock.h](https://github.com/AliceO2Group/Common/blob/master/include/Common/DataBlock.h)),
but this is used mainly for debugging and not recommended for production, as it's an internal
format subject to change without notice.


# File replay

Readout has a special equipment (ReadoutEquipmentPlayer) to replay data from a RAW file.
There are several modes of operation:
1) continuous replay (default): the data from the file is copied to each memory data page (once per page, or multiple times to fill the page,
see 'fillPage' option). This creates an infinite stream of all-identical datapages: 1 file -> 1 data page, repeated continuously.
There is a 'preload' option to load memory with file content on startup, in order to maximize runtime throughput (no data copy).
The data page size is limited to ~2GB in readout (32-bit signed int value, minus some space reserved for metadata at top of page),
so this mode can not be used to replay files larger than ~2GB.
This mode is typically used for data stream performance tests.

2) one-time replay (use 'autoChunk' option): the data of the file is replayed once only, and fitted in data pages of given (maximum) size,
respecting the same constraints as if they would be generated from CRU (new page when changing CRU, link or timeframe ID).
The input file must have a valid RDH formatting to access these fields. The data is copied from file to memory at runtime.
This mode is typically used to test data processing downstream of readout.
An example configuration file is given in 'readout-player.cfg'.

3) loop replay (use 'autoChunk' + 'autoChunkLoop' options): same as 2), but when reaching the end of the file, replay restarts from the beginning.
After 1st iteration, the readout software updates the trigger orbit counters in the RDH to make them realistic, continuously increasing.
An offset is applied on each loop, so that readout outputs a continuous timeframe sequence.

Check the equipment-player-* configuration parameters for further details on the options.
In all replay modes, ReadoutEquipmentPlayer does not support LZ4 files or files recorded with internal headers.



# Contact
sylvain.chapeland@cern.ch
