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
