#include "Consumer.h"
#include "ReadoutUtils.h"
#include "MemoryHandler.h"


#ifdef WITH_FAIRMQ

#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQMessage.h>
#include <fairmq/FairMQTransportFactory.h>
#include <fairmq/zeromq/FairMQTransportFactoryZMQ.h>

#include "RAWDataHeader.h"
#include "SubTimeframe.h"

/*
namespace {

// cleanup function, defined with the callback footprint expected in the 3rd argument of FairMQTransportFactory.CreateMessage()
// when object not null, it should be a (DataBlockContainerReference *), which will be destroyed
void cleanupCallback(void *data, void *object) {
    if ((object!=nullptr)&&(data!=nullptr)) {
      DataBlockContainerReference *ptr=(DataBlockContainerReference *)object;
      //printf("release ptr %p: use_count=%d\n",ptr,ptr->use_count());
      delete ptr;
    }
  }

}

void cleanupCallbackForMalloc(void *data, void *object) {
    if ((object!=nullptr)&&(data!=nullptr)) {
      free(object);
    }
}
*/


/*
TODO: add with/without state machine
*/

class ConsumerFMQchannel: public Consumer {
  private:
    std::unique_ptr<FairMQChannel> sendingChannel;
    std::shared_ptr<FairMQTransportFactory> transportFactory;   
    FairMQUnmanagedRegionPtr memoryBuffer=nullptr;
    bool disableSending=0;
    
    int memPoolNumberOfElements;  // number of pages in memory pool
    int memPoolElementSize;       // size of each page
    std::shared_ptr<MemoryHandler> mh;  // a memory pool from which to allocate data pages
    
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
      memoryBuffer=sendingChannel->Transport()->CreateUnmanagedRegion(mMemorySize,[this](void* data, size_t size, void *hint) { // cleanup callback
        //printf("ack %p (size %d) hint=%p\n",data,(int)size,hint);
        
        if (hint!=nullptr) {
          DataBlockContainerReference *blockRef=(DataBlockContainerReference *)hint;
          delete blockRef;
        }
      });
      
