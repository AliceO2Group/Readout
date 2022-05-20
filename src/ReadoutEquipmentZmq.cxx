// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include <atomic>
#include <functional>
#include <thread>
#include <zmq.h>
#include <inttypes.h>

#include "MemoryBankManager.h"
#include "ReadoutEquipment.h"
#include "ReadoutUtils.h"
#include "ZmqClient.hxx"
#include "readoutInfoLogger.h"

class ReadoutEquipmentZmq : public ReadoutEquipment
{

 public:
  ReadoutEquipmentZmq(ConfigFile& cfg, std::string name = "zmq");
  ~ReadoutEquipmentZmq();
  DataBlockContainerReference getNextBlock();
  void setDataOn();
  void setDataOff();

 private:
  Thread::CallbackResult populateFifoOut(); // iterative callback

  void* context = nullptr;
  void* zh = nullptr;

  int snapshotMode = 0;

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

  std::unique_ptr<ZmqClient> tfClient;
  int tfClientCallback(void* msg, int msgSize);
  std::atomic<int> maxTf = -1;
  std::atomic<int> tfUpdateTime = 0;
  std::atomic<int> tfUpdateTimeWarning = 0;
  int nBlocks = 0;
  
  uint64_t bytesRx = 0;
  uint64_t blocksRx = 0;
};

ReadoutEquipmentZmq::ReadoutEquipmentZmq(ConfigFile& cfg, std::string cfgEntryPoint) : ReadoutEquipment(cfg, cfgEntryPoint)
{
  int zmqTimeout = 0;
  int zmqMaxQueue = 0;
  int zmqRxBuffer = 16 * 1024 * 1024;
  
  std::string cfgMode = "stream";
  // configuration parameter: | equipment-zmq-* | mode | string | stream | Possible values: stream (1 input ZMQ message = 1 output data page), snapshot (last ZMQ message = one output data page per TF). |
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".mode", cfgMode);
  theLog.log(LogInfoDevel_(3002), "Using mode %s", cfgMode.c_str());
  if (cfgMode == "snapshot") {
    snapshotMode = 1;
    zmqTimeout = 5000;
  } else if (cfgMode == "stream") {
  } else {
    throw std::string("Wrong mode");
  }
  
  std::string cfgAddress = "";
  // configuration parameter: | equipment-zmq-* | address | string | | Address of remote server to connect, eg tcp://remoteHost:12345. |
  cfg.getValue<std::string>(cfgEntryPoint + ".address", cfgAddress);

  std::string cfgType = "SUB";
  // configuration parameter: | equipment-zmq-* | type | string | SUB | Type of ZMQ socket to use to get data (PULL, SUB). |
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".type", cfgType);

  theLog.log(LogInfoDevel_(3002), "Connecting to %s : %s", cfgAddress.c_str(), cfgType.c_str());

  int linerr = 0;
  int zmqerr = 0;
  for (;;) {
    context = zmq_ctx_new();
    if (context == nullptr) {
      linerr = __LINE__;
      zmqerr = zmq_errno();
      break;
    }
    if (cfgType == "PULL") {
      zh = zmq_socket(context, ZMQ_PULL);
    } else if (cfgType == "SUB") {
      zh = zmq_socket(context, ZMQ_SUB);    
    }
    if (zh == nullptr) {
      linerr = __LINE__;
      zmqerr = zmq_errno();
      break;
    }
    zmqerr = zmq_setsockopt(zh, ZMQ_RCVTIMEO, (void*)&zmqTimeout, sizeof(int));
    if (zmqerr) {
      linerr = __LINE__;
      break;
    }
    if (zmqMaxQueue >=0 ) {
      zmqerr = zmq_setsockopt(zh, ZMQ_RCVHWM, (void*)&zmqMaxQueue, sizeof(int));
      if (zmqerr) {
	linerr = __LINE__;
	break;
      }
    }
    if (zmqRxBuffer >=0 ) {
      zmqerr = zmq_setsockopt(zh, ZMQ_RCVBUF, (void*)&zmqRxBuffer, sizeof(int));
      if (zmqerr) {
	linerr = __LINE__;
	break;
      }
    }
    
    zmqerr = zmq_connect(zh, cfgAddress.c_str());
    if (zmqerr) {
      linerr = __LINE__;
      break;
    }

/*    if (cfgType == "SUB") {
      // subscribe to all published messages
      zmqerr = zmq_setsockopt(zh, ZMQ_SUBSCRIBE, "", 0);
      if (zmqerr) {
	linerr = __LINE__;
	break;
      }
    }
*/

    break;
  }

  if ((zmqerr) || (linerr)) {
    theLog.log(LogErrorSupport_(3236), "ZeroMQ error @%d : (%d) %s", linerr, zmqerr, zmq_strerror(zmqerr));
    throw __LINE__;
  }
  
  if (snapshotMode) {
    // configuration parameter: | equipment-zmq-* | timeframeClientUrl | string | | The address to be used to retrieve current timeframe. When set, data is published only once for each TF id published by remote server. |
    std::string cfgTimeframeClientUrl;
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".timeframeClientUrl", cfgTimeframeClientUrl);
    if (cfgTimeframeClientUrl.length() > 0) {
      theLog.log(LogInfoDevel_(3002), "Creating Timeframe client @ %s", cfgTimeframeClientUrl.c_str());
      tfClient = std::make_unique<ZmqClient>(cfgTimeframeClientUrl);
      if (tfClient == nullptr) {
	theLog.log(LogErrorSupport_(3236), "Failed to create TF client");
      } else {
	maxTf = 0;
	tfUpdateTime = time(NULL);
	std::function<int(void*, int)> cb = std::bind(&ReadoutEquipmentZmq::tfClientCallback, this, std::placeholders::_1, std::placeholders::_2);
	tfClient->setCallback(cb);
      }
    }

    // allocate data for snapshot
    snapshotMetadata.maxSize = memoryPoolPageSize;
    snapshotMetadata.currentSize = 0;
    snapshotMetadata.timestamp = 0;
    snapshotData = std::make_unique<unsigned char[]>(snapshotMetadata.maxSize);
    if (snapshotData == nullptr) {
      theLog.log(LogErrorSupport_(3230), "Failed to allocate memory (%d bytes)", snapshotMetadata.maxSize);
      throw __LINE__;
    }

    // starting snapshot thread
    shutdownSnapshotThread = 0;
    std::function<void(void)> l = std::bind(&ReadoutEquipmentZmq::loopSnapshot, this);
    snapshotThread = std::make_unique<std::thread>(l);

    // wait that we have at least one snapshot with success
  }
}

