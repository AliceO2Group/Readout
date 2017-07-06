Readout is the executable reading out the data from devices.


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


## Aggregator

This is a simple loop putting in the same vector data with matching ID.
It expected for each equipment to have a monotonic increase of ID (but not necesseraly
continuous numbering).


## Consumers

The consumers are threads making use of the data. The following have been implemented:
- ConsumerStats : keeps count of number and size of blocks produced
by readout.
- ConsumerFileRecorder : writes the readout data to a file
- ConsumerDataChecker : checks data content (header, payload). Implemented for
CRU internal data generator.
- ConsumerDataSampling : pushes data through the DataSampling interface
- ConsumerFMQ : pushes data outside readout process as a FairMQ device.

They all follow the interface defined in the base Consumer Class.


# Memory management

Depending on the readout equipment, memory is allocated in different ways.
The base class to pre-allocate and get/release at runtime memory blocks is
defined in DataFormat/MemPool.h

For the dummy software generator, it is a big block of malloc() data.
For the ROC, it is based on MemoryMappedFile shared memory.


# Data format
Readout uses the data format defined in FlpPrototype.
The base data type is a vector of header+payload pairs.
In practice, it deals with different object types:
- DataBlock : header+payload pair
- DataBlockContainer : object storing a DataBlock, specialized depending on
underlying MemPool, with ad-hoc release callback.
- DataSet : a vector of DataBlockContainer
- DataSetReference : a shared pointer to a DataSet object


# Configuration

Readout is configured with a ".ini"-formatted file. A documented example file is provided with the source code
and distribution. Each readout component is configured in a different file section.
The section name is used to get the type of the component to be instanciated.
Equipments should be prefixed as [equipment-...].
Consumers should be prefixed as [consumer-...]
Settings for data sampling are in section [sampling] (NB: to be moved to a consumer instead?).
General settings are defined in section [readout]


# Usage


At the moment, readout is a command-line utility only. It starts taking data when
launched, and stops when process exits (interactive CTRL+C, signal, or timeout
if configured).
It will be integrated with the O2 control system when available.

It takes as command-line argument the name of the configuration file to be used:

readout.exe file://path/to/my/readout.cfg



# Contact
sylvain.chapeland@cern.ch
