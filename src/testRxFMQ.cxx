#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQMessage.h>
#include <fairmq/FairMQTransportFactory.h>
#include <memory>

int main() {

  std::string cfgTransportType="shmem";
  std::string cfgChannelName="test";
  std::string cfgChannelType="pair";
  std::string cfgChannelAddress="ipc:///tmp/test-pipe";

  auto factory = FairMQTransportFactory::CreateTransportFactory(cfgTransportType);
  auto pull = FairMQChannel{cfgChannelName, cfgChannelType, factory};
  pull.Connect(cfgChannelAddress);

  for (;;) {
    auto msg = pull.NewMessage();
    if (pull.ReceiveAsync(msg)>0) {
      if (msg->GetSize()==0) {continue;}
      int sz=(int)msg->GetSize();
      void *data=msg->GetData();
      printf("Received message %p size %d\n",data,sz);
      sleep(1);
      printf("Releasing message %p size %d\n",data,sz);
    } else {
      usleep(1000);
    }
  }

  return 0;
}