ReadoutEquipmentZmq::~ReadoutEquipmentZmq()
{

  // stopping snapshot thread
  if (snapshotThread != nullptr) {
    theLog.log(LogInfoDevel_(3006), "Terminating snapshot thread");
    shutdownSnapshotThread = 1;
    snapshotThread->join();
    snapshotThread = nullptr;
  }

  if (zh != nullptr) {
    zmq_close(zh);
  }

  if (context != nullptr) {
    zmq_ctx_destroy(context);
  }

  // release memory
  snapshotLock.lock();
  snapshotData = nullptr;
  snapshotLock.unlock();

  tfClient = nullptr;
  
  theLog.log(LogInfoDevel_(3003), "ZeroMQ subscribe stats: %" PRIu64 " blocks %" PRIu64 " bytes", blocksRx, bytesRx);
}

void ReadoutEquipmentZmq::loopSnapshot(void)
{

  int linerr = 0, zmqerr = 0;
  bool doLogSnapshot = 1; // flag to display message on next successful snapshot (1st on start or after error)

  for (; !shutdownSnapshotThread;) {
    for (; !shutdownSnapshotThread;) {
      zmq_msg_t msg;
      zmqerr = zmq_msg_init(&msg);
      if (zmqerr) {
        linerr = __LINE__;
        break;
      }

      int nbytes = zmq_recvmsg(zh, &msg, 0);
      if (nbytes <= 0) {
        zmqerr = zmq_errno();
        if (zmqerr) {
          linerr = __LINE__;
          break;
        }
      }

      int msgSize = zmq_msg_size(&msg);
      if (msgSize < snapshotMetadata.maxSize) {
        snapshotLock.lock();
        memcpy(snapshotData.get(), zmq_msg_data(&msg), msgSize);
        snapshotMetadata.currentSize = msgSize;
        snapshotMetadata.timestamp = time(NULL);
        snapshotLock.unlock();
        if (doLogSnapshot) {
          theLog.log(LogInfoDevel_(3003), "Received snapshot (%d bytes)", msgSize);
          doLogSnapshot = 0;
        }
      } else {
        theLog.log(LogErrorSupport_(3230), "Received message bigger than buffer: %d > %d", msgSize, snapshotMetadata.maxSize);
      }

      zmq_msg_close(&msg);
      usleep(100000);
    }

    if ((zmqerr) || (linerr)) {
      if (zmqerr == 11) {
        theLog.log(LogWarningDevel_(3236), "ZeroMQ timeout @%d : (%d) %s", linerr, zmqerr, zmq_strerror(zmqerr));
      } else {
        theLog.log(LogErrorDevel_(3236), "ZeroMQ error @%d : (%d) %s", linerr, zmqerr, zmq_strerror(zmqerr));
      }
      doLogSnapshot = 1;
    }
  }
}