      theLog.log("Got FMQ unmanaged memory buffer size %lu @ %p",memoryBuffer->GetSize(),memoryBuffer->GetData());
      std::unique_ptr<MemoryRegion> m;
      m=std::make_unique<MemoryRegion>();
      m->name="FMQ unmanaged memory buffer";
      m->size=memoryBuffer->GetSize();
      m->ptr=memoryBuffer->GetData();
      m->usedSize=0;
      bigBlock=std::move(m);
    }
    
    // allocate a pool of pages for headers/data copies
    cfg.getOptionalValue<int>(cfgEntryPoint + ".memPoolNumberOfElements", memPoolNumberOfElements,100);
    std::string cfgMemPoolElementSize;
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".memPoolElementSize", cfgMemPoolElementSize);
    memPoolElementSize=ReadoutUtils::getNumberOfBytesFromString(cfgMemPoolElementSize.c_str());
    if (memPoolElementSize<=0) {
      memPoolElementSize=128*1024;
    }
    mh=std::make_shared<MemoryHandler>(memPoolElementSize,memPoolNumberOfElements);

        
    sendingChannel->Bind(cfgChannelAddress);
    
    if (!sendingChannel->ValidateChannel()) {
      throw "ConsumerFMQ: channel validation failed";
    }
    
  }
  
  ~ConsumerFMQchannel() {
    bigBlock=nullptr;
  }
  
  int pushData(DataBlockContainerReference &) {
    // this consumer does not accept a per-block push, it needs a set
    return -1;
  }
  
  int pushData(DataSetReference &bc) {
  
    if (disableSending) {
      return 0;
    }

  
    const unsigned int cruBlockSize=8192;
    // todo: replace by RDHv3 length
   
    // we iterate a first time to count number of HB
    if (memPoolElementSize<(int)sizeof(SubTimeframe)) {return -1;}
    DataBlockContainerReference headerBlock=nullptr;
    try {
      headerBlock=std::make_shared<DataBlockContainerFromMemoryHandler>(mh);
    }
    catch (...) {
    }
    if (headerBlock==nullptr) {return -1;}
    auto blockRef=new DataBlockContainerReference(headerBlock);
    SubTimeframe *stfHeader=(SubTimeframe *)headerBlock->getData()->data;
    if (stfHeader==nullptr) {return -1;}
    stfHeader->timeframeId=0;
    stfHeader->numberOfHBF=0;
    stfHeader->linkId=0;

    unsigned int lastHBid=-1;
    int isFirst=true;
    int ix=0;
    for (auto &br : *bc) {
      ix++;
      DataBlock *b=br->getData();
      if (isFirst) {
        stfHeader->timeframeId=b->header.id;
        stfHeader->linkId=b->header.linkId;
        stfHeader->numberOfHBF=0;
        isFirst=false;
      } else {
        if (stfHeader->timeframeId!=b->header.id) {printf("mismatch tfId\n");}
        if (stfHeader->linkId!=b->header.linkId) {printf("mismatch linkId\n");}       
      }
      //printf("block %d tf %d link %d\n",ix,b->header.id,b->header.linkId);
      for (int offset=0;offset+cruBlockSize<=b->header.dataSize;offset+=cruBlockSize) {
        //printf("checking %p : %d\n",b,offset);
        o2::Header::RAWDataHeader *rdh=(o2::Header::RAWDataHeader *)&b->data[offset];       
        if (rdh->heartbeatOrbit!=lastHBid) {
          stfHeader->numberOfHBF++;
          lastHBid=rdh->heartbeatOrbit;
          //printf("offset %d now %d HBF - HBid=%d\n",offset,stfHeader->numberOfHBF,lastHBid);
        }
        if (stfHeader->linkId!=rdh->linkId) {
          printf("Warning: TF%d link Id mismatch %d != %d\n",(int)stfHeader->timeframeId,(int)stfHeader->linkId,(int)rdh->linkId);
          //dumpRDH(rdh);
          //printf("block %p : offset %d = %p\n",b,offset,rdh);
        }
      }
    }  
    
    //printf("TF %d link %d = %d blocks, %d HBf\n",(int)stfHeader->timeframeId,(int)stfHeader->linkId,(int)bc->size(),(int)stfHeader->numberOfHBF);
    
    // create a header message
    //std::unique_ptr<FairMQMessage> msgHeader(transportFactory->CreateMessage((void *)stfHeader, sizeof(SubTimeframe), cleanupCallback, (void *)(blockRef)));
    std::unique_ptr<FairMQMessage> msgHeader(transportFactory->CreateMessage(memoryBuffer,(void *)stfHeader, sizeof(SubTimeframe), (void *)(blockRef)));
    sendingChannel->Send(msgHeader);
    //printf("sent header %d bytes\n",(int)sizeof(SubTimeframe));

    
    // cut: one message per HBf
    lastHBid=-1;
    
    // this is for data not sent yet (from one loop to the next)
    struct pendingFrame {
     DataBlockContainerReference *blockRef;
     unsigned int HBstart;
     unsigned int HBlength;
     unsigned int HBid;
    };
    std::vector<pendingFrame> pendingFrames;
    
    auto pendingFramesAppend = [&](unsigned int ix, unsigned int l, unsigned int id, DataBlockContainerReference br) {
        pendingFrame pf;
        pf.HBstart=ix;
        pf.HBlength=l;
        pf.HBid=id;
        // we create a copy of the reference, in a newly allocated object, so that reference is kept alive until this new object is destroyed in the cleanupCallback
        pf.blockRef=new DataBlockContainerReference(br);
        //printf("allocating blockRef %p for %p\n",pf.blockRef,br);
        pendingFrames.push_back(pf);       
    };
    
    auto pendingFramesSend = [&]() {
      int nFrames=pendingFrames.size();

      if (nFrames==0) {
        //printf("no pending frames\n");
        return;
      }
           
      if (nFrames==1) {
        // single block, no need to repack
        auto br=*(pendingFrames[0].blockRef);
        DataBlock *b=br->getData();
        int ix=pendingFrames[0].HBstart;
        int l=pendingFrames[0].HBlength;
//        std::unique_ptr<FairMQMessage> msgBody(transportFactory->CreateMessage((void *)(&(b->data[ix])),(size_t)(l), cleanupCallback, (void *)(pendingFrames[0].blockRef)));
        void *hint=(void *)pendingFrames[0].blockRef;
        //printf("block %p ix = %d : %d hint=%p\n",(void *)(&(b->data[ix])),ix,l,hint);
        //std::cout << typeid(pendingFrames[0].blockRef).name() << std::endl;
        std::unique_ptr<FairMQMessage> msgBody(transportFactory->CreateMessage(memoryBuffer,(void *)(&(b->data[ix])),(size_t)(l),hint));
        sendingChannel->Send(msgBody);        
        //printf("sent single HB %d = %d bytes\n",pendingFrames[0].HBid,l);
        //printf("left to FMQ: %p\n",pendingFrames[0].blockRef);

      } else {
        // todo : account number of repack-copies in this situation
        
        // multiple blocks, need to repack
        int totalSize=0;
        for (auto &f : pendingFrames) {
          totalSize+=f.HBlength;
        }
        // allocate
        // todo: same code as for header -> create func/lambda
        // todo: send empty message if no page left in buffer
        if (memPoolElementSize<totalSize) {
          printf("error page size too small %d < %d\n",memPoolElementSize,totalSize);
          return;
        }
        DataBlockContainerReference copyBlock=nullptr;
        try {
          copyBlock=std::make_shared<DataBlockContainerFromMemoryHandler>(mh);
        }
        catch (...) {
        }
        if (copyBlock==nullptr) {
          printf("error: no page left\n");
          return;
        }
        auto blockRef=new DataBlockContainerReference(copyBlock);
        char *newBlock=(char *)copyBlock->getData()->data;;
        int newIx=0;
        for (auto &f : pendingFrames) {
          auto br=*(f.blockRef);
          DataBlock *b=br->getData();
          int ix=f.HBstart;
          int l=f.HBlength;
          //printf("block %p @ %d : %d\n",b,ix,l);
          memcpy(&newBlock[newIx],&(b->data[ix]),l);
          //printf("release %p for %p\n",f.blockRef,br);
          delete f.blockRef;
          f.blockRef=nullptr;
          newIx+=l;
        }

        //std::unique_ptr<FairMQMessage> msgBody(transportFactory->CreateMessage((void *)newBlock, totalSize, cleanupCallbackForMalloc, (void *)(newBlock)));
        //sendingChannel->Send(msgBody);
  
        // create a fmq message
        std::unique_ptr<FairMQMessage> msgCopy(transportFactory->CreateMessage(memoryBuffer,(void *)newBlock, totalSize, (void *)(blockRef)));
        sendingChannel->Send(msgCopy);
  
        //printf("sent reallocated HB %d (originally %d blocks) = %d bytes\n",pendingFrames[0].HBid,nFrames,totalSize);
      }
      pendingFrames.clear();
    };
    
    
    
    for (auto &br : *bc) {
      DataBlock *b=br->getData();
      unsigned int HBstart=0;
      for (int offset=0;offset+cruBlockSize<=b->header.dataSize;offset+=cruBlockSize) {
        o2::Header::RAWDataHeader *rdh=(o2::Header::RAWDataHeader *)&b->data[offset];       
        //printf("CRU block %p = HB %d link %d @ %d\n",b,(int)rdh->heartbeatOrbit,(int)rdh->linkId,offset);
        if (rdh->heartbeatOrbit!=lastHBid) {
          //printf("new HBf detected\n");
          int HBlength=offset-HBstart;
          
          if (HBlength) {
            // add previous block to pending frames
            pendingFramesAppend(HBstart,HBlength,lastHBid,br);
          }
          // send pending frames, if any
          pendingFramesSend();

          // update new HB frame
          HBstart=offset;
          lastHBid=rdh->heartbeatOrbit;
        }
      }
      
      // keep last piece for later, HBframe may continue in next block(s)
      if (HBstart<b->header.dataSize) {
        pendingFramesAppend(HBstart,b->header.dataSize-HBstart,lastHBid,br);
      }
    }
    
    // purge pendingFrames
    pendingFramesSend();
    
    /*

      std::unique_ptr<FairMQMessage> msgBody(transportFactory->CreateMessage((void *)(&b->data[HBstart]), (size_t)(HBlength), cleanupCallback, (void *)(ptr)));
      sendingChannel->Send(msgBody);
      printf("HB %d link %d @ %d + %d\n",lastHBid,(int)rdh->linkId,HBstart,HBlength);
      printf("(last part) sent %d bytes\n",(int)HBlength);
*/
      
      // todo: wait until next block to check if HBF continues on top... if so, realloc into a single block
          
    
    
/*    // we create a copy of the reference, in a newly allocated object, so that reference is kept alive until this new object is destroyed in the cleanupCallback
    DataBlockContainerReference *ptr=new DataBlockContainerReference(br);
    //std::unique_ptr<FairMQMessage> msgHeader(transportFactory->CreateMessage((void *)&(b->getData()->header), (size_t)(b->getData()->header.headerSize), cleanupCallback, (void *)nullptr));
    std::unique_ptr<FairMQMessage> msgBody(transportFactory->CreateMessage((void *)(br->getData()->data), (size_t)(br->getData()->header.dataSize), cleanupCallback, (void *)(ptr)));
    //sendingChannel->Send(msgHeader);
    sendingChannel->Send(msgBody);    
    return 0;
    */
    return 0;
  }
  
  private:
};



std::unique_ptr<Consumer> getUniqueConsumerFMQchannel(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ConsumerFMQchannel>(cfg, cfgEntryPoint);
}



#endif


