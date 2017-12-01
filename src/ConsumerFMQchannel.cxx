#include "Consumer.h"
#include "ReadoutUtils.h"
#include "MemoryHandler.h"


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
    FairMQUnmanagedRegionPtr memoryBuffer=nullptr;
    bool disableSending=0;
    
  public: 


  ConsumerFMQchannel(ConfigFile &cfg, std::string cfgEntryPoint) : Consumer(cfg,cfgEntryPoint) {

    int cfgDisableSending=0;
    cfg.getOptionalValue<int>(cfgEntryPoint + ".disableSending", cfgDisableSending);
    if (cfgDisableSending) {
      theLog.log("FMQ message sending disabled");
      disableSending=true;
    }
    
    std::string cfgTransportType="zeromq";
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".transportType", cfgTransportType);
    
    std::string cfgChannelName="readout";
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".channelName", cfgChannelName);
    
    std::string cfgChannelType="pair";
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".channelType", cfgChannelType);

    std::string cfgChannelAddress="ipc:///tmp/pipe-readout";
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".channelAddress", cfgChannelAddress);

    theLog.log("Creating FMQ TX channel %s type %s:%s @ %s",cfgChannelName.c_str(),cfgTransportType.c_str(),cfgChannelType.c_str(),cfgChannelAddress.c_str());
            
    transportFactory=FairMQTransportFactory::CreateTransportFactory(cfgTransportType);
    sendingChannel=std::make_unique<FairMQChannel>(cfgChannelName, cfgChannelType, transportFactory);
    
    std::string cfgUnmanagedMemorySize="";
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".unmanagedMemorySize",cfgUnmanagedMemorySize);
    long long mMemorySize=ReadoutUtils::getNumberOfBytesFromString(cfgUnmanagedMemorySize.c_str());
    if (mMemorySize>0) {
      memoryBuffer=sendingChannel->Transport()->CreateUnmanagedRegion(mMemorySize);
      theLog.log("Got FMQ unmanaged memory buffer size %lu @ %p",memoryBuffer->GetSize(),memoryBuffer->GetData());
      std::unique_ptr<MemoryRegion> m;
      m=std::make_unique<MemoryRegion>();
      m->name="FMQ unmanaged memory buffer";
      m->size=memoryBuffer->GetSize();
      m->ptr=memoryBuffer->GetData();
      bigBlock=std::move(m);
    }
    
    
    sendingChannel->Bind(cfgChannelAddress);
    
    if (!sendingChannel->ValidateChannel()) {
      throw "ConsumerFMQ: channel validation failed";
    }
    
  }
  
  ~ConsumerFMQchannel() {
    bigBlock=nullptr;
  }
  
  int pushData(DataBlockContainerReference &b) {

    if (disableSending) return 0;
    
    // we create a copy of the reference, in a newly allocated object, so that reference is kept alive until this new object is destroyed in the cleanupCallback
    DataBlockContainerReference *ptr=new DataBlockContainerReference(b);
    //std::unique_ptr<FairMQMessage> msgHeader(transportFactory->CreateMessage((void *)&(b->getData()->header), (size_t)(b->getData()->header.headerSize), cleanupCallback, (void *)nullptr));
    std::unique_ptr<FairMQMessage> msgBody(transportFactory->CreateMessage((void *)(b->getData()->data), (size_t)(b->getData()->header.dataSize), cleanupCallback, (void *)(ptr)));

    //sendingChannel->Send(msgHeader);
    sendingChannel->Send(msgBody);    
    return 0;
  }
  
  private:
};



std::unique_ptr<Consumer> getUniqueConsumerFMQchannel(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ConsumerFMQchannel>(cfg, cfgEntryPoint);
}



#endif