DataBlockContainerReference ReadoutEquipmentZmq::getNextBlock()
{

  if (!isDataOn) {
    return nullptr;
  }

  if (snapshotMode) {
    // check TF rate... do we produce data now ?
    if (maxTf >= 0) {
      const int tfTimeout = 5;
      if ((time(NULL) > tfUpdateTime + tfTimeout) && (!tfUpdateTimeWarning)) {
	tfUpdateTimeWarning = 1;
	theLog.log(LogWarningSupport_(3236), "No TF id received from TF server for the past %d seconds", tfTimeout);
      }
      if (nBlocks >= maxTf) {
	return nullptr;
      }
    }

    // query memory pool for a free block
    DataBlockContainerReference nextBlock = nullptr;
    try {
      nextBlock = mp->getNewDataBlockContainer();
    } catch (...) {
    }

    // format data block
    if (nextBlock != nullptr) {
      DataBlock* b = nextBlock->getData();

      snapshotLock.lock();
      if ((time(NULL) - snapshotMetadata.timestamp < 5) && (snapshotMetadata.currentSize < (int)b->header.dataSize)) {
	b->header.dataSize = snapshotMetadata.currentSize;
	memcpy(b->data, snapshotData.get(), snapshotMetadata.currentSize);
      }
      snapshotLock.unlock();

      // TODO: set TF id, timestamp, etc
      nBlocks++;
      // printf("publish DCS for tf %d / maxTf %d\n", nBlocks, (int)maxTf);
    }
    return nextBlock;
  }

  // query memory pool for a free block
  DataBlockContainerReference nextBlock = nullptr;
  try {
    nextBlock = mp->getNewDataBlockContainer();
  } catch (...) {
  }

  // get data from ZMQ
  if (nextBlock != nullptr) {
    DataBlock* b = nextBlock->getData();
    int bsz = nextBlock->getDataBufferSize();
    
    int nb = 0;
    nb = zmq_recv(zh, b->data, bsz, ZMQ_DONTWAIT);
    if (nb >= bsz) {
      // buffer was too small to get full message
      theLog.log(LogWarningDevel, "ZMQ message bigger than buffer, skipping");
      nextBlock = nullptr;
    } else if (nb <= 0) {
      nextBlock = nullptr;
    } else {
      b->header.dataSize = nb;
      bytesRx += nb;
      blocksRx++;
    }
  }
  
  return nextBlock;
}

int ReadoutEquipmentZmq::tfClientCallback(void* msg, int msgSize)
{
  uint64_t tf;
  if (msgSize == sizeof(tf)) {
    int tf = (int)*((uint64_t*)msg);
    // printf("TF %d\n",(int)tf);
    tfUpdateTime = time(NULL);
    if (tfUpdateTimeWarning) {
      tfUpdateTimeWarning = 0;
      theLog.log(LogInfoSupport_(3236), "New TF id received from TF server");
    }
    if (maxTf == 0) {
      // that's the first TF
      nBlocks = maxTf;
    }
    maxTf = tf;
    return 0;
  }
  return -1;
}

void ReadoutEquipmentZmq::setDataOn() {
  ReadoutEquipment::setDataOn();
  zmq_setsockopt(zh, ZMQ_SUBSCRIBE, "", 0);
}

void ReadoutEquipmentZmq::setDataOff() {
  zmq_setsockopt(zh, ZMQ_UNSUBSCRIBE, "", 0);
  ReadoutEquipment::setDataOff();
}

std::unique_ptr<ReadoutEquipment> getReadoutEquipmentZmq(ConfigFile& cfg, std::string cfgEntryPoint) { return std::make_unique<ReadoutEquipmentZmq>(cfg, cfgEntryPoint); }

