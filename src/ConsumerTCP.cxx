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
#include "SocketTx.hxx"
#include "ReadoutUtils.h"

class ConsumerTCP: public Consumer {
  public:
  std::vector<std::unique_ptr<SocketTx>> tx;
  uint64_t block_ix=0;
  int txIx=0;
  uint64_t nBlocksDropped=0;
  uint64_t nBytesDropped=0;
  uint64_t nBytesSent=0;
  uint64_t nBlocksSent=0;
   
  ConsumerTCP(ConfigFile &cfg, std::string cfgEntryPoint):Consumer(cfg,cfgEntryPoint) {
    
    // configuration parameter: | consumer-tcp-* | port | int | 10001 | Remote server TCP port number to connect to. |
    int cfgPort=10001; // remote server port
    cfg.getOptionalValue<int>(cfgEntryPoint + ".port", cfgPort);
    
    // configuration parameter: | consumer-tcp-* | host | string | localhost | Remote server IP name to connect to. |
    std::string cfgHost="localhost"; // remote server address
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".host", cfgHost);
    
    // configuration parameter: | consumer-tcp-* | ncx | int | 1 | Number of parallel streams (and threads) to use. The port number specified in 'port' parameter will be increased by 1 for each extra connection. |
    int cfgNcx=1; // number of parallel connections (using ports from port to port+ncx-1
    cfg.getOptionalValue<int>(cfgEntryPoint + ".ncx", cfgNcx);
    theLog.log("TCP client connecting to %s:%d-%d",cfgHost.c_str(),cfgPort,cfgPort+cfgNcx-1);
    
    for (int i=0;i<cfgNcx;i++) {
      int p=cfgPort+i;
      tx.push_back(std::make_unique<SocketTx>("Readout",cfgHost.c_str(),p));
    }
  }
  ~ConsumerTCP() {
    int nc=tx.size();
    for (int i=0;i<nc;i++) {
      tx[i]=nullptr;
    }
    tx.clear();

     theLog.log("TCP client:  %llu blocks sent, %llu blocks dropped", nBlocksSent,nBlocksDropped);
     theLog.log("TCP client:  %s sent,%s dropped",
       NumberOfBytesToString(nBytesSent,"bytes",1024).c_str(),
       NumberOfBytesToString(nBytesDropped,"bytes",1024).c_str()
       );
  }
  int pushData(DataBlockContainerReference &b) {
    bool isOk=0;
    int nc=tx.size();
    for (int i=0;i<nc;i++) {
      int k=(i+txIx) % nc;
      if (tx[k]->pushData(b)==0) {
        txIx=(k+1) % nc;
	isOk=1;
	nBytesSent+=b->getData()->header.dataSize;
	nBlocksSent++;
        break;
      };
    }
    if (!isOk) {
      nBlocksDropped++;
      nBytesDropped+=b->getData()->header.dataSize;
    }
    return 0;
  }
  private:
};


std::unique_ptr<Consumer> getUniqueConsumerTCP(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ConsumerTCP>(cfg, cfgEntryPoint);
}
