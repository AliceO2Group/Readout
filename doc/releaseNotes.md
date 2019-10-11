# Readout release notes

This file describes the main feature changes for each readout.exe released version.

## v0.18 - 27/02/2019
- Addition of release notes.
- New configuration parameters:
	- consumer-FMQchannel: enableRawFormat, fmqProgOptions.
- Added consumer-tcp:
	- It allows to send data by tcp/ip sockets, for testing purposes.

## v0.19 - 01/03/2019
- CMake updated. Now depends on FairMQ instead of FairRoot.

## v0.19.1 - 07/03/2019
- Fix for FairMQ build.

## v0.20 - 29/03/2019
- New configuration parameters:
	- consumer-fileRecorder: added parameter pagesMax.
	- consumer-*: added parameter consumerOutput, to chain consumers (e.g. to record data output from a consumer-processor instance).
	- equipment-*: by default, outputFifoSize now set to the same value as memoryPoolNumberOfPages (this ensures that nothing can block the equipment while there are free pages).
	- equipment-dummy: added a new data page filling pattern.
- Added consumer-processor:
	- It allows data pages processing with configurable user-provided dynamic library.
	- Added libProcessorZlibCompress as example consumer-processor library.
- Added consumer-rdma:
	- It allows to send data by RDMA/ibverbs, for testing purposes.
	
## v0.21 - 11/04/2019
- Adapt to FairMQ v1.4.2+
- Added equipment-player, to inject data from files.

## v0.22 - 12/04/2019
- Added libProcessorLZ4Compress for fast compression testing.
- New configuration parameters:
	- consumer-stats: added parameter consoleUpdate.
- Added internal state machine for control

##  v0.23 - 16/04/2019
- New configuration parameters:
	- equipment-*: added parameter consoleStatsUpdateTime.
- Consumer-rdma:
	- Multiple banks supported only if contiguous.

## v0.24 - 29/04/2019
- Control: added state machine for readout.
- New configuration parameters:
	- equipment-rorc-*: added parameter rdhDumpErrorEnabled.

## v0.25 - 09/05/2019
- libProcessorLZ4Compress output formatted in standard LZ4 file format. lz4 command line utility may be used to uncompress recorded data.
- readRaw.exe utility updated. Provides means to check/display content of data files recorded with readout.
- This version requires Common > v1.4.2
- Updated configuration parameters:
	- consumer-fileRecorder.fileName : can specify per-link filename with %l. Data from different links will be recorded to different files.

## v0.26 - 14/05/2019
- New configuration parameters:
        - equipment-rorc.rdhUseFirstInPageEnabled : if set, the first RDH in each data page is used to populate readout headers (e.g. linkId). This avoids to enable a full check of all RDHs just for this purpose.

## v0.27 - 17/05/2019
- Updated configuration parameters:
	- consumer-fileRecorder.filesMax : to record data in file chunks respecting the defined per-file limits (specify max number of files, or -1 for unlimited number of files). Compatible with LZ4 compression.
- readRaw.exe utility: added option dataBlockEnabled to select file format, plus various formatting enhancements.

## v0.28 - 27/05/2019
- Updated configuration parameters:
	- consumer-processor.ensurePageOrder : to keep the same page ordering after multi-threaded processing.
- Data checks updates:
	- RDH check: disabled block length check (might not be set)
	- readRaw.exe: check order of triggers when RDH check enabled.
- Experimental features:
	- Added support for O2 logbook connectivity (not enabled in cmake).

## v1.0.1 - 13/06/2019
- Updated configuration parameters:
	- equipment-dummy.eventMinSize/eventMaxSize: now accept "bytes" prefix (k,M,...)
	- consumer-*.stopOnError: when set, readout will stop automatically on data consumer error (file recording, data processing not keeping up, etc).
	- equipment-*.stopOnError: when set, readout will stop automatically on data equipment error (CRU packet dropped, etc).
- Added minimal example configuration files for cru and dummy equipments.
- Updated RDH definition to v4 (but still compatible with v3, as v4 features not yet used).
- Experimental features:
        - Jiskefet logbook enabled in cmake.
- Enabled TimeFrame ID from RDH instead of software clock (effective when rdhUseFirstInPageEnabled=1).
- receiverFMQ.exe: moved to multipart for readout decoding mode.
- Warning on CRU packet dropped.

## v1.0.2 - 29/7/2019
- Added LZ4 file support to readRaw.exe
- Updated configuration parameters:
	- equipment-rorc.TFperiod: to configure length of timeframe, in number of orbits.
- Fixed consumer-processor ensurePageOrder behavior (using page id instead of timeframe id).

## v1.0.4 - 13/08/2019
- Added per-link RDH packetCounter contiguity check (requires rdhCheckEnabled=1).
- Code cleanup (warnings, clang-format, copyright).

## v1.0.5 - 20/08/2019
- Tagging of log messages (facility = readout).
- Collect and publish in logbook FLP runtime statistics. See readout.logbook* configuration parameters.
- FairMQChannel parameters names updated to match AliECS/OCC naming. The configured values may also be overwritten at runtime by values provided by OCC, for matching FMQ channel names.
- Added readoutAutoConfigure utlity to help generating configuration file from local system inventory (auto-detect memory and CRU settings).

## v1.0.6 - 04/09/2019
- Cosmetics release and dependencies update.

## v1.0.7 - 18/09/2019
- Cosmetics release and dependencies update.

## v1.0.8 - 04/10/2019
- Updated configuration parameters:
	- consumer-fileRecorder.dropEmptyPackets : to discard empty HB frames from recorded files (e.g. continuous detectors in triggered mode).

## v1.0.9 - 11/10/2019
- Updated configuration parameters:
	- consumer-fileRecorder.dropEmptyPackets : logic updated. Packets with stopBit set are not dropped even if payload empty.
