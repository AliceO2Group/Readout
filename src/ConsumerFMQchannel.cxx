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
//#include "MemoryHandler.h"
#include "MemoryBank.h"
#include "MemoryBankManager.h"
#include "MemoryPagesPool.h"
#include "ReadoutStats.h"

#ifdef WITH_FAIRMQ

#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQMessage.h>
#include <fairmq/FairMQTransportFactory.h>

#include "RAWDataHeader.h"
#include "SubTimeframe.h"

/*
namespace {

// cleanup function, defined with the callback footprint expected in the 3rd
argument of FairMQTransportFactory.CreateMessage()
// when object not null, it should be a (DataBlockContainerReference *), which
will be destroyed void cleanupCallback(void *data, void *object) { if
((object!=nullptr)&&(data!=nullptr)) { DataBlockContainerReference
*ptr=(DataBlockContainerReference *)object;
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

class ConsumerFMQchannel : public Consumer {
private:
  std::unique_ptr<FairMQChannel> sendingChannel;
  std::shared_ptr<FairMQTransportFactory> transportFactory;
  FairMQUnmanagedRegionPtr memoryBuffer = nullptr;
  bool disableSending = 0;
  bool enableRawFormat = false;

  std::shared_ptr<MemoryBank>
      memBank; // a dedicated memory bank allocated by FMQ mechanism
  std::shared_ptr<MemoryPagesPool>
      mp; // a memory pool from which to allocate data pages

  int memoryPoolPageSize;
  int memoryPoolNumberOfPages;

public:
  std::vector<FairMQMessagePtr>
      messagesToSend; // collect HBF messages of each update
  uint64_t messagesToSendSize; // size (bytes) of messagesToSend payload

  ConsumerFMQchannel(ConfigFile &cfg, std::string cfgEntryPoint)
      : Consumer(cfg, cfgEntryPoint) {

    // configuration parameter: | consumer-FairMQchannel-* | disableSending | int |
    // 0 | If set, no data is output to FMQ channel. Used for performance test
    // to create FMQ shared memory segment without pushing the data. |
    int cfgDisableSending = 0;
    cfg.getOptionalValue<int>(cfgEntryPoint + ".disableSending",
                              cfgDisableSending);
    if (cfgDisableSending) {
      theLog.log("FMQ message sending disabled");
      disableSending = true;
    }

    // configuration parameter: | consumer-FairMQchannel-* | enableRawFormat | int
    // | 0 | If set, data is pushed in raw format without additional headers, 1
    // FMQ message per data page. |
    int cfgEnableRawFormat = 0;
    cfg.getOptionalValue<int>(cfgEntryPoint + ".enableRawFormat",
                              cfgEnableRawFormat);
    if (cfgEnableRawFormat) {
      theLog.log("FMQ message output in raw format: 1 message per data page");
      enableRawFormat = true;
    }

    // configuration parameter: | consumer-FairMQchannel-* | sessionName | string |
    // default | Name of the FMQ session. c.f. FairMQ::FairMQChannel.h |
    std::string cfgSessionName = "default";
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".sessionName",
                                      cfgSessionName);

    // configuration parameter: | consumer-FairMQchannel-* | fmq-transport | string
    // | shmem | Name of the FMQ transport. Typically: zeromq or shmem. c.f.
    // FairMQ::FairMQChannel.h |
    std::string cfgTransportType = "shmem";
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".fmq-transport",
                                      cfgTransportType);

    // configuration parameter: | consumer-FairMQchannel-* | fmq-name | string |
    // readout | Name of the FMQ channel. c.f. FairMQ::FairMQChannel.h |
    std::string cfgChannelName = "readout";
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".fmq-name",
                                      cfgChannelName);

    // configuration parameter: | consumer-FairMQchannel-* | fmq-type | string |
    // pair | Type of the FMQ channel. Typically: pair. c.f.
    // FairMQ::FairMQChannel.h |
    std::string cfgChannelType = "pair";
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".fmq-type",
                                      cfgChannelType);

    // configuration parameter: | consumer-FairMQchannel-* | fmq-address |
    // string | ipc:///tmp/pipe-readout | Address of the FMQ channel. Depends on
    // transportType. c.f. FairMQ::FairMQChannel.h |
    std::string cfgChannelAddress = "ipc:///tmp/pipe-readout";
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".fmq-address",
                                      cfgChannelAddress);

    theLog.log("Creating FMQ (session %s) TX channel %s type %s:%s @ %s",
               cfgSessionName.c_str(), cfgChannelName.c_str(),
               cfgTransportType.c_str(), cfgChannelType.c_str(),
               cfgChannelAddress.c_str());

    FairMQProgOptions fmqOptions;
    fmqOptions.SetValue<std::string>("session", cfgSessionName);

    // configuration parameter: | consumer-FairMQchannel-* | fmq-progOptions |
    // string |  | Additional FMQ program options parameters, as a
    // comma-separated list of key=value pairs. |
    std::string cfgFmqOptions = "";
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".fmq-progOptions",
                                      cfgFmqOptions);
    std::map<std::string, std::string> mapOptions;
    if (getKeyValuePairsFromString(cfgFmqOptions, mapOptions)) {
      throw("Can not parse configuration item fmqProgOptions");
    }
    for (auto &it : mapOptions) {
      fmqOptions.SetValue<std::string>(it.first, it.second);
      theLog.log("Setting FMQ option %s = %s", it.first.c_str(),
                 it.second.c_str());
    }

    transportFactory = FairMQTransportFactory::CreateTransportFactory(
        cfgTransportType, fair::mq::tools::Uuid(), &fmqOptions);
    sendingChannel = std::make_unique<FairMQChannel>(
        cfgChannelName, cfgChannelType, transportFactory);

    // configuration parameter: | consumer-FairMQchannel-* | memoryBankName |
    // string |  | Name of the memory bank to crete (if any) and use. This
    // consumer has the special property of being able to provide memory banks
    // to readout, as the ones defined in bank-*. It creates a memory region
    // optimized for selected transport and to be used for readout device DMA. |
    std::string memoryBankName =
        ""; // name of memory bank to create (if any) and use.
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".memoryBankName",
                                      memoryBankName);

    // configuration parameter: | consumer-FairMQchannel-* | unmanagedMemorySize |
    // bytes |  | Size of the memory region to be created. c.f.
    // FairMQ::FairMQUnmanagedRegion.h. If not set, no special FMQ memory region
    // is created. |
    std::string cfgUnmanagedMemorySize = "";
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".unmanagedMemorySize",
                                      cfgUnmanagedMemorySize);
    long long mMemorySize = ReadoutUtils::getNumberOfBytesFromString(
        cfgUnmanagedMemorySize.c_str());
    if (mMemorySize > 0) {
      memoryBuffer = sendingChannel->Transport()->CreateUnmanagedRegion(
          mMemorySize,
          [this](void *data, size_t size, void *hint) { // cleanup callback
            // printf("ack %p (size %d) hint=%p\n",data,(int)size,hint);

            if (hint != nullptr) {
              DataBlockContainerReference *blockRef =
                  (DataBlockContainerReference *)hint;
              delete blockRef;
            }
          });

      theLog.log("Got FMQ unmanaged memory buffer size %lu @ %p",
                 memoryBuffer->GetSize(), memoryBuffer->GetData());
      memBank = std::make_shared<MemoryBank>(
          memoryBuffer->GetData(), memoryBuffer->GetSize(), nullptr,
          "FMQ unmanaged memory buffer from " + cfgEntryPoint);
      if (memoryBankName.length() == 0) {
        memoryBankName = cfgEntryPoint; // if no bank name defined, create one
                                        // with the name of the consumer
      }
      theMemoryBankManager.addBank(memBank, memoryBankName);
      theLog.log("Bank %s added", memoryBankName.c_str());
    }

    // allocate a pool of pages for headers and data frame copies
    // configuration parameter: | consumer-FairMQchannel-* | memoryPoolPageSize |
    // bytes | 128k | c.f. same parameter in bank-*. | configuration parameter:
    // | consumer-FairMQchannel-* | memoryPoolNumberOfPages | int | 100 | c.f. same
    // parameter in bank-*. |
    memoryPoolPageSize = 0;
    memoryPoolNumberOfPages = 100;
    std::string cfgMemoryPoolPageSize = "128k";
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".memoryPoolPageSize",
                                      cfgMemoryPoolPageSize);
    memoryPoolPageSize = (int)ReadoutUtils::getNumberOfBytesFromString(
        cfgMemoryPoolPageSize.c_str());
    cfg.getOptionalValue<int>(cfgEntryPoint + ".memoryPoolNumberOfPages",
                              memoryPoolNumberOfPages);
    mp = theMemoryBankManager.getPagedPool(
        memoryPoolPageSize, memoryPoolNumberOfPages, memoryBankName);
    if (mp == nullptr) {
      throw "ConsumerFMQ: failed to get memory pool from " + memoryBankName +
          " for " + std::to_string(memoryPoolNumberOfPages) + " pages x " +
          std::to_string(memoryPoolPageSize) + " bytes";
    }
    theLog.log("Using memory pool %d pages x %d bytes", memoryPoolNumberOfPages,
               memoryPoolPageSize);

    sendingChannel->Bind(cfgChannelAddress);

    if (!sendingChannel->Validate()) {
      throw "ConsumerFMQ: channel validation failed";
    }
  }

  ~ConsumerFMQchannel() {
    // release in reverse order
    mp = nullptr;
    memoryBuffer = nullptr; // warning: data range may still be referenced in
                            // memory bank manager
    sendingChannel = nullptr;
    transportFactory = nullptr;
  }

  int pushData(DataBlockContainerReference &) {
    // this consumer does not accept a per-block push, it needs a set
    return -1;
  }

  int pushData(DataSetReference &bc) {

    if (disableSending) {
      return 0;
    }

    // debug mode to send in simple raw format: 1 FMQ message per data page
    if (enableRawFormat) {
      // we just ship one FMQmessage per incoming data page
      for (auto &br : *bc) {
        DataBlock *b = br->getData();
        DataBlockContainerReference *blockRef =
            new DataBlockContainerReference(br);
        void *hint = (void *)blockRef;
        void *blobPtr = b->data;
        size_t blobSize = (size_t)b->header.dataSize;
        std::unique_ptr<FairMQMessage> msgBody(transportFactory->CreateMessage(
            memoryBuffer, blobPtr, blobSize, hint));
        // printf("send %p = %d bytes hint=%p\n",blobPtr,(int)blobSize,hint);
        sendingChannel->Send(msgBody);
        gReadoutStats.bytesFairMQ += blobSize;
      }
      return 0;
    }

    // send msg with WP5 format: 1 FMQ message for header + 1 FMQ message per
    // data page

    const unsigned int cruBlockSize = 8192;
    // todo: replace by RDHv3 length

    // we iterate a first time to count number of HB
    if (memoryPoolPageSize < (int)sizeof(SubTimeframe)) {
      return -1;
    }
    DataBlockContainerReference headerBlock = nullptr;
    try {
      headerBlock = mp->getNewDataBlockContainer();
    } catch (...) {
    }
    if (headerBlock == nullptr) {
      return -1;
    }
    auto blockRef = new DataBlockContainerReference(headerBlock);
    SubTimeframe *stfHeader = (SubTimeframe *)headerBlock->getData()->data;
    if (stfHeader == nullptr) {
      return -1;
    }
    stfHeader->timeframeId = 0;
    stfHeader->numberOfHBF = 0;
    stfHeader->linkId = undefinedLinkId;

    unsigned int lastHBid = -1;
    int isFirst = true;
    int ix = 0;
    for (auto &br : *bc) {
      ix++;
      DataBlock *b = br->getData();
      if (isFirst) {
        stfHeader->timeframeId = b->header.timeframeId;
        stfHeader->linkId = b->header.linkId;
        stfHeader->numberOfHBF = 0;
        isFirst = false;
      } else {
        if (stfHeader->timeframeId != b->header.timeframeId) {
          printf("mismatch tfId\n");
        }
        if (stfHeader->linkId != b->header.linkId) {
          printf("mismatch linkId\n");
        }
      }
      // printf("block %d tf %d link
      // %d\n",ix,b->header.timeframeId,b->header.linkId);
      for (int offset = 0; offset + cruBlockSize <= b->header.dataSize;
           offset += cruBlockSize) {
        // printf("checking %p : %d\n",b,offset);
        o2::Header::RAWDataHeader *rdh =
            (o2::Header::RAWDataHeader *)&b->data[offset];
        if (rdh->heartbeatOrbit != lastHBid) {
          stfHeader->numberOfHBF++;
          lastHBid = rdh->heartbeatOrbit;
          // printf("offset %d now %d HBF -
          // HBid=%d\n",offset,stfHeader->numberOfHBF,lastHBid);
        }
        if (stfHeader->linkId != rdh->linkId) {
          printf("Warning: TF%d link Id mismatch %d != %d\n",
                 (int)stfHeader->timeframeId, (int)stfHeader->linkId,
                 (int)rdh->linkId);
          // dumpRDH(rdh);
          // printf("block %p : offset %d = %p\n",b,offset,rdh);
        }
      }
    }

    // printf("TF %d link %d = %d blocks, %d
    // HBf\n",(int)stfHeader->timeframeId,(int)stfHeader->linkId,(int)bc->size(),(int)stfHeader->numberOfHBF);

    // create a header message
    // std::unique_ptr<FairMQMessage>
    // msgHeader(transportFactory->CreateMessage((void *)stfHeader,
    // sizeof(SubTimeframe), cleanupCallback, (void *)(blockRef)));
    assert(messagesToSend.empty());
    messagesToSend.emplace_back(std::move(
        sendingChannel->NewMessage(memoryBuffer, (void *)stfHeader,
                                   sizeof(SubTimeframe), (void *)(blockRef))));
    messagesToSendSize = sizeof(SubTimeframe);
    // printf("sent header %d bytes\n",(int)sizeof(SubTimeframe));

    // cut: one message per HBf
    lastHBid = -1;

    // this is for data not sent yet (from one loop to the next)
    struct pendingFrame {
      DataBlockContainerReference *blockRef;
      unsigned int HBstart;
      unsigned int HBlength;
      unsigned int HBid;
    };
    std::vector<pendingFrame> pendingFrames;

    auto pendingFramesAppend = [&](unsigned int ix, unsigned int l,
                                   unsigned int id,
                                   DataBlockContainerReference br) {
      pendingFrame pf;
      pf.HBstart = ix;
      pf.HBlength = l;
      pf.HBid = id;
      // we create a copy of the reference, in a newly allocated object, so that
      // reference is kept alive until this new object is destroyed in the
      // cleanupCallback
      pf.blockRef = new DataBlockContainerReference(br);
      // printf("allocating blockRef %p for %p\n",pf.blockRef,br);
      pendingFrames.push_back(pf);
    };

    auto pendingFramesCollect = [&]() {
      int nFrames = pendingFrames.size();

      if (nFrames == 0) {
        // printf("no pending frames\n");
        return;
      }

      if (nFrames == 1) {
        // single block, no need to repack
        auto br = *(pendingFrames[0].blockRef);
        DataBlock *b = br->getData();
        int ix = pendingFrames[0].HBstart;
        int l = pendingFrames[0].HBlength;
        //        std::unique_ptr<FairMQMessage>
        //        msgBody(transportFactory->CreateMessage((void
        //        *)(&(b->data[ix])),(size_t)(l), cleanupCallback, (void
        //        *)(pendingFrames[0].blockRef)));
        void *hint = (void *)pendingFrames[0].blockRef;
        // printf("block %p ix = %d : %d hint=%p\n",(void
        // *)(&(b->data[ix])),ix,l,hint); std::cout <<
        // typeid(pendingFrames[0].blockRef).name() << std::endl;

        // create and queue a fmq message
        messagesToSend.emplace_back(std::move(sendingChannel->NewMessage(
            memoryBuffer, (void *)(&(b->data[ix])), (size_t)(l), hint)));
        messagesToSendSize += l;
        // printf("sent single HB %d = %d bytes\n",pendingFrames[0].HBid,l);
        // printf("left to FMQ: %p\n",pendingFrames[0].blockRef);

      } else {
        // todo : account number of repack-copies in this situation

        // multiple blocks, need to repack
        int totalSize = 0;
        for (auto &f : pendingFrames) {
          totalSize += f.HBlength;
        }
        // allocate
        // todo: same code as for header -> create func/lambda
        // todo: send empty message if no page left in buffer
        if (memoryPoolPageSize < totalSize) {
          printf("error page size too small %d < %d\n", memoryPoolPageSize,
                 totalSize);
          return;
        }
        DataBlockContainerReference copyBlock = nullptr;
        try {
          copyBlock = mp->getNewDataBlockContainer();
        } catch (...) {
        }
        if (copyBlock == nullptr) {
          printf("error: no page left\n");
          return;
        }
        auto blockRef = new DataBlockContainerReference(copyBlock);
        char *newBlock = (char *)copyBlock->getData()->data;
        ;
        int newIx = 0;
        for (auto &f : pendingFrames) {
          auto br = *(f.blockRef);
          DataBlock *b = br->getData();
          int ix = f.HBstart;
          int l = f.HBlength;
          // printf("block %p @ %d : %d\n",b,ix,l);
          memcpy(&newBlock[newIx], &(b->data[ix]), l);
          // printf("release %p for %p\n",f.blockRef,br);
          delete f.blockRef;
          f.blockRef = nullptr;
          newIx += l;
        }

        // std::unique_ptr<FairMQMessage>
        // msgBody(transportFactory->CreateMessage((void *)newBlock, totalSize,
        // cleanupCallbackForMalloc, (void *)(newBlock)));
        // sendingChannel->Send(msgBody);

        // create and queue a fmq message
        messagesToSend.emplace_back(std::move(sendingChannel->NewMessage(
            memoryBuffer, (void *)newBlock, totalSize, (void *)(blockRef))));
        messagesToSendSize += totalSize;
        
        // printf("sent reallocated HB %d (originally %d blocks) = %d
        // bytes\n",pendingFrames[0].HBid,nFrames,totalSize);
      }
      pendingFrames.clear();
    };

    for (auto &br : *bc) {
      DataBlock *b = br->getData();
      unsigned int HBstart = 0;
      for (int offset = 0; offset + cruBlockSize <= b->header.dataSize;
           offset += cruBlockSize) {
        o2::Header::RAWDataHeader *rdh =
            (o2::Header::RAWDataHeader *)&b->data[offset];
        // printf("CRU block %p = HB %d link %d @
        // %d\n",b,(int)rdh->heartbeatOrbit,(int)rdh->linkId,offset);
        if (rdh->heartbeatOrbit != lastHBid) {
          // printf("new HBf detected\n");
          int HBlength = offset - HBstart;

          if (HBlength) {
            // add previous block to pending frames
            pendingFramesAppend(HBstart, HBlength, lastHBid, br);
          }
          // send pending frames, if any
          pendingFramesCollect();

          // update new HB frame
          HBstart = offset;
          lastHBid = rdh->heartbeatOrbit;
        }
      }

      // keep last piece for later, HBframe may continue in next block(s)
      if (HBstart < b->header.dataSize) {
        pendingFramesAppend(HBstart, b->header.dataSize - HBstart, lastHBid,
                            br);
      }
    }

    // purge pendingFrames
    pendingFramesCollect();

    // send all the messages
    if (sendingChannel->Send(messagesToSend) >= 0) {
      messagesToSend.clear();
      gReadoutStats.bytesFairMQ += messagesToSendSize;
      messagesToSendSize = 0;
    } else {
      LOG(ERROR) << "Sending failed!";
    }

    /*

      std::unique_ptr<FairMQMessage>
      msgBody(transportFactory->CreateMessage((void *)(&b->data[HBstart]),
      (size_t)(HBlength), cleanupCallback, (void *)(ptr)));
      sendingChannel->Send(msgBody);
      printf("HB %d link %d @ %d +
      %d\n",lastHBid,(int)rdh->linkId,HBstart,HBlength); printf("(last part)
      sent %d bytes\n",(int)HBlength);
*/

    // todo: wait until next block to check if HBF continues on top... if so,
    // realloc into a single block

    /*    // we create a copy of the reference, in a newly allocated object, so
       that reference is kept alive until this new object is destroyed in the
       cleanupCallback DataBlockContainerReference *ptr=new
       DataBlockContainerReference(br);
        //std::unique_ptr<FairMQMessage>
       msgHeader(transportFactory->CreateMessage((void
       *)&(b->getData()->header), (size_t)(b->getData()->header.headerSize),
       cleanupCallback, (void *)nullptr)); std::unique_ptr<FairMQMessage>
       msgBody(transportFactory->CreateMessage((void *)(br->getData()->data),
       (size_t)(br->getData()->header.dataSize), cleanupCallback, (void
       *)(ptr)));
        //sendingChannel->Send(msgHeader);
        sendingChannel->Send(msgBody);
        return 0;
        */
    return 0;
  }

private:
};

std::unique_ptr<Consumer>
getUniqueConsumerFMQchannel(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ConsumerFMQchannel>(cfg, cfgEntryPoint);
}

#endif
