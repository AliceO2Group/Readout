# Readout release notes

This file describes the main feature changes for each readout.exe released version.

## v0.18 - 27/02/2019
- Addition of release notes.
- New configuration parameters:
	- consumer-FairMQchannel: enableRawFormat, fmqProgOptions.
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
	- consumer-fileRecorder.dropEmptyPackets : logic updated. Packets with stopBit set following a non-empty packet are kept even if payload empty.

## v1.0.10 - 14/10/2019
- Improved collection of incomplete timeframes at end of run when disableAggregatorSlicing=0 and flushEquipmentTimeout>0.

## v1.0.10.1 - 21/10/2019
- Updated configuration parameters:
	- consumer-fileRecorder.dropEmptyPackets : replaced by consumer-fileRecorder.dropEmptyHBFrames. Logic updated, only empty HBstart/HBstop packet pairs are dropped. Also works when happening at page boundary.
- readRaw.exe utility updated: compact tabulated format when dumpRDH=1. Added option checkContinuousTriggerOrder.

## v1.0.11 - 14/10/2019
- Updated configuration parameters:
	- Following changes in the ReadoutCard library, equipment-rorc-*.generator* parameters have been removed. They have been replaced by a single equipment-rorc-*.dataSource parameter to select the data source of the readoutCard devices (both CRU and CRORC).

## v1.1.0 - 29/10/2019
- Merging v1.0.10.1 in master.
- Update for the CRU firmware v3.4.0 (non-filled superpages).
- RDH content displayed in a compact view.
- readRaw.exe utility updated. checkContinuousTriggerOrder option.
- Cosmetic fixes.

## v1.1.1 - 12/11/2019
- readoutAutoConfigure:
  - improved error reporting when no device found.
  - added support for CRORC.
- consumer-fileRecorder.dropEmptyHBFrames: new algorithm for multi-link readout.
- readRaw.exe: fix for large files reading.
- equipment-cruemulator: new algorithm for packet generator, allowing to simulate detectors in triggered mode (empty HB frames).
- Updated configuration parameters:
  - added equipment-cruemulator-*.EmptyHbRatio and equipment-cruemulator-*.PayloadSize to fake triggered mode.

## v1.2.0 - 25/11/2019
- Added readoutConfigEditor utlity to view and edit readout configuration files. Right-click on a parameter value shows corresponding documentation.

## v1.2.1 - 28/11/2019
- Updated configuration parameters:
  - added equipment-player-*.autoChunk: to replay a large data file once. It is automatically cut in data pages of appropriate size, matching memory page settings and RDH info. It complies with CRU behavior of having a new page on timeframe or link id change.
  - added equipment-player-*.TFperiod.

## v1.2.2 - 29/11/2019
- Cosmetics fix release.

## v1.2.3 - 29/11/2019
- Compatibility fix for FMQ v1.4.9

## v1.2.4 - 2/12/2019
- readoutAutoConfigure fix.

## v1.2.5 - 16/12/2019
- Updated configuration parameters:
  - default value for flushEquipmentTimeout changed to 1s.
- equipment-player-*.autoChunk: fix metadata for FMQ-STF interface.
- consumer-FairMQChannel-*: handling of RDH dynamic offset.

## v1.3.0 - 22/01/2020
- Updated the stopDma procedure for RORC device (new page reclaim mechanism).
- Readout logs directed to stdout/stderr by default, when started from a console.
- Added optional mode for interactive state machine (keyboard driven).
- Fixes in the equipments for repetitive start/stop command cycles.

## v1.3.1 - 23/01/2020
- Cosmetics fix release.

## v1.3.2 - 24/01/2020
- Cosmetics fix release.

## v1.3.3 - 30/01/2020
- Updated configuration parameters:
  - added parameters for receiverFMQ: dumpRDH, dumpTF
  - added readout.aggregatorSliceTimeout
- readRaw.exe: fix reading large files, realign was not done for last page.

## v1.3.4 - 03/02/2020
- RDH printing: added CRU id.
- Use CRU id from data as equipment id for file replay and RORC equipment.
- Fixed slicer handling of undefinedLinkId (bug in v1.3.3).
- New slicing based on equipmentId + linkId indexing, to allow proper STF formatting when replaying data from single file with multiple CRUids.

## v1.3.5 - 04/02/2020
- Cosmetics fix release.

