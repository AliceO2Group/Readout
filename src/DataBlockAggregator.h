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
#include <Common/Thread.h>
#include <Common/Timer.h>
#include <map>
#include <memory>
#include <queue>
#include <vector>

#include "DataBlock.h"
#include "DataBlockContainer.h"
#include "DataSet.h"

using namespace AliceO2::Common;

// DataBlockAggregator
//
// One "slicer" per equipment: data blocks with same sourceId are grouped in a "slice" of blocks having the same TF id.

// a class to group blocks with same ID in slices
class DataBlockSlicer
{

 public:
  DataBlockSlicer();
  ~DataBlockSlicer();

  // append a new block to curent slice of corresponding link a timestamp may be given
  // returns the number of blocks in slice used
  int appendBlock(DataBlockContainerReference const& block, double timestamp = 0);

  // get a slice, if available
  // if includeIncomplete is true, also retrieves current slice, even if incomplete otherwise, only a complete slice is returned, if any when iterated, returned in order of creation, older first
  DataSetReference getSlice(bool includeIncomplete = false);

  // consider the slices which have not been updated since timestamp as complete
  // they are flushed and moved to the "ready" slices
  // returns the number of slices flushed
  int completeSliceOnTimeout(double timestamp);

  // reset all internal variables/state
  // buffered data is released
  void reset();
  
  int slicerId;

 private:
  // data source id used to group data
  struct DataSourceId {
    uint16_t equipmentId = undefinedEquipmentId;
    uint8_t linkId = undefinedLinkId;
  };

  struct PartialSlice {
    uint64_t tfId;                   // timeframeId of this slice
    double lastUpdateTime = 0;       // timestamp of last block pushed
    DataSetReference currentDataSet; // currently associated data
  };

  struct CompareDataSourceId {
    inline bool operator()(const DataSourceId& id1, const DataSourceId& id2) const
    {
      if (id1.equipmentId != id2.equipmentId) {
        return id1.equipmentId < id2.equipmentId;
      } else {
        return id1.linkId < id2.linkId;
      }
    }
  };

  using PartialSliceMap = std::map<DataSourceId, PartialSlice, CompareDataSourceId>;

  const unsigned int maxLinks = 32; // maximum number of links
  PartialSliceMap partialSlices;    // slices being built (one per link)

  std::queue<DataSetReference> slices; // data sets which have been built and are complete
};

class DataBlockAggregator
{
 public:
  DataBlockAggregator(AliceO2::Common::Fifo<DataSetReference>* output, std::string name = "Aggregator");
  ~DataBlockAggregator();

  int addInput(std::shared_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>> input); // add a FIFO to be used as input

  void start();                   // starts processing thread
  void stop(int waitStopped = 1); // stop processing thread (and possibly wait it terminates)

  int disableSlicing = 0; // when set, slicer is disabled, data is just passed through

  double cfgSliceTimeout = 0; // when set, slices not updated after timeout (seconds) are considered completed and are flushed

  static Thread::CallbackResult threadCallback(void* arg);

  Thread::CallbackResult executeCallback();

  bool doFlush = 0; // when set, flush slices including incomplete ones the flag is reset automatically when done

  bool enableStfBuilding = 0; // when set, STF are buffered until all sources have participated. Data from late sources are discarded.
  double cfgStfTimeout = 0;   // timeout used with enableStfBuilding
  int nSources = 0;           // accounted number of sources, on first timeframe

  void reset(); // reset all internal buffers, counters and states

 private:
  std::vector<std::shared_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>>> inputs;
  AliceO2::Common::Fifo<DataSetReference>* output; // todo: unique_ptr

  std::unique_ptr<Thread> aggregateThread;
  AliceO2::Common::Timer incompletePendingTimer;
  AliceO2::Common::Timer timeNow; // a time counter, used to timestamp slices

  int isIncompletePending;

  std::vector<DataBlockSlicer> slicers;
  int nextIndex = 0;                    // index of input channel to start with at next iteration to fill output fifo. not starting always from zero to avoid favorizing low-index channels.
  unsigned long long totalBlocksIn = 0; // number of blocks received from inputs

  // container for sub-subtimeframe (i.e. all data pages of 1 timeframe for a given single source)
  struct tSstf {
    uint64_t sourceId;     // id of the source (equipmentId + linkId);
    DataSetReference data; // data pages for this sstf
    double updateTime;
  };

  // container for one subtimeframe (i.e. sub-subtimeframes of all sources of 1 timeframe)
  struct tStf {
    uint64_t tfId;           // timeframe id
    std::vector<tSstf> sstf; // vector of sub-subtimeframes (1 per source)
    double updateTime;
  };

  typedef std::map<uint64_t, tStf> tStfMap;
  tStfMap stfBuffer;            // buffer to hold pending subtimeframes
  uint64_t lastTimeframeId = 0; // counter for last timeframe id sent out
};
