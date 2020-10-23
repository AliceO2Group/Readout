// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include <zmq.h>

#include "Consumer.h"
#include "ReadoutUtils.h"
#include "ZmqClient.hxx"

class ConsumerZMQ : public Consumer
{
 public:
  uint64_t block_ix = 0;
  int txIx = 0;
  uint64_t nBlocksDropped = 0;
  uint64_t nBytesDropped = 0;
  uint64_t nBytesSent = 0;
  uint64_t nBlocksSent = 0;
  std::string cfgAddress = "tcp://127.0.0.1:50001";

  ConsumerZMQ(ConfigFile& cfg, std::string cfgEntryPoint) : Consumer(cfg, cfgEntryPoint)
  {

    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".address", cfgAddress);
    theLog.log(LogInfoDevel_(3002), "ZeroMQ server @ %s", cfgAddress.c_str());

    int linerr = 0;
    int zmqerr = 0;
    for (;;) {
      context = zmq_ctx_new();
      if (context == nullptr) {
        linerr = __LINE__;
        zmqerr = zmq_errno();
        break;
      }
      zh = zmq_socket(context, ZMQ_PUB);
      /*
      if (zh==nullptr) { linerr=__LINE__; zmqerr=zmq_errno(); break; }
      int timeout = 1000;
      zmqerr=zmq_setsockopt(zh, ZMQ_RCVTIMEO, (void*) &timeout, sizeof(int));
      if (zmqerr) { linerr=__LINE__; break; }
      int linger = 0;
      zmqerr=zmq_setsockopt(zh, ZMQ_LINGER, (void*) &linger, sizeof(int));
      if (zmqerr) { linerr=__LINE__; break; }
      */
      zmqerr = zmq_bind(zh, cfgAddress.c_str());
      if (zmqerr) {
        linerr = __LINE__;
        break;
      }
      break;
    }

    if ((zmqerr) || (linerr)) {
      theLog.log(LogErrorSupport_(3236), "ZeroMQ error @%d : (%d) %s", linerr, zmqerr, zmq_strerror(zmqerr));
      throw __LINE__;
    } else {
      theLog.log(LogInfoDevel, "ZeroMQ server started");
    }
  }

  ~ConsumerZMQ()
  {
    if (zh != nullptr) {
      zmq_close(zh);
    }
    if (context != nullptr) {
      zmq_ctx_destroy(context);
    }
  }

  int pushData(DataBlockContainerReference& b)
  {
    bool isOk = 0;

    int nBytes = b->getData()->header.dataSize;
    void* data = b->getData()->data;
    int err = zmq_send(zh, data, nBytes, 0);
    err = 0;
    if (err) {
      theLog.log(LogErrorSupport_(3236), "ZeroMQ send() error : %s", zmq_strerror(err));
      return -1;
    }
    return 0;

    /*
        int nc = tx.size();
        for (int i = 0; i < nc; i++) {
          int k = (i + txIx) % nc;
    //      if (tx[k]->pushData(b) == 0) {
    if (0) {
            txIx = (k + 1) % nc;
            isOk = 1;
            nBytesSent += b->getData()->header.dataSize;
            nBlocksSent++;
            break;
          };
        }
        */

    if (!isOk) {
      nBlocksDropped++;
      nBytesDropped += b->getData()->header.dataSize;
    }

    return 0;
  }

 private:
  void* context = nullptr;
  void* zh = nullptr;
};

std::unique_ptr<Consumer> getUniqueConsumerZMQ(ConfigFile& cfg, std::string cfgEntryPoint) { return std::make_unique<ConsumerZMQ>(cfg, cfgEntryPoint); }