## v1.3.6 - 10/02/2020
- Updated configuration parameters:
  - equipment-rorc-*.linkMask : default value changed to 0-11 (instead of 0-31). There was a change in ReadoutCard, which now uses numbering of links 0-11 for each CRU end-point, and creates an exception when outside of this range.
- Improved error reporting when FairMQChannel consumer memory settings cause runtime issues (eg pages too small).
- Fix issue in file replay, data size was zero (readout>=v1.2, autochunk=off).

## v1.3.7 - 28/02/2020
- readRaw.exe:
  - fixed printing offset in lz4 files.
  - added options:
    - dumpDataInline: if set, each packet raw content is printed (hex dump style).
    - fileReadVerbose: if set, more information is printed when reading/decoding file. By default, file size/chunking printouts are now off.
- Updated configuration parameters:
  - added readout.memoryPoolStatsEnabled, to print debug information on memory pages usage.
  - added equipment-*.debugFirstPages, to print debug information for first (given number of) data pages readout.

## v1.3.8 - 05/03/2020
- Updated configuration parameters:
  - equipment-rorc-*.firmwareCheckEnabled : set to zero to disable RORC firmware compatibility check (enabled by default).
  - equipment-rorc-*.debugStatsEnabled : if set, more information collected about internal buffers status (disabled by default).
  - equipment-rorc-*.linkMask : parameter removed (and ignored if present in configuration). The RORC library now automatically enables links as configured with roc-config.

## v1.3.9 - 31/03/2020
- Updated configuration parameters:
  - consumer-FairMQChannel.enableRawFormat : added new mode (use value = 2) to send 1 part per superpage instead of 1 part per HBF.
  - receiver-fmq-*.decodingMode :  readout mode replaced by stfHbf and stfSuperpage.
- Added utilities testTxPerfFMQ / testRxPerfFMQ
- Memory banks: 1 page should be kept for metadata. readoutAutoConfigure updated accordingly.

## v1.3.10 - 20/04/2020
- Added readout version in startup log
- Updated configuration parameters:
  -  readout.aggregatorStfTimeout: if set, STF buffered until timeout, so that slices of all sources sent together. This requires enough memory buffer for the corresponding amount of time.

## v1.4.0 - 22/05/2020
- Updated configuration parameters:
  - removed equipment-rorc-*.resetLevel : the reset is now handled internally by ReadoutCard driver.
- Monitoring: statistics are tagged with Readout tag.
- RDH version updated to RDHv6 (also works with RDHv5). It is NOT backward compatible: features making use of the RDH (e.g. timeframe identification) will NOT work with this version of readout.

## v1.4.1 - 18/06/2020
- FairLogger dependency update

## v1.4.2 - 19/06/2020
- FairLogger dependency update

## v1.4.3 - 22/06/2020
- Updated configuration parameters:
  - added equipment-rorc-*.rdhDumpWarningEnabled : disabled by default. For checks concerning e.g. timeframe ID continuity and link consistency in data page.
- consumer-FairMQChannel: channel bind failure is now fatal for the consumer, and associated memory bank will not be created.
- consumer-zmq: new equipment to stream data from DCS.
- fix RDH struct (PAR fields), and improved checks related to RDH size.
- increased aggregator output queue length for file replay with many sources.

## v1.4.4 - 23/06/2020
- Improvements for the DCS readout. Added example config file.

## v1.4.5 - 22/07/2020
- Added support for QC connection in consumer-FairMQChannel: see readout-qc.cfg example for alternate configuration.
- Improved consumer-fileRecorder for use with start/stop cycles. The variable ${O2_RUN} can be used to set run number in recorded file name (when readout started with AliECS).

## v1.4.6 - 27/07/2020
- Updated configuration parameters:
  - added equipment-player-*.autoChunkLoop: when set, file is replayed in loop. RDH orbit counters are updated after 1st loop to make a realistic TF sequence.
- Cleanup configuration parameters documentation and editor.

## v1.4.7 - 29/09/2020
- ReadoutCard dependency is now optional, to allow building readout on platforms where it is not available. If so, equipment-rorc and memoryMappedFile banks features are disabled.
- Updated configuration parameters:
  - added readout.timeframeServerUrl: creates a ZMQ server to publish each TF id produced, to serve as reference for other instances producing data without hardware clock.
- ProcessorLZ4Compress: data is no more compressed in-place, but in separate data pages to preserve original data (eg for shipping to data distribution).
- equipment-zmq-*.timeframeClientUrl: when set, data is published only once for each TF id published by remote server. This allows to synchronize DCS data with a remote hardware-driven readout equipment.

