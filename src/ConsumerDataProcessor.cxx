#include "Consumer.h"

#include <dlfcn.h>
#include <memory>
#include <thread>

#include <Common/Fifo.h>

const bool debug=false;


// declaration of function type to process data
// input: input block
// output: output block (can be the same as input block
// preliminary interface - will likely be replaced by a class
int processBlock(DataBlockContainerReference &input, DataBlockContainerReference &output);
using PtrProcessFunction = decltype(&processBlock);



// A class to implement a processsing thread
class processThread {

public:
  
  std::unique_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>> inputFifo;  // fifo for input data. This should be filled externally to provide data blocks.
  std::unique_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>> outputFifo; // fifo for output data. This should be emptied externally, to dispose of processed data blocks.

  
  // constructor
  // parameters:
  // - f: process function, called for each block coming in inputFifo. Result is put in outputFifo.
  // - id: a number to identify this processing thread
  // - fifoSize: size of input and output FIFOs for incoming/output data blocks
  // - idleSleepTime: idle sleep time (in microseconds), when input fifo empty or output fifo full, before retrying.
  //
  // The constructor initialize the member variables and create the processing thread.
  processThread(PtrProcessFunction f, int id, unsigned int fifoSize=10, unsigned int idleSleepTime=100){
    shutdown=0;
    fProcess=f;
    cfgIdleSleepTime=idleSleepTime;
    threadId=id;
    inputFifo=std::make_unique<AliceO2::Common::Fifo<DataBlockContainerReference>>(fifoSize);
    outputFifo=std::make_unique<AliceO2::Common::Fifo<DataBlockContainerReference>>(fifoSize);
    std::function<void(void)> l = std::bind(&processThread::loop, this);
    th=std::make_unique<std::thread>(l);
  }
  
  // stop the thread
  void stop() {
    if (th==nullptr) {return;}
    if (debug) {printf("thread %d stopping\n",threadId);}
    shutdown=1;
    th->join();
    if (debug) {printf("thread %d stopped\n",threadId);}
    th=nullptr;
  }
  
  // destructor
  ~processThread(){
    stop(); // stop thread
  }
  
  // the loop which runs in a separate thread and calls fProcess() for each block in input fifo, until stop() is called
  void loop() {
    //printf("starting thread %d input=%p output=%p\n",threadId,inputFifo.get(),outputFifo.get());
    //printf("processing thread %d starting\n",threadId);
    //printf("outputfifo=%p\n",outputFifo.get());
    //if (outputFifo==nullptr) return;
    for(;!shutdown;) {
      bool isActive=0;
      //printf("thread %d loop\n",threadId);
      // wait there is a slot in output fifo before processing a new block, so that we are sure we can push the result
      if (!outputFifo->isFull()) {
        DataBlockContainerReference bc=nullptr;
        inputFifo->pop(bc);
        if (bc!=nullptr) {
          isActive=1;
          DataBlockContainerReference result=nullptr;
          //if (debug) {printf("thread %d : got %p\n",threadId,bc.get());}
          fProcess(bc,result);          
          if (result) {
            outputFifo->push(result);
          }
        }
      }
      if (!isActive) {
        //printf("thread %d sleeping\n",threadId);
        usleep(cfgIdleSleepTime);
      }
    }
    //printf("processing thread %d completed\n",threadId);
  }
  
  
private:
  std::atomic<int> shutdown; // flag set to 1 to request thread termination
  std::unique_ptr<std::thread> th; // the thread
  unsigned int cfgIdleSleepTime=0; // idle sleep time (in microseconds), when fifos empty or full, before retrying
  PtrProcessFunction fProcess=nullptr; // the process function to be used
  int threadId=0; // id of the thread
};



// A consumer class allowing to call a function from a dynamically loaded library for each datablock
class ConsumerDataProcessor: public Consumer {
  
  private:
  
  void *libHandle=nullptr; // handle to dynamic library
  PtrProcessFunction processBlock=nullptr; // pointer to processBlock() function
  int numberOfThreads=0; // number of threads used for processing
  std::vector<std::unique_ptr<processThread>> threadPool; // the pool of processing threads
  int threadIndex=0; // a running index for the next thread in pool to use
  
  // various statistics
  unsigned long long dropBytes=0; // amount of data lost in bytes (could not keep up with incomping data)
  unsigned long long dropBlocks=0; // amount of data lost in blocks (could not keep up with incomping data)  
  unsigned long long processedBytes=0; // amount of data processed in bytes
  unsigned long long processedBlocks=0; // amount of data processed in blocks
  unsigned long long processedBytesOut=0; // amount of data output in bytes
  unsigned long long processedBlocksOut=0; // amount of data output in blocks  

