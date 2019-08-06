// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQMessage.h>
#include <fairmq/FairMQTransportFactory.h>
#include <memory>
#include <vector>

int main() {

  std::string cfgTransportType = "shmem";
  std::string cfgChannelName = "test";
  std::string cfgChannelType = "pair";
  std::string cfgChannelAddress = "ipc:///tmp/test-pipe";

  auto factory =
      FairMQTransportFactory::CreateTransportFactory(cfgTransportType);
  auto pull = FairMQChannel{cfgChannelName, cfgChannelType, factory};
  pull.Connect(cfgChannelAddress);
  int64_t ret;

  for (;;) {
    std::vector<FairMQMessagePtr> msgs;

    ret = pull.Receive(msgs);
    if (ret > 0) {
      for (auto &msg : msgs) {
        int sz = (int)msg->GetSize();
        void *data = msg->GetData();
        printf("Received message %p size %d\n", data, sz);
        printf("Releasing message %p size %d\n", data, sz);
      }
    } else {
      printf("Error while receiving messages %d\n", (int)ret);
      return -1;
    }
  }

  return 0;
}
