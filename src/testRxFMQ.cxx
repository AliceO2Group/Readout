#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQMessage.h>
#include <fairmq/FairMQTransportFactory.h>
#include <fairmq/zeromq/FairMQTransportFactoryZMQ.h>
#include <memory>
#include <vector>

int main() {

  std::string cfgTransportType="shmem";
  std::string cfgChannelName="test";
  std::string cfgChannelType="pair";
  std::string cfgChannelAddress="ipc:///tmp/test-pipe";

  auto factory = FairMQTransportFactory::CreateTransportFactory(cfgTransportType);
  auto pull = FairMQChannel{cfgChannelName, cfgChannelType, factory};
  pull.Connect(cfgChannelAddress);
  int64_t ret;

  for (;;) {
    std::vector<FairMQMessagePtr> msgs;

    ret = pull.Receive(msgs);
    if (ret > 0) {
      for (auto &msg : msgs) {
        int sz=(int)msg->GetSize();
        void *data=msg->GetData();
        printf("Received message %p size %d\n",data,sz);
        printf("Releasing message %p size %d\n",data,sz);
      }
    } else {
      printf("Error while receiving messages %d\n", (int)ret);
      return -1;
    }
  }

  return 0;
}
