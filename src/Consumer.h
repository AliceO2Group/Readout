// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include <Common/Configuration.h>
#include <Common/Fifo.h>

#include <Common/DataBlock.h>
#include <Common/DataBlockContainer.h>
#include <Common/DataSet.h>

#include <memory>

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;

class Consumer {
public:
  Consumer(ConfigFile &, std::string){};
  virtual ~Consumer(){};
  virtual int pushData(DataBlockContainerReference &b) = 0;

  // iterate through blocks of a dataset, using the per-block pushData() method.
  // Returns number of successfully pushed blocks in set.
  virtual int pushData(DataSetReference &bc);

  virtual int start() {
    return 0;
  }; // function called just before starting data taking. Data will soon start
     // to flow in.
  virtual int stop() {
    return 0;
  }; // function called just after stopping data taking, after the last call to
     // pushData(). Not called before input FIFO empty.

public:
  Consumer *forwardConsumer =
      nullptr; // consumer where to push output data, if any
  bool isForwardConsumer =
      false; // this consumer will get data from output of another consumer
  std::string name; // name of this consumer
  bool stopOnError =
      false; // if set, readout will stop when this consumer reports an error
             // (isError flag or pushData() failing)
  int isError = 0; // flag which might be used to count number of errors
                   // occuring in the consumer
  bool isErrorReported =
      false; // flag to keep track of error reports for this consumer
};

std::unique_ptr<Consumer> getUniqueConsumerStats(ConfigFile &cfg,
                                                 std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerFMQ(ConfigFile &cfg,
                                               std::string cfgEntryPoint);
std::unique_ptr<Consumer>
getUniqueConsumerFMQchannel(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer>
getUniqueConsumerFileRecorder(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer>
getUniqueConsumerDataChecker(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer>
getUniqueConsumerDataProcessor(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer>
getUniqueConsumerDataSampling(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerTCP(ConfigFile &cfg,
                                               std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerRDMA(ConfigFile &cfg,
                                                std::string cfgEntryPoint);
