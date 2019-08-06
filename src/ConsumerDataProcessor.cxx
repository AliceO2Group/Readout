// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "Consumer.h"

#include <dlfcn.h>
#include <memory>
#include <thread>

#include <Common/Fifo.h>

const bool debug = false;

// declaration of function type to process data
// input: input block
// output: output block (can be the same as input block
// preliminary interface - will likely be replaced by a class
int processBlock(DataBlockContainerReference &input,
                 DataBlockContainerReference &output);
using PtrProcessFunction = decltype(&processBlock);

// A class to implement a processsing thread
class processThread {

public:
  std::unique_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>>
      inputFifo; // fifo for input data. This should be filled externally to
                 // provide data blocks.
  std::unique_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>>
      outputFifo; // fifo for output data. This should be emptied externally, to
                  // dispose of processed data blocks.

  // constructor
  // parameters:
  // - f: process function, called for each block coming in inputFifo. Result is
  // put in outputFifo.
  // - id: a number to identify this processing thread
  // - fifoSize: size of input and output FIFOs for incoming/output data blocks
  // - idleSleepTime: idle sleep time (in microseconds), when input fifo empty
  // or output fifo full, before retrying.
  //
  // The constructor initialize the member variables and create the processing
  // thread.
  processThread(PtrProcessFunction f, int id, unsigned int fifoSize = 10,
                unsigned int idleSleepTime = 100) {
    shutdown = 0;
    fProcess = f;
    cfgIdleSleepTime = idleSleepTime;
    threadId = id;
    inputFifo =
        std::make_unique<AliceO2::Common::Fifo<DataBlockContainerReference>>(
            fifoSize);
    outputFifo =
        std::make_unique<AliceO2::Common::Fifo<DataBlockContainerReference>>(
            fifoSize);
    std::function<void(void)> l = std::bind(&processThread::loop, this);
    th = std::make_unique<std::thread>(l);
  }

  // stop the thread
  void stop() {
    if (th == nullptr) {
      return;
    }
    if (debug) {
      printf("thread %d stopping\n", threadId);
    }
    shutdown = 1;
    th->join();
    if (debug) {
      printf("thread %d stopped\n", threadId);
    }
    th = nullptr;
  }

  // destructor
  ~processThread() {
    stop(); // stop thread
  }

  // the loop which runs in a separate thread and calls fProcess() for each
  // block in input fifo, until stop() is called
  void loop() {
    // printf("starting thread %d input=%p
    // output=%p\n",threadId,inputFifo.get(),outputFifo.get());
    // printf("processing thread %d starting\n",threadId);
    // printf("outputfifo=%p\n",outputFifo.get());
    // if (outputFifo==nullptr) return;
    for (; !shutdown;) {
      bool isActive = 0;
      // printf("thread %d loop\n",threadId);
      // wait there is a slot in output fifo before processing a new block, so
      // that we are sure we can push the result
      if (!outputFifo->isFull()) {
        DataBlockContainerReference bc = nullptr;
        inputFifo->pop(bc);
        if (bc != nullptr) {
          isActive = 1;
          DataBlockContainerReference result = nullptr;
          // if (debug) {printf("thread %d : got %p\n",threadId,bc.get());}
          int err = fProcess(bc, result);
          if (err) {
            printf("processBlock() failed: error %d\n", err);
          }
          if (result) {
            outputFifo->push(result);
          }
        }
      }
      if (!isActive) {
        // printf("thread %d sleeping\n",threadId);
        usleep(cfgIdleSleepTime);
      }
    }
    // printf("processing thread %d completed\n",threadId);
  }

private:
  std::atomic<int> shutdown; // flag set to 1 to request thread termination
  std::unique_ptr<std::thread> th;   // the thread
  unsigned int cfgIdleSleepTime = 0; // idle sleep time (in microseconds), when
                                     // fifos empty or full, before retrying
  PtrProcessFunction fProcess = nullptr; // the process function to be used
  int threadId = 0;                      // id of the thread
};

// A consumer class allowing to call a function from a dynamically loaded
// library for each datablock
class ConsumerDataProcessor : public Consumer {

private:
  void *libHandle = nullptr; // handle to dynamic library
  PtrProcessFunction processBlock =
      nullptr;             // pointer to processBlock() function
  int numberOfThreads = 0; // number of threads used for processing
  std::vector<std::unique_ptr<processThread>>
      threadPool;      // the pool of processing threads
  int threadIndex = 0; // a running index for the next thread in pool to use

  // various statistics
  unsigned long long dropBytes =
      0; // amount of data lost in bytes (could not keep up with incomping data)
  unsigned long long dropBlocks = 0; // amount of data lost in blocks (could not
                                     // keep up with incomping data)
  unsigned long long processedBytes = 0;  // amount of data processed in bytes
  unsigned long long processedBlocks = 0; // amount of data processed in blocks
  unsigned long long processedBytesOut = 0;  // amount of data output in bytes
  unsigned long long processedBlocksOut = 0; // amount of data output in blocks

