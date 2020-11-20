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
#include "ReadoutUtils.h"

Consumer::Consumer(ConfigFile& cfg, std::string cfgEntryPoint)
{
  // configuration parameter: | consumer-* | filterLinksInclude | string |  | Defines a filter based on link ids. Only data belonging to the links in this list (coma separated values) are accepted. If empty, all link ids are fine. |
  std::string cfgFilterLinksInclude;
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".filterLinksInclude", cfgFilterLinksInclude);
  if (!getIntegerListFromString(cfgFilterLinksInclude, filterLinksInclude)) {
    throw("Can not parse configuration item filterLinksInclude");
  }
  // configuration parameter: | consumer-* | filterLinksExclude | string |  | Defines a filter based on link ids. All data belonging to the links in this list (coma separated values) are rejected. |
  std::string cfgFilterLinksExclude;
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".filterLinksExclude", cfgFilterLinksExclude);
  if (!getIntegerListFromString(cfgFilterLinksExclude, filterLinksExclude)) {
    throw("Can not parse configuration item filterLinksExclude");
  }
  if (filterLinksInclude.size() || filterLinksExclude.size()) {
    filterLinksEnabled = 1;    
    theLog.log(LogInfoDevel_(3002), "Filtering on links enabled: include=%s exclude=%s", cfgFilterLinksInclude.c_str(), cfgFilterLinksExclude.c_str());
  }
}

int Consumer::pushData(DataSetReference& bc)
{
  int success = 0;
  int error = 0;
  for (auto& b : *bc) {
    DataBlock* db = b->getData();
    if (db == nullptr) {
      continue;
    }
    if (db->data == nullptr) {
      continue;
    }
    if (!isDataBlockFilterOk(*db)) {
      totalBlocksFiltered++;
      continue;
    } else {
      totalBlocksUnfiltered++;
    }
    if (!pushData(b)) {
      success++;
    } else {
      error++;
    }
  }
  if (error) {
    totalPushError++;
    // return a negative number indicating number of errors
    return -error;
  }
  totalPushSuccess++;
  // return a positive number indicating number of successes
  return success;
}

bool Consumer::isDataBlockFilterOk(const DataBlock& b) {
  if (!filterLinksEnabled) {
    return 1;
  }
  int linkId = b.header.linkId;
  for(auto i : filterLinksExclude) {
    if (i == linkId) {
      return 0;
    }
  }
  if (filterLinksInclude.size() == 0) {
    return 1;
  }
  for(auto i : filterLinksInclude) {
    if (i == linkId) {
      return 1;
    }
  }
  return 0;
}
