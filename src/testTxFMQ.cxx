#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQMessage.h>
#include <fairmq/FairMQTransportFactory.h>
#include <fairmq/zeromq/FairMQTransportFactoryZMQ.h>
#include <memory>

int main() {

  std::string cfgTransportType="shmem";
  std::string cfgChannelName="test";
  std::string cfgChannelType="pair";
  std::string cfgChannelAddress="ipc:///tmp/test-pipe";


  auto transportFactory=FairMQTransportFactory::CreateTransportFactory(cfgTransportType);
  auto channel=FairMQChannel{cfgChannelName, cfgChannelType, transportFactory};
  channel.Bind(cfgChannelAddress);
  if (!channel.ValidateChannel()) { return -1; }

  const size_t bufferSize=100*1024*1024;
  auto memoryBuffer=channel.Transport()->CreateUnmanagedRegion(
    bufferSize,
    [](void* data, size_t size, void* hint) {
      // cleanup callback
      printf("ack %p (size %d) hint=%p\n",data,(int)size,hint);
    }
  );
  printf("Created buffer %p size %ld\n",memoryBuffer->GetData(),memoryBuffer->GetSize());
  size_t msgSize=100;

  size_t ix=0;
  while (ix<(bufferSize-msgSize)) {
    // send random number of messages in one multipart [1-50]
    int nmsgs = (rand()%50)+1;
    nmsgs = std::min(nmsgs, (int)((bufferSize-ix)/msgSize));

    std::vector<FairMQMessagePtr> msgs;
    for (int im=0; im < nmsgs; im++, ix+=msgSize) {
      void *dataPtr=(void *)(&((char *)memoryBuffer->GetData())[ix]);
      void *hint=(void *)ix;
      msgs.emplace_back(transportFactory->CreateMessage(memoryBuffer,dataPtr,msgSize,hint));
      printf("send %p : %ld bytes hint=%p\n",dataPtr,msgSize,hint);
    }
    printf("* sending %lu messages\n",msgs.size());
    channel.Send(msgs);
    sleep(2);
  }

  return 0;
}