  std::atomic<int> shutdown; // flag set to 1 to request thread termination
  std::unique_ptr<std::thread>
      outputThread; // the collector thread taking care of emptying processors
                    // output fifos
  int cfgIdleSleepTime; // sleep time (microseconds) for the processing threads
                        // (see class processThread) and the collector thread
                        // aggregating output
  int cfgFifoSize;      // fifo size for the processing threads (see class
                        // processThread)

  int cfgEnsurePageOrder = 0; // if set, will track incoming pages ID and make
                              // sure it goes out in same order
  std::unique_ptr<AliceO2::Common::Fifo<DataBlockId>>
      idFifo; // fifo to keep track of order of IDs coming in

  DataBlockId currentId =
      1000000000000ULL; // a global counter to tag pages being processed. We
                        // don't start from zero just to make this id a bit more
                        // unique.

  const bool fpPagesLog = false; // if set, this allows to save input/output ids
                                 // to file for debugging
  FILE *fpPagesIn = nullptr;
  FILE *fpPagesOut = nullptr;

public:
  // constructor
  ConsumerDataProcessor(ConfigFile &cfg, std::string cfgEntryPoint)
      : Consumer(cfg, cfgEntryPoint) {

    // configuration parameter: | consumer-processor-* | libraryPath | string |
    // | Path to the library file providing the processBlock() function to be
    // used. |
    std::string libraryPath =
        cfg.getValue<std::string>(cfgEntryPoint + ".libraryPath");
    theLog.log("Using library file = %s", libraryPath.c_str());

    // dynamically load the user-provided library
    libHandle = dlopen(libraryPath.c_str(), RTLD_LAZY);
    if (libHandle == nullptr) {
      theLog.logError("Failed to load library");
      throw __LINE__;
    }

    // lookup for the processing function
    processBlock =
        reinterpret_cast<PtrProcessFunction>(dlsym(libHandle, "processBlock"));
    if (processBlock == nullptr) {
      theLog.logError("Library - processBlock() not found");
      throw __LINE__;
    }

    // configuration parameter: | consumer-processor-* | threadInputFifoSize |
    // int | 10 | Size of input FIFO, where pending data are waiting to be
    // processed. |
    cfg.getOptionalValue<int>(cfgEntryPoint + ".threadInputFifoSize",
                              cfgFifoSize, 10);

    // configuration parameter: | consumer-processor-* | threadIdleSleepTime |
    // int | 1000 | Sleep time (microseconds) of inactive thread, before polling
    // for next data. |
    cfg.getOptionalValue<int>(cfgEntryPoint + ".threadIdleSleepTime",
                              cfgIdleSleepTime, 1000);

    // create a thread pool for the processing
    // configuration parameter: | consumer-processor-* | numberOfThreads | int |
    // 1 | Number of threads running the processBlock() function in parallel. |
    cfg.getOptionalValue<int>(cfgEntryPoint + ".numberOfThreads",
                              numberOfThreads, 1);
    theLog.log("Using %d thread(s) for processing", numberOfThreads);
    for (int i = 0; i < numberOfThreads; i++) {
      threadPool.push_back(std::make_unique<processThread>(
          processBlock, i + 1, cfgFifoSize, cfgIdleSleepTime));
    }

    // create a FIFO to keep track of incoming page IDs
    // configuration parameter: | consumer-processor-* | ensurePageOrder | int |
    // 0 | If set, ensures that data pages goes out of the processing pool in
    // same order as input (which is not guaranteed with multithreading
    // otherwise). This option adds latency. |
    cfg.getOptionalValue<int>(cfgEntryPoint + ".ensurePageOrder",
                              cfgEnsurePageOrder, 0);
    if (cfgEnsurePageOrder) {
      idFifo = std::make_unique<AliceO2::Common::Fifo<DataBlockId>>(
          (int)(numberOfThreads * cfgFifoSize * 2));
      theLog.log("Page ordering enforced for processing output");
    }
    if (fpPagesLog) {
      fpPagesIn = fopen("/tmp/pagesIn.txt", "w");
      fpPagesOut = fopen("/tmp/pagesOut.txt", "w");
    }

    // create a collector thread to collect output blocks from the processing
    // threads
    shutdown = 0;
    std::function<void(void)> l =
        std::bind(&ConsumerDataProcessor::loopOutput, this);
    outputThread = std::make_unique<std::thread>(l);
  }

  // destructor
  ~ConsumerDataProcessor() {
    // stop processing threads
    theLog.log("Flushing processing threads");
    for (auto const &th : threadPool) {
      th->stop();
    }
    // stop collector thread
    theLog.log("Flushing output thread");
    shutdown = 1;
    outputThread->join();

    // release resources
    threadPool.clear();
    theLog.log("Processing threads completed");
    if (libHandle != nullptr) {
      dlclose(libHandle);
    }
    idFifo = nullptr;
    theLog.log(
        "bytes processed: %llu bytes dropped: %llu acceptance rate: %.2lf%%",
        processedBytes, dropBytes,
        processedBlocks * 100.0 / (processedBlocks + dropBlocks));
    theLog.log("bytes accepted in: %llu bytes out: %llu compression %.4lf",
               processedBytes, processedBytesOut,
               processedBytesOut * 1.0 / processedBytes);

    if (fpPagesIn != nullptr) {
      fclose(fpPagesIn);
    }
    if (fpPagesOut != nullptr) {
      fclose(fpPagesOut);
    }
  }