## v1.5.0 - 23/10/2020
- Implementation of the new STF interface (v2). See SubTimeframe.h. This version of readout will not work with older STFB executables.
- RDH is handled in the same way for all equipments (RORC, player, emulator). Same RDH configuration parameters apply to all.
- Log messages level and codes added.
- Prettified source code.
- Added utility eventDump.exe, to dump the RDH of data taken by readout.exe online (provided that a zmq consumer is defined in readout config).

## v1.5.1 - 13/11/2020
- readoutAutoConfigure: improved compatibility with roc-list-cards, now using JSON output.
- STFB interface: fixed version number.

## v1.5.2 - 16/11/2020
- receiverFMQ.exe: added parameter dumpSTF.
- STFB interface: fixed unpopulated fields in header.

## v1.5.3 - 30/11/2020
- readoutAutoConfigure: added option ("-m full") to generate a working config with STFB and QC FMQ channels.
- Updated configuration parameters:
  - added consumer-\*.filterLinksInclude and consumer-\*.filterLinkExclude to define filters based on link ids. (eg, to zmq-publish only CTP CRU / IR data link).
  - added consumer-\*.filterEquipmentIdsInclude and consumer-\*.filterEquipmentIdsExclude to define filters based on equipment ids.
- eventDump: added maxRdhPerPage parameter, to limit the number of RDH printed for each page and reduce verbosity.

## v1.5.7 - 3/12/2020
- minor release, code and build cleanup.

## v1.5.8 - 10/12/2020
- Updated configuration parameters:
  - added equipment-cruemulator-*.cruId, dpwId, systemId: to set corresponding fields in emulated RDH.
- Added example configuration file to run readout software data generator + stfBuilder.

## v1.5.9 - 11/01/2021
- readoutAutoConfigure: minor fixes (memory size, stfb fmq-name).

## v1.5.12 - 04/02/2021
- Fixed compressor libraries linking.
- Updated configuration parameters:
  - added consumer-stats.zmqPublishAddress : to publish readout statistics by ZMQ.
- Added readoutMonitor.exe, a server to collect and display statistics published by ZMQ from multiple readout processes.

##  v1.6.0 - 03/03/2021
- readoutAutoConfigure: using virtual ipc address for FMQ. Config generator versioning now indepedent from readout version (now 2.1) for better tracking of generated config files.
- optimized flush of TF buffer on stop.
- Updated configuration parameters:
  - changed equipment-player-*.autoChunkLoop: when negative value set, stop the replay after corresponding number of iterations. (e.g. -5 -> 5x replay).
  - added readout.tfRateLimit:  when set, the output (of the aggregator) is limited to a given timeframe rate.
  - added readout.timeStart and readout.timeStop: when set, in standalone mode, readout will execute START and STOP at given time.
  - added stats for data pages given to consumer-FMQchannel (number in use, release latency).

## v1.6.1 - 09/03/2021
- readoutAutoConf v2.1.1

## v2.0.0 - 06/04/2021
- Adapted to follow o2-flp conventions: renaming of executables, libraries, paths. In particular, _readout.exe_ is now _o2-readout-exe_.
- Fix file replay spurious warning on loop (was harmless and occuring only in some conditions).

## v2.0.1 - 06/04/2021
- Added run number tag to monitoring metrics.

## v2.0.2 - 14/04/2021
- Fix for automatic logging to console.

## v2.1.0 - 04/05/2021
- o2-readout-eventdump: added options dumpRdh / dumpPayload.
- o2-readout-rawreader: added print of RDH field systemId.
- Aggregator: extended cleanup (fix start/stop cycles), TF flushing (fix dicard warning on stop when multiple euipments).
- RORC equipment: empty pages flushing mechanism improved (fix pages in use stats after stop when idleSleepTime is large).
- receiver-fmq: added option releaseDelay, to add a delay before releasing data pages received.
- o2-readout-monitor: improved formatting.
- consumer-stats: publish/print statistics of memory pages locked by STFB.

## v2.2.0 - 07/05/2021
- Added rate limit for eventDump: see consumer-zmq.maxRate and consumer-zmq.pagesPerBurst.

## v2.3.0 - 11/05/2021
- consumer-stats: publish/print the current timeframe Id sent to STFB.
- consumer-stats: ZMQ stats client cleanup timeout, to avoid blocking on exit.
- auto-mute RDH warnings: verbosity reduced if many successive logs done in a short time.

