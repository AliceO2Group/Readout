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
  if (getIntegerListFromString(cfgFilterLinksInclude, filterLinksInclude) < 0) {
    throw("Can not parse configuration item filterLinksInclude");
  }
  // configuration parameter: | consumer-* | filterLinksExclude | string |  | Defines a filter based on link ids. All data belonging to the links in this list (coma separated values) are rejected. |
  std::string cfgFilterLinksExclude;
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".filterLinksExclude", cfgFilterLinksExclude);
  if (getIntegerListFromString(cfgFilterLinksExclude, filterLinksExclude) < 0) {
    throw("Can not parse configuration item filterLinksExclude");
  }
  if (filterLinksInclude.size() || filterLinksExclude.size()) {
    filterLinksEnabled = 1;
    theLog.log(LogInfoDevel_(3002), "Filtering on links enabled: include=%s exclude=%s", cfgFilterLinksInclude.c_str(), cfgFilterLinksExclude.c_str());
  }

  // configuration parameter: | consumer-* | filterEquipmentIdsInclude | string |  | Defines a filter based on equipment ids. Only data belonging to the equipments in this list (coma separated values) are accepted. If empty, all equipment ids are fine. |
  std::string cfgFilterEquipmentIdsInclude;
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".filterEquipmentIdsInclude", cfgFilterEquipmentIdsInclude);
  if (getIntegerListFromString(cfgFilterEquipmentIdsInclude, filterEquipmentIdsInclude) < 0) {
    throw("Can not parse configuration item filterEquipmentIdsInclude");
  }
  // configuration parameter: | consumer-* | filterEquipmentIdsExclude | string |  | Defines a filter based on equipment ids. All data belonging to the equipments in this list (coma separated values) are rejected. |
  std::string cfgFilterEquipmentIdsExclude;
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".filterEquipmentIdsExclude", cfgFilterEquipmentIdsExclude);
  if (getIntegerListFromString(cfgFilterEquipmentIdsExclude, filterEquipmentIdsExclude) < 0) {
    throw("Can not parse configuration item filterEquipmentIdsExclude");
  }
  if (filterEquipmentIdsInclude.size() || filterEquipmentIdsExclude.size()) {
    filterEquipmentIdsEnabled = 1;
    theLog.log(LogInfoDevel_(3002), "Filtering on equipment ids enabled: include=%s exclude=%s", cfgFilterEquipmentIdsInclude.c_str(), cfgFilterEquipmentIdsExclude.c_str());
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

bool Consumer::isDataBlockFilterOk(const DataBlock& b)
{
  bool isOk = 1;

  if (filterLinksEnabled) {
    int id = b.header.linkId;
    for (auto i : filterLinksExclude) {
      if (i == id) {
        return 0;
      }
    }
    if (filterLinksInclude.size() != 0) {
      isOk = 0;
      for (auto i : filterLinksInclude) {
        if (i == id) {
          isOk = 1;
          break;
        }
      }
    }
  }
  if (!isOk) {
    return 0;
  }

  if (filterEquipmentIdsEnabled) {
    int id = b.header.equipmentId;
    for (auto i : filterEquipmentIdsExclude) {
      if (i == id) {
        return 0;
      }
    }
    if (filterEquipmentIdsInclude.size() != 0) {
      isOk = 0;
      for (auto i : filterEquipmentIdsInclude) {
        if (i == id) {
          isOk = 1;
          break;
        }
      }
    }
  }
  if (!isOk) {
    return 0;
  }

  return 1;
}
