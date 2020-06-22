// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "MemoryBankManager.h"
#include "ReadoutEquipment.h"
#include "ReadoutUtils.h"
#include <thread>
#include <functional>
#include <atomic>
#include <zmq.h>

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;

class ReadoutEquipmentZmq : public ReadoutEquipment {

public:
  ReadoutEquipmentZmq(ConfigFile &cfg, std::string name = "zmq");
  ~ReadoutEquipmentZmq();
  DataBlockContainerReference getNextBlock();

private:
  Thread::CallbackResult populateFifoOut(); // iterative callback

  void *context=nullptr;
  void *zh=nullptr;
  
  std::atomic<int> shutdownSnapshotThread = 0;
  std::unique_ptr<std::thread> snapshotThread;
  void loopSnapshot(void);
  std::unique_ptr<unsigned char[]> snapshotData = nullptr; // latestSnapshot
  
  struct SnapshotDescriptor {
    int maxSize = 0;
    int currentSize = 0;
    int timestamp = 0;
  };
  
  SnapshotDescriptor snapshotMetadata;
  std::mutex snapshotLock; // to access snapshotData/Metadata
};

ReadoutEquipmentZmq::ReadoutEquipmentZmq(ConfigFile &cfg,
                                             std::string cfgEntryPoint)
    : ReadoutEquipment(cfg, cfgEntryPoint) {
  

  std::string cfgAddress = "";
  // configuration parameter: | equipment-zmq-* | address | string | |
  // Address of remote server to connect, eg tcp://remoteHost:12345. |
  cfg.getValue<std::string>(cfgEntryPoint + ".address", cfgAddress);
  theLog.log("Connecting to %s",cfgAddress.c_str());

  int linerr=0;
  int zmqerr=0;
  for (;;) {
    context = zmq_ctx_new ();
    if (context==nullptr) { linerr=__LINE__; zmqerr=zmq_errno(); break; }
    zh = zmq_socket (context, ZMQ_PULL);
    if (zh==nullptr) { linerr=__LINE__; zmqerr=zmq_errno(); break; }
    int timeout = 5000;
    zmqerr=zmq_setsockopt(zh, ZMQ_RCVTIMEO, (void*) &timeout, sizeof(int));
    if (zmqerr) { linerr=__LINE__; break; }

    zmqerr=zmq_connect(zh,"");
    if (zmqerr) { linerr=__LINE__; break; }

    break;
  }

  if ((zmqerr)||(linerr)) {
    theLog.log(InfoLogger::Severity::Error,
               "ZeroMQ error @%d : (%d) %s", linerr, zmqerr, zmq_strerror(zmqerr));
    throw __LINE__;
  }

  // allocate data for snapshot
  snapshotMetadata.maxSize = memoryPoolPageSize;
  snapshotMetadata.currentSize = 0;
  snapshotMetadata.timestamp = 0;
  snapshotData = std::make_unique<unsigned char[]>(snapshotMetadata.maxSize);
  if (snapshotData == nullptr) {
    theLog.log(InfoLogger::Severity::Error, "Failed to allocate memory (%d bytes)", snapshotMetadata.maxSize);
    throw __LINE__;
  }
    
  // starting snapshot thread
  shutdownSnapshotThread = 0;
  std::function<void(void)> l = std::bind(&ReadoutEquipmentZmq::loopSnapshot, this);
  snapshotThread = std::make_unique<std::thread>(l);
  
  // wait that we have at least one snapshot with success
}

ReadoutEquipmentZmq::~ReadoutEquipmentZmq() {

  // stopping snapshot thread
  if (snapshotThread != nullptr) {
    theLog.log("Terminating snapshot thread");
    shutdownSnapshotThread = 1;    
    snapshotThread->join();
    snapshotThread=nullptr;
  }
  
  if (zh != nullptr) {
    zmq_close (zh);
  }
  
  if (context != nullptr) {
    zmq_ctx_destroy (context);
  }
  
  // release memory
  snapshotLock.lock();
  snapshotData = nullptr;
  snapshotLock.unlock();
}

void ReadoutEquipmentZmq::loopSnapshot(void) {
 
  int linerr=0, zmqerr=0;
  for (;!shutdownSnapshotThread;) {
    zmq_msg_t msg;
    zmqerr=zmq_msg_init(&msg);
    if (zmqerr) { linerr=__LINE__; break; }

    int nbytes=zmq_recvmsg(zh, &msg,0);
    if (nbytes<=0) {
       zmqerr=zmq_errno();
       if (zmqerr) { linerr=__LINE__; break; }
    }
    
    snapshotLock.lock();
    if (zmq_msg_size(&msg) < snapshotMetadata.maxSize) {
      memcpy(snapshotData.get(),zmq_msg_data(&msg), zmq_msg_size(&msg));
      snapshotMetadata.currentSize = zmq_msg_size(&msg);
      snapshotMetadata.timestamp = time(NULL);
    }
    snapshotLock.unlock();

    zmq_msg_close (&msg);
    usleep(100000);
  }
  
  if ((zmqerr)||(linerr)) {
    theLog.log(InfoLogger::Severity::Error,
               "ZeroMQ error @%d : (%d) %s", linerr, zmqerr, zmq_strerror(zmqerr));
    theLog.log(InfoLogger::Severity::Error,"Aborting snapshot thread");
  }
}


DataBlockContainerReference ReadoutEquipmentZmq::getNextBlock() {

  if (!isDataOn) {
    return nullptr;
  }
  
  // check TF rate... do we produce data now ?

  // query memory pool for a free block
  DataBlockContainerReference nextBlock = nullptr;
  try {
    nextBlock = mp->getNewDataBlockContainer();
  }
  catch (...) {
  }

  // format data block
  if (nextBlock != nullptr) {
    DataBlock *b = nextBlock->getData();

    snapshotLock.lock();
    if ((time(NULL) - snapshotMetadata.timestamp < 5) && (snapshotMetadata.currentSize < b->header.dataSize)) {
      b->header.dataSize = snapshotMetadata.currentSize;
      memcpy(b->data, snapshotData.get(), snapshotMetadata.currentSize);
    }
    snapshotLock.unlock();
    
    // set TF id, etc   
  }

  return nextBlock;
}

std::unique_ptr<ReadoutEquipment>
getReadoutEquipmentZmq(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ReadoutEquipmentZmq>(cfg, cfgEntryPoint);
}
