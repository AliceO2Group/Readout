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
#include <inttypes.h>
#include "RateRegulator.h"

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
  int cfgZmqMaxQueue = 10;
  int cfgZmqConflate = 1;
  int cfgZmqSendTimeout = 1000;
  int cfgZmqLinger = 1000;
  double cfgMaxRate = 0; // max number of pages per second (average)
  int cfgPagesPerBurst = 1; // number of pages per burst (peak successive pages accepted without avg rate check)
  int pagesInBurst = 0; // current number of pages in burst
  RateRegulator blockRate;

  ConsumerZMQ(ConfigFile& cfg, std::string cfgEntryPoint) : Consumer(cfg, cfgEntryPoint)
  {

    // configuration parameter: | consumer-zmq-* | address | string| tcp://127.0.0.1:50001 | ZMQ address where to publish (PUB) data pages, eg ipc://@readout-eventDump |
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".address", cfgAddress);
    // configuration parameter: | consumer-zmq-* | maxRate | int| 0 | Maximum number of pages to publish per second. The associated memory copy has an impact on cpu load, so this should be limited when one does not use all the data (eg for eventDump). |
    cfg.getOptionalValue<double>(cfgEntryPoint + ".maxRate", cfgMaxRate);
   // configuration parameter: | consumer-zmq-* | pagesPerBurst | int | 1 | Number of consecutive pages guaranteed to be part of each publish sequence. The maxRate limit is checked at the end of each burst. |
    cfg.getOptionalValue<int>(cfgEntryPoint + ".pagesPerBurst", cfgPagesPerBurst);
    if (cfgMaxRate < 0) {
      cfgMaxRate = 0;
    }
    if (cfgPagesPerBurst < 1) {
      cfgPagesPerBurst = 1;
    }
    theLog.log(LogInfoDevel_(3002), "ZeroMQ server @ %s, rate limit = %.4f pages/s, in burst of %d pages", cfgAddress.c_str(), cfgMaxRate, cfgPagesPerBurst);

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
      zmq_setsockopt(zh, ZMQ_SNDHWM, &cfgZmqMaxQueue, sizeof(cfgZmqMaxQueue)); // max queue size
      zmq_setsockopt(zh, ZMQ_CONFLATE, &cfgZmqConflate, sizeof(cfgZmqConflate)); // buffer last message only
      zmq_setsockopt(zh, ZMQ_SNDTIMEO, (void*)&cfgZmqSendTimeout, sizeof(cfgZmqSendTimeout)); // send timeout
      zmq_setsockopt(zh, ZMQ_LINGER, (void*)&cfgZmqLinger, sizeof(cfgZmqLinger)); // close timeout
      break;
    }

    if ((zmqerr) || (linerr)) {
      theLog.log(LogErrorSupport_(3236), "ZeroMQ error @%d : (%d) %s", linerr, zmqerr, zmq_strerror(zmqerr));
      throw __LINE__;
    } else {
      theLog.log(LogInfoDevel, "ZeroMQ server started");      
    }
    
    blockRate.init(cfgMaxRate / cfgPagesPerBurst);
  }

  ~ConsumerZMQ()
  {
    if (zh != nullptr) {
      zmq_close(zh);
    }
    if (context != nullptr) {
      zmq_ctx_destroy(context);
    }
    // the stats are not meaningfull for a ZMQ PUB: send always works...
    // theLog.log(LogInfoDevel_(3003), "ZeroMQ stats: %" PRIu64 "/%" PRIu64 " blocks sent/discarded", nBlocksSent, nBlocksDropped);
  }

  int pushData(DataBlockContainerReference& b)
  {
    int nBytes = b->getData()->header.dataSize;
    void* data = b->getData()->data;
    bool success = 0;

    // check rate throttling
    bool throttle = 0;    
    if (pagesInBurst == 0) {
      if (!blockRate.next()) {
        throttle = 1;
      }
    }
    if (!throttle) { 
      pagesInBurst++;
      if (pagesInBurst == cfgPagesPerBurst) {
        pagesInBurst = 0;
      }
      int err = zmq_send(zh, data, nBytes, 0);
      err = 0;
      if (err) {
        theLog.log(LogErrorSupport_(3236), "ZeroMQ send() error : %s", zmq_strerror(err));
      } else {
        success = 1;
      }
    }
    
    if (success) {  
      nBlocksSent++;
      nBytesSent += nBytes;
      return 0;
    }
    nBlocksDropped++;
    nBytesDropped += nBytes;
    return -1;
  }

 private:
  void* context = nullptr;
  void* zh = nullptr;
};

std::unique_ptr<Consumer> getUniqueConsumerZMQ(ConfigFile& cfg, std::string cfgEntryPoint) { return std::make_unique<ConsumerZMQ>(cfg, cfgEntryPoint); }