  std::atomic<int> shutdown; // flag set to 1 to request thread termination    
  std::unique_ptr<std::thread> outputThread; // the collector thread taking care of emptying processors output fifos
  int cfgIdleSleepTime=100; // sleep time (microseconds) for the processing threads (see class processThread) and the collector thread aggregating output
  int cfgFifoSize=10; // fifo size for the processing threads (see class processThread)
  
  
  public:
  
  // constructor
  ConsumerDataProcessor(ConfigFile &cfg, std::string cfgEntryPoint):Consumer(cfg,cfgEntryPoint) {  
  
    // configuration parameter: | consumer-processor-* | libraryPath | string |  | Path to the library file providing the processBlock() function to be used. |
    std::string libraryPath=cfg.getValue<std::string>(cfgEntryPoint + ".libraryPath");
    theLog.log("Using library file = %s",libraryPath.c_str());
    
    // dynamically load the user-provided library
    libHandle = dlopen(libraryPath.c_str(), RTLD_LAZY);
    if (libHandle==nullptr) {
      theLog.logError("Failed to load library");
      throw __LINE__;
    }
    
    // lookup for the processing function
    processBlock = reinterpret_cast<PtrProcessFunction>(dlsym(libHandle, "processBlock"));
    if (processBlock==nullptr) {
      theLog.logError("Library - processBlock() not found");
      throw __LINE__;
    }
    
    // create a thread pool for the processing
    // configuration parameter: | consumer-processor-* | numberOfThreads | int | 1 | Number of threads running the processBlock() function in parallel. |
    cfg.getOptionalValue<int>(cfgEntryPoint + ".numberOfThreads",numberOfThreads,1);
    theLog.log("Using %d thread(s) for processing",numberOfThreads);    
    for (int i=0;i<numberOfThreads;i++) {
      threadPool.push_back(std::make_unique<processThread>(processBlock,i+1,cfgFifoSize,cfgIdleSleepTime));
    }
    
    // create a collector thread to collect output blocks from the processing threads
    shutdown=0;
    std::function<void(void)> l = std::bind(&ConsumerDataProcessor::loopOutput, this);
    outputThread=std::make_unique<std::thread>(l);
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
    shutdown=1;
    outputThread->join();
    
    // release resources    
    threadPool.clear();
    theLog.log("Processing threads completed");    
    if (libHandle!=nullptr) {
      dlclose(libHandle);
    }
    theLog.log("bytes processed: %lu bytes dropped: %lu acceptance rate: %.2lf%%",processedBytes,dropBytes,processedBlocks*100.0/(processedBlocks+dropBlocks));
    theLog.log("bytes accepted in: %lu bytes out: %lu compression %.4lf",processedBytes,processedBytesOut, processedBytesOut*1.0/processedBytes);
  }
  
  // function called when new data available from readout
  int pushData(DataBlockContainerReference &b) {  
    
    // get input data
    void *ptr=b->getData()->data;
    if (ptr==NULL) {return -1;}    
    size_t size=b->getData()->header.dataSize;

    // find a free thread to process it, or drop it
    int i;
    for (i=0;i<numberOfThreads;i++) {
      int ix=(i+threadIndex)%numberOfThreads;
      if (threadPool[ix]->inputFifo->isFull()) {continue;}
      //if (debug) {printf("pushing %p to thread %d\n",b.get(),ix+1);}      
      threadPool[ix]->inputFifo->push(b);
      threadIndex=ix+1;
      break;
    }

    // update stats
    if (i==numberOfThreads) {
      dropBlocks++;
      dropBytes+=size;
    } else {
      processedBytes+=size;
      processedBlocks++;
    }
    
    return 0;
  }
  
  // collector thread loop: handle the output of processing threads
  void loopOutput(void) {
    for(;!shutdown;) {
       bool isActive=0;

       // iterate over processing threads       
       for (int i=0;i<numberOfThreads;i++) {
         // get new output
         DataBlockContainerReference bc=nullptr;
         threadPool[i]->outputFifo->pop(bc);
         if (bc==nullptr) {continue;}
         isActive=1;
         //if (debug) {printf("output: got %p\n",bc.get());}

         processedBlocksOut++;
         processedBytesOut+=bc->getData()->header.dataSize;

         // forward it to next consumer, if one configured         
         if (forwardConsumer!=nullptr) {
           forwardConsumer->pushData(bc);
         }
       }
        
       // wait a bit if inactive        
       if (!isActive) {
         usleep(cfgIdleSleepTime);
       }
     }
     //if (debug){printf("loopOutput() completed\n");}
   }
};


std::unique_ptr<Consumer> getUniqueConsumerDataProcessor(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ConsumerDataProcessor>(cfg, cfgEntryPoint);
}
