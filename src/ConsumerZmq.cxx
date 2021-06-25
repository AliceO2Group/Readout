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

  // default ZMQ settings for data monitoring
  // settings for CTP readout: ZMQ_CONFLATE=0,ZMQ_IO_THREADS=4,ZMQ_SNDHWM=1000
  int cfg_ZMQ_CONFLATE = 1; // buffer last message only
  int cfg_ZMQ_IO_THREADS = 1; // number of IO threads
  int cfg_ZMQ_LINGER = 1000; // close timeout
  int cfg_ZMQ_SNDBUF = 16*1024*1024; // kernel transmit buffer size                
  int cfg_ZMQ_SNDHWM = 10; // max send queue size
  int cfg_ZMQ_SNDTIMEO = 1000; // send timeout

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

    // configuration parameter: | consumer-zmq-* | zmqOptions | string |  | Additional ZMQ options, as a comma-separated list of key=value pairs. Possible keys: ZMQ_CONFLATE, ZMQ_IO_THREADS, ZMQ_LINGER, ZMQ_SNDBUF, ZMQ_SNDHWM, ZMQ_SNDTIMEO. |
    std::string cfg_ZMQOptions = "";
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".zmqOptions", cfg_ZMQOptions);
    std::map<std::string, std::string> mapOptions;
    if (getKeyValuePairsFromString(cfg_ZMQOptions, mapOptions)) {
      throw("Can not parse configuration item zmqOptions");
    }
    bool isOk=1;
    for (auto& it : mapOptions) {
      int value = atoi(it.second.c_str());
      if (it.first=="ZMQ_CONFLATE") { cfg_ZMQ_CONFLATE = value; }
      else if (it.first=="ZMQ_IO_THREADS") { cfg_ZMQ_IO_THREADS = value; }
      else if (it.first=="ZMQ_LINGER") { cfg_ZMQ_LINGER = value; }
      else if (it.first=="ZMQ_SNDBUF") { cfg_ZMQ_SNDBUF = value; }
      else if (it.first=="ZMQ_SNDHWM") { cfg_ZMQ_SNDHWM = value; }
      else if (it.first=="ZMQ_SNDTIMEO") { cfg_ZMQ_SNDTIMEO = value; }
      else {
        theLog.log(LogErrorSupport_(3102), "Wrong ZMQ option %s", it.first.c_str());
	isOk = 0;
        continue;
      }
    }
    if (!isOk) {
      throw __LINE__;
    }	
  
    // log config summary
    theLog.log(LogInfoDevel_(3002), "ZeroMQ PUB server @ %s, rate limit = %.4f pages/s, in burst of %d pages", cfgAddress.c_str(), cfgMaxRate, cfgPagesPerBurst);
    theLog.log(LogInfoDevel_(3002), "ZMQ options: ZMQ_SNDHWM=%d ZMQ_CONFLATE=%d ZMQ_SNDTIMEO=%d ZMQ_LINGER=%d ZMQ_SNDBUF=%d ZMQ_IO_THREADS=%d", cfg_ZMQ_SNDHWM, cfg_ZMQ_CONFLATE, cfg_ZMQ_SNDTIMEO, cfg_ZMQ_LINGER, cfg_ZMQ_SNDBUF, cfg_ZMQ_IO_THREADS);
  
    int linerr = 0;
    int zmqerr = 0;
    for (;;) {
      context = zmq_ctx_new();
      if (context == nullptr) {
        linerr = __LINE__;
        zmqerr = zmq_errno();
        break;
      }
      
      zmq_ctx_set(context, ZMQ_IO_THREADS, cfg_ZMQ_IO_THREADS);
      if (zmq_ctx_get(context, ZMQ_IO_THREADS) != cfg_ZMQ_IO_THREADS) {
        linerr = __LINE__;
        break;
      }
      zh = zmq_socket(context, ZMQ_PUB);
      if (zh==nullptr) { linerr=__LINE__; zmqerr=zmq_errno(); break; }
      zmqerr = zmq_bind(zh, cfgAddress.c_str());
      if (zmqerr) { linerr=__LINE__; break; }
      zmqerr = zmq_setsockopt(zh, ZMQ_CONFLATE, &cfg_ZMQ_CONFLATE, sizeof(cfg_ZMQ_CONFLATE));
      if (zmqerr) { linerr=__LINE__; break; }
      zmqerr = zmq_setsockopt(zh, ZMQ_LINGER, (void*)&cfg_ZMQ_LINGER, sizeof(cfg_ZMQ_LINGER));
      if (zmqerr) { linerr=__LINE__; break; }
      zmqerr = zmq_setsockopt(zh, ZMQ_SNDBUF, (void*)&cfg_ZMQ_SNDBUF, sizeof(cfg_ZMQ_SNDBUF));
      if (zmqerr) { linerr=__LINE__; break; }
      zmqerr=zmq_setsockopt(zh, ZMQ_SNDHWM, &cfg_ZMQ_SNDHWM, sizeof(cfg_ZMQ_SNDHWM));
      if (zmqerr) { linerr=__LINE__; break; }
      zmqerr = zmq_setsockopt(zh, ZMQ_SNDTIMEO, (void*)&cfg_ZMQ_SNDTIMEO, sizeof(cfg_ZMQ_SNDTIMEO));
      if (zmqerr) { linerr=__LINE__; break; }
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
    theLog.log(LogInfoDevel_(3003), "ZeroMQ publish stats: %" PRIu64 " blocks %" PRIu64 " bytes", nBlocksSent, nBytesSent);
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