  // function called when new data available from readout
  int pushData(DataBlockContainerReference &b) {

    // get input data
    void *ptr = b->getData()->data;
    if (ptr == NULL) {
      return -1;
    }
    size_t size = b->getData()->header.dataSize;

    // check we have space to keep track of this page
    if (cfgEnsurePageOrder) {
      if (idFifo->isFull()) {
        // theLog.log(InfoLogger::Severity::Warning,"Page ordering FIFO full,
        // discarding data");
        dropBlocks++;
        dropBytes += size;
        return -1;
      }
    }

    // find a free thread to process it, or drop it
    int i;
    for (i = 0; i < numberOfThreads; i++) {
      threadIndex++;
      if (threadIndex == numberOfThreads) {
        threadIndex = 0;
      }
      //      if (threadPool[threadIndex]->inputFifo->isFull()) {continue;
      //      if (debug) {printf("pushing %p to thread
      //      %d\n",b.get(),threadIndex+1);}
      if (threadPool[threadIndex]->inputFifo->push(b) == 0) {
        break;
      }
    }

    // update stats
    if (i == numberOfThreads) {
      // printf("all threads full\n");
      dropBlocks++;
      dropBytes += size;
      return -1;
    } else {
      processedBytes += size;
      processedBlocks++;
    }

    // tag data page with a unique id
    DataBlockId newId = currentId++;

    // use the general-purpose id in header to store it
    b->getData()->header.id = newId;

    if (cfgEnsurePageOrder) {
      if (idFifo->push(newId) != 0) {
        theLog.log(InfoLogger::Severity::Warning, "Page ordering FIFO full");
      }
    }

    if (fpPagesIn != nullptr) {
      fprintf(fpPagesIn, "%llu\t%llu\t%d\t%d\t%llu\n", b->getData()->header.id,
              b->getData()->header.blockId, b->getData()->header.linkId,
              b->getData()->header.equipmentId,
              b->getData()->header.timeframeId);
    }

    return 0;
  }

  // collector thread loop: handle the output of processing threads
  void loopOutput(void) {

    bool isActive = 0;

    // lambda function that pushes forward next available bage
    auto pushPage = [&](DataBlockContainerReference bc) {
      isActive = 1;
      // if (debug) {printf("output: got %p\n",bc.get());}
      // printf("output: push %lu\n",bc->getData()->header.id);

      this->processedBlocksOut++;
      this->processedBytesOut += bc->getData()->header.dataSize;

      // forward it to next consumer, if one configured
      if (this->forwardConsumer != nullptr) {
        if (this->forwardConsumer->pushData(bc) < 0) {
          // printf("forward consumer push error\n");
          this->isError++;
        }
      }
    };

    int threadIx = 0; // index of current thread being checked

    for (; !shutdown;) {
      isActive = 0;

      DataBlockId nextId = 0;
      if (cfgEnsurePageOrder) {
        // we want a specific page number
        if (idFifo->front(nextId) == 0) {
          DataBlockContainerReference bc = nullptr;
          for (int i = 0; i < numberOfThreads; i++) {
            int ix =
                (i + threadIx) % numberOfThreads; // we start from stored index
            if (threadPool[ix]->outputFifo->front(bc) == 0) {
              if (bc->getData()->header.id == nextId) {
                // we found it !
                idFifo->pop(nextId);
                threadPool[ix]->outputFifo->pop(bc);
                pushPage(bc);
                if (fpPagesOut != nullptr) {
                  fprintf(fpPagesOut, "%llu\t%llu\t%d\t%d\t%llu\n",
                          bc->getData()->header.id,
                          bc->getData()->header.blockId,
                          bc->getData()->header.linkId,
                          bc->getData()->header.equipmentId,
                          bc->getData()->header.timeframeId);
                }
                // we increment start index, as it is more likely to have the
                // next page
                threadIx++;
                break;
              }
            }
          }
        }

      } else {

        // iterate over all processing threads
        for (int i = 0; i < numberOfThreads; i++) {
          // get new output
          DataBlockContainerReference bc = nullptr;
          threadPool[i]->outputFifo->pop(bc);
          if (bc == nullptr) {
            continue;
          }
          pushPage(bc);
          if (fpPagesOut != nullptr) {
            fprintf(fpPagesOut, "%llu\t%llu\t%d\t%d\t%llu\n",
                    bc->getData()->header.id, bc->getData()->header.blockId,
                    bc->getData()->header.linkId,
                    bc->getData()->header.equipmentId,
                    bc->getData()->header.timeframeId);
          }
        }
      }

      // wait a bit if inactive
      if (!isActive) {
        usleep(cfgIdleSleepTime);
      }
    }
    // if (debug){printf("loopOutput() completed\n");}
  }
};

std::unique_ptr<Consumer>
getUniqueConsumerDataProcessor(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ConsumerDataProcessor>(cfg, cfgEntryPoint);
}
