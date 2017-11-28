#include "Consumer.h"


#ifdef WITH_FAIRMQ

#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQMessage.h>
#include <fairmq/FairMQTransportFactory.h>
#include <fairmq/zeromq/FairMQTransportFactoryZMQ.h>

namespace {

// cleanup function, defined with the callback footprint expected in the 3rd argument of FairMQTransportFactory.CreateMessage()
// when object not null, it should be a (DataBlockContainerReference *), which will be destroyed
void cleanupCallback(void *data, void *object) {
    if ((object!=nullptr)&&(data!=nullptr)) {
      DataBlockContainerReference *ptr=(DataBlockContainerReference *)object;
//      printf("ptr %p: use_count=%d\n",ptr,ptr->use_count());
      delete ptr;
    }
  }

}

/*
TODO: add with/without state machine
*/

class ConsumerFMQchannel: public Consumer {
  private:
    std::unique_ptr<FairMQChannel> sendingChannel;
    std::shared_ptr<FairMQTransportFactory> transportFactory;   
        
  public: 


  ConsumerFMQchannel(ConfigFile &cfg, std::string cfgEntryPoint) : Consumer(cfg,cfgEntryPoint) {
  
    transportFactory=FairMQTransportFactory::CreateTransportFactory("zeromq");
    sendingChannel=std::make_unique<FairMQChannel>("readout-out", "pub", transportFactory);
    sendingChannel->Bind("tcp://*:5555");
    
    if (!sendingChannel->ValidateChannel()) {
      throw "ConsumerFMQ: channel validation failed";
    }
    
  }
  
  ~ConsumerFMQchannel() {
  }
  
  int pushData(DataBlockContainerReference &b) {

    // we create a copy of the reference, in a newly allocated object, so that reference is kept alive until this new object is destroyed in the cleanupCallback
    DataBlockContainerReference *ptr=new DataBlockContainerReference(b);
    std::unique_ptr<FairMQMessage> msgHeader(transportFactory->CreateMessage((void *)&(b->getData()->header), (size_t)(b->getData()->header.headerSize), cleanupCallback, (void *)nullptr));
    std::unique_ptr<FairMQMessage> msgBody(transportFactory->CreateMessage((void *)(b->getData()->data), (size_t)(b->getData()->header.dataSize), cleanupCallback, (void *)(ptr)));

    sendingChannel->Send(msgHeader);
    sendingChannel->Send(msgBody);    
    return 0;
  }
  
  private:
};



std::unique_ptr<Consumer> getUniqueConsumerFMQchannel(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ConsumerFMQchannel>(cfg, cfgEntryPoint);
}



#endif


