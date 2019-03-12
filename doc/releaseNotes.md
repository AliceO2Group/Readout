# Readout release notes

This file describes the main feature changes for each readout.exe released version.

## v0.18 - 27/02/2019
- Addition of release notes.
- Added configuration paramters for consumer-FMQchannel: enableRawFormat, fmqProgOptions
- Added consumer-tcp to send data by tcp/ip sockets, for testing purposes.

## v0.19 - 01/03/2019
- CMake updated. Now depends on FairMQ instead of FairRoot.

## v0.19.1 - 07/03/2019
- fix for FairMQ build.

## next version
- consumer-fileRecorder: added parameter pagesMax.
- equipment-dummy: added a new data page filling pattern.
- Added consumer-processor to allow data pages processing with configurable user-provided dynamic library.
- consumer-*: added parameter consumerOutput, to chain consumers (e.g. to record data output from a consumer-processor instance).
- Added libProcessorZlibCompress as example consumer-processor library.
