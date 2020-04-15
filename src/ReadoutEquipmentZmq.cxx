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
#include <zmq.h>

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;

class ReadoutEquipmentZmq : public ReadoutEquipment {

public:
  ReadoutEquipmentZmq(ConfigFile &cfg, std::string name = "dummyReadout");
  ~ReadoutEquipmentZmq();
  DataBlockContainerReference getNextBlock();

private:
  Thread::CallbackResult populateFifoOut(); // iterative callback

  void *context=nullptr;
  void *zh=nullptr;
};

ReadoutEquipmentZmq::ReadoutEquipmentZmq(ConfigFile &cfg,
                                             std::string cfgEntryPoint)
    : ReadoutEquipment(cfg, cfgEntryPoint) {

 printf ("Connecting to DCS ADAPOS server\n");
int err=0;

context = zmq_ctx_new ();
if (context!=nullptr) {
 zh = zmq_socket (context, ZMQ_PULL);
 if (zh!=nullptr) {
   
   err=zmq_connect (zh,"tcp://localhost:5555");
   if (err) { throw __LINE__; } // zmq_errno()

     int timeout = 10000;
     err=zmq_setsockopt(zh, ZMQ_RCVTIMEO, (void*) &timeout, sizeof(int));
     printf("err=%d, zmq_errno returned %d.\n", err, zmq_errno());
     timeout=0;
     size_t argsz=sizeof(timeout);
     err=zmq_getsockopt(zh, ZMQ_RCVTIMEO, (void*) &timeout, &argsz);
     printf("err=%d, timeout=%d argsz=%d\n", err, timeout,(int)argsz);
     
     zmq_msg_t msg;
     err=zmq_msg_init(&msg);
     if (err) { throw __LINE__;}
   
     int nbytes=zmq_recvmsg(zh, &msg,0);
     if (nbytes) {
       err=zmq_errno();
       printf("zmq_errno returned %d = %s\n", err,zmq_strerror(err));
       printf("received %d\n",nbytes);
       zmq_msg_close (&msg);
     }
   }
 }
/*
int i;
for (i=0;i<50;i++) {
  zmq_msg_t msg;
  err=zmq_msg_init(&msg);
  if (err) {
    return __LINE__;
  }

  int nbytes=zmq_recvmsg(zh, &msg,0);
  if (nbytes) {
    err=zmq_errno();
    printf("zmq_errno returned %d = %s\n", err,zmq_strerror(err));
    printf("received %d\n",nbytes);
    zmq_msg_close (&msg);
  } else {
    fflush(stdout);  
    usleep(100000);
  }
}
*/

 }

ReadoutEquipmentZmq::~ReadoutEquipmentZmq() {

  if (zh!=nullptr) {
    zmq_close (zh);
  }
  if (context!=nullptr) {
    zmq_ctx_destroy (context);
  }

}

DataBlockContainerReference ReadoutEquipmentZmq::getNextBlock() {

  if (!isDataOn) {
    return nullptr;
  }

  // query memory pool for a free block
  DataBlockContainerReference nextBlock = nullptr;
  try {
    nextBlock = mp->getNewDataBlockContainer();
  } catch (...) {
  }

  // format data block
  if (nextBlock != nullptr) {
    DataBlock *b = nextBlock->getData();

    // set size
    int dSize = 0;
    
    // no need to fill header defaults, this is done by getNewDataBlockContainer()
    // only adjust payload size
    b->header.dataSize = dSize;
    //b->data[k] = (char)k;
  }

  return nextBlock;
}

std::unique_ptr<ReadoutEquipment>
getReadoutEquipmentZmq(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ReadoutEquipmentZmq>(cfg, cfgEntryPoint);
}
