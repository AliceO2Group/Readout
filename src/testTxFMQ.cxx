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
    
  int bufferSize=100*1024*1024;
  auto memoryBuffer=channel.Transport()->CreateUnmanagedRegion(
    bufferSize,
    [](void* data, size_t size, void* hint) {
      // cleanup callback
      printf("ack %p (size %d) hint=%p\n",data,(int)size,hint);                                                           
    }
  );
  printf("Created buffer %p size %ld\n",memoryBuffer->GetData(),memoryBuffer->GetSize());
  size_t msgSize=100;
  for (int ix=0;ix<bufferSize;ix+=msgSize) {
    void *dataPtr=(void *)(&((char *)memoryBuffer->GetData())[ix]);
    void *hint=(void *)ix;
    std::unique_ptr<FairMQMessage> msg(transportFactory->CreateMessage(memoryBuffer,dataPtr,msgSize,hint));
    printf("send %p : %ld bytes hint=%p\n",dataPtr,msgSize,hint);
    channel.Send(msg);
    usleep(3000000);
  }

  return 0;
}
