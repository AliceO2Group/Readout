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
#include <memory>

#include "DataBlock.h"
#include "DataBlockContainer.h"
#include "DataSet.h"
#include "readoutInfoLogger.h"

class Consumer
{
 public:
  Consumer(ConfigFile&, std::string);
  virtual ~Consumer(){};
  virtual int pushData(DataBlockContainerReference& b) = 0;

  // Iterate through blocks of a dataset, using the per-block pushData() method.
  // Returns number of successfully pushed blocks in set.
  virtual int pushData(DataSetReference& bc);

  // Function called just before starting data taking. Data will soon start to flow in.
  virtual int start()
  {
    totalPushSuccess = 0;
    totalPushError = 0;
    totalBlocksFiltered = 0;
    totalBlocksUnfiltered = 0;
    return 0;
  };

  // Function called just after stopping data taking, after the last call to pushData(). Not called before input FIFO empty.
  virtual int stop()
  {
    theLog.log(LogInfoDevel_(3003), "Push statistics for %s: %llu err / %llu total (DataSets), %llu/%llu filtered (DataBlocks)", this->name.c_str(), totalPushError, totalPushError + totalPushSuccess, totalBlocksFiltered, totalBlocksUnfiltered + totalBlocksFiltered);
    return 0;
  };

 public:
  Consumer* forwardConsumer = nullptr; // consumer where to push output data, if any
  bool isForwardConsumer = false;      // this consumer will get data from output of another consumer
  std::string name;                    // name of this consumer
  bool stopOnError = false;            // if set, readout will stop when this consumer reports an error (isError flag or pushData() failing)
  int isError = 0;                     // flag which might be used to count number of errors occuring in the consumer
  bool isErrorReported = false;        // flag to keep track of error reports for this consumer
  unsigned long long totalPushSuccess = 0;
  unsigned long long totalPushError = 0;
  unsigned long long totalBlocksFiltered = 0;
  unsigned long long totalBlocksUnfiltered = 0;
  
 protected:
  // check if a DataBlock passes defined filters. Return 1 if ok, zero if not.
  // if link in list of excluded filters: 0
  // if link in list of included filters, or included filters list empty: 1
  // empty datablocks are always excluded
  bool isDataBlockFilterOk(const DataBlock&);
  
 private:
  bool filterLinksEnabled = 0; // when set, defines a filter link
  std::vector<int> filterLinksInclude; // match is OK only for links in this list (or for all if list is empty).
  std::vector<int> filterLinksExclude; // match is NOT OK for any link in this list.
  bool filterEquipmentIdsEnabled = 0; // when set, defines a filter based on equipmentId
  std::vector<int> filterEquipmentIdsInclude; // match is OK only for ids in this list (or for all if list is empty).
  std::vector<int> filterEquipmentIdsExclude; // match is NOT OK for any id in this list.

};

std::unique_ptr<Consumer> getUniqueConsumerStats(ConfigFile& cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerFMQ(ConfigFile& cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerFMQchannel(ConfigFile& cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerFileRecorder(ConfigFile& cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerDataChecker(ConfigFile& cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerDataProcessor(ConfigFile& cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerDataSampling(ConfigFile& cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerTCP(ConfigFile& cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerRDMA(ConfigFile& cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerZMQ(ConfigFile& cfg, std::string cfgEntryPoint);
