#include "Consumer.h"


#ifdef WITH_FAIRMQ

#include <thread>
#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQMessage.h>
#include <fairmq/FairMQTransportFactory.h>


class FMQSender : public FairMQDevice
{
  public:

    FMQSender() { }
    ~FMQSender() { }

  protected:

    void Run() override {
       while (CheckCurrentState(RUNNING)) {
         //printf("loop Run()\n");
         usleep(200000);
       }
    }
};


// cleanup function, defined with the callback footprint expected in the 3rd argument of FairMQTransportFactory.CreateMessage()
// when object not null, it should be a (DataBlockContainerReference *), which will be destroyed
void cleanupCallback(void *data, void *object) {
    if ((object!=nullptr)&&(data!=nullptr)) {
      DataBlockContainerReference *ptr=(DataBlockContainerReference *)object;
//      printf("ptr %p: use_count=%d\n",ptr,ptr->use_count());
      delete ptr;
    }
  }



class ConsumerFMQ: public Consumer {
  private:
    std::vector<FairMQChannel> channels;
    FMQSender sender;


 // todo: check why this type is not public in FMQ interface?
    typedef std::unordered_map<std::string, std::vector<FairMQChannel>> FairMQMap;
    FairMQMap m;

    std::shared_ptr<FairMQTransportFactory> transportFactory;
    std::thread deviceThread;

  public:

  ConsumerFMQ(ConfigFile &cfg, std::string cfgEntryPoint) : Consumer(cfg,cfgEntryPoint), channels(1) {

    channels[0].UpdateType("pair");  // pub or push?
    channels[0].UpdateMethod("bind");
    channels[0].UpdateAddress("ipc:///tmp/readout-pipe-0");
    channels[0].UpdateRateLogging(0);
    channels[0].UpdateSndBufSize(10);
    if (!channels[0].ValidateChannel()) {
      throw "ConsumerFMQ: channel validation failed";
    }


    // todo: def "data-out" as const string to name output channel to which we will push
    m.emplace(std::string("data-out"),channels);

    for (auto it : m) {
      std::cout << it.first << " = " << it.second.size() << " channels  " << std::endl;
      for (auto ch : it.second) {
        std::cout << ch.GetAddress() <<std::endl;
      }
    }

    transportFactory = FairMQTransportFactory::CreateTransportFactory("zeromq");

    deviceThread = std::thread(&ConsumerFMQ::runDevice, this);

    sender.fChannels = m;
    sender.SetTransport("zeromq");
    sender.ChangeState(fair::mq::Transition::InitDevice);
    sender.WaitForState(fair::mq::State::InitializingDevice);
    sender.ChangeState(fair::mq::Transition::CompleteInit);
    sender.WaitForState(fair::mq::State::Initialized);
    sender.ChangeState(fair::mq::Transition::Bind);
    sender.WaitForState(fair::mq::State::Bound);
    sender.ChangeState(fair::mq::Transition::Connect);
    sender.WaitForState(fair::mq::State::DeviceReady);
    sender.ChangeState(fair::mq::Transition::InitTask);
    sender.WaitForState(fair::mq::State::Ready);
    sender.ChangeState(fair::mq::Transition::Run);

//    sender.InteractiveStateLoop();
  }

  ~ConsumerFMQ() {
    sender.ChangeState(fair::mq::Transition::Stop);
    sender.WaitForState(fair::mq::State::Ready);
    sender.ChangeState(fair::mq::Transition::ResetTask);
    sender.WaitForState(fair::mq::State::DeviceReady);
    sender.ChangeState(fair::mq::Transition::ResetDevice);
    sender.WaitForState(fair::mq::State::Idle);
    sender.ChangeState(fair::mq::Transition::End);

    if (deviceThread.joinable()) {
      deviceThread.join();
    }
  }

  int pushData(DataBlockContainerReference &b) {

    // we create a copy of the reference, in a newly allocated object, so that reference is kept alive until this new object is destroyed in the cleanupCallback
    DataBlockContainerReference *ptr=new DataBlockContainerReference(b);
    std::unique_ptr<FairMQMessage> msgHeader(transportFactory->CreateMessage((void *)&(b->getData()->header), (size_t)(b->getData()->header.headerSize), cleanupCallback, (void *)nullptr));
    std::unique_ptr<FairMQMessage> msgBody(transportFactory->CreateMessage((void *)(b->getData()->data), (size_t)(b->getData()->header.dataSize), cleanupCallback, (void *)(ptr)));

    sender.fChannels.at("data-out").at(0).Send(msgHeader);
    sender.fChannels.at("data-out").at(0).Send(msgBody);



    // how to know if it was a success?

    // every time we do a push there is a string compare ???
    //channels[0].SendPart(msgHeader);
    //channels[0].Send(msgBody);

    // use multipart?
    // channels.at("data-out").at(0).SendPart(msgBody);

    return 0;
  }
  private:

  void runDevice() {
    sender.RunStateMachine();
  }
};



std::unique_ptr<Consumer> getUniqueConsumerFMQ(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ConsumerFMQ>(cfg, cfgEntryPoint);
}



#endif