## v2.4.0 - 03/06/2021
- equipment-player: fixed bug in "autochunk" replay mode. There was a "last packet invalid" error wrongly reported (and aborting replay) in the rare case when a link change would occur exactly at the beginning of a page.
- Default value of equipment-rorc.rdhUseFirstInPageEnabled is now 1 for all RDH equipments (RORC, emu, player).
- FMQ stats not printed when consoleUpdate=1 unless there is a running consumerFMQchannel with disableSending=0.
- tfRateLimit is handled in the equipment directly and avoid potential issues with timeframe slicing at very slow rates.
- equipment-cruemulator: TF id extracted from trigger counters (single timer source for improved coherency).
- Memory allocation policy updated: all readout memory is locked (RAM only, can not be swapped). A warning is reported if not.
- consumer-FMQchannel: checks are done before FMQ shared memory region is created, to avoid going in a state with over-committed memory (no checks done in FMQ library about the validity of the region created, which can cause severe crash when trying to access it). Both /proc/meminfo (MemFree) and /dev/shm (if using shmem transport type) should report enough available memory before proceeding. Memory is also immediately locked and zeroed to avoid later crashes.

## v2.4.1 - 23/06/2021
- Updated configuration parameters:
  - added readout.disableTimefarmes:  when set, all timeframe-related features are disabled (STF slicing, TF rate limits, etc). All data are tagged with TF id = 0. To be used for some calibration runs not using a central trigger clock.
  - added consumer-FMQchannel.checkResources: controls which resources are checked for fitting unmanaged region. This is a comma-separated list of items to be checked. By default, no checks are done. Recommended value: /dev/shm, MemAvailable.

## v2.4.2 - 28/06/2021
- verbose logs auto-mute (aggregator, consumer-FMQchannel).
- consumer-FMQchannel: drop TF on error (to avoid unhappy STFB when sending incomplete data, eg on "data page too small" or "no page left" conditions).
- added memory pool usage statistics (to help tuning buffer pages count and size).
- added some ZeroMQ options for consumerZMQ and equipmentZMQ.

## v2.5.0 - 27/07/2021
- Updated configuration parameters:
  - added equipment-*.saveErrorPagesMax and equipment-*.saveErrorPagesPath to save to disk data pages found with errors (up to given maximum, in given path).
  - equipment-*.rdhDumpWarningEnabled default set to 1 (now that RDH warning messages auto-muted on flood).
- o2-readout-rawreader: added print of TF id when RDH dump enabled. To enable this feature, one must specify a value for timeframePeriodOrbits (typically 128 or 256) in the command line parameters. TF ids generated are relative to the beginning of the file.

## v2.5.1 - 26/08/2021
- Updated configuration parameters:
  - added equipment-*.dataPagesLogPath to allow saving to disk summary debug info for all data pages received.
- Fixed spurious "Non-contiguous timeframe IDs" warning: the check was too strict, having some links 1 TF behind is fine.

## v2.5.2 - 13/09/2021
- Updated configuration parameters:
  - equipment.TFperiod is now set to 128 by default, instead of 256 previously. This is the duration of a timeframe, in number of LHC orbits.

## v2.6 - 06/10/2021
- Built-in memory usage reporting for o2-readout-test-fmq-memory
- Added a default configuration file (/etc/o2.d/readout-defaults.cfg) to store some global settings, loaded on process startup. This is independent from the CONFIGURE time configuration file.
- Process memory lock is now disabled by default (saves 2GB of unused FMQ virtual memory created for each channel).

## v2.6.1 - 11/10/2021
- Fix monitoring statistics: memlocked pages.

## v2.7.0 - 19/10/201
- Added linkId in RDH error reporting.
- Error reported when first orbit received by equipments mismatch.
- Updated configuration parameters:
  - readout.maxMsgError and readout.maxMsgWarning: when set, readout will stop when reaching the number of error or warning messages defined as threshold.

## v2.8.0 - 24/11/2021
- Added a [howto guide](howto.md) to document some typical use cases.
- Updated readout counters published by ZMQ and added an online status display. 

## v2.8.1 - 03/02/2022
- Updated configuration parameters:
  - equipment-cruemulator-*.triggerRate: when set, the equipment HBF rate is limited to given value (eg to emulate slow triggers).
- Fixed verbosity for non-contiguous TF warnings.

## v2.8.2 - 15/02/2022
- Updated configuration paratmeters:
  - equipment-*-rdhCheckFirstOrbit: when set to zero, readout does not check mismatch of first orbit received by equipments.
