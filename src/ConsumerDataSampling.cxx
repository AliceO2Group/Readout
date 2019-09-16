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

#ifdef WITH_FAIRMQ

#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQMessage.h>
#include <fairmq/FairMQParts.h>
#include <fairmq/FairMQTransportFactory.h>
#include <thread>

class ConsumerDataSampling : public Consumer {

  class FMQSender : public FairMQDevice {
  public:
    FMQSender() {}
    ~FMQSender() {}

  protected:
    void Run() override {
      while (!NewStatePending()) {
        usleep(200000);
      }
    }
  };

  // cleanup function, defined with the callback footprint expected in the 3rd
  // argument of FairMQTransportFactory.CreateMessage() when object not null, it
  // should be a (DataBlockContainerReference *), which will be destroyed
  static void cleanupCallback(void *data, void *object) {
    if ((object != nullptr) && (data != nullptr)) {
      DataBlockContainerReference *ptr = (DataBlockContainerReference *)object;
      //      printf("ptr %p: use_count=%d\n",ptr,ptr->use_count());
      delete ptr;
    }
  }

private:
  std::vector<FairMQChannel> channels;
  FMQSender sender;
  std::thread deviceThread;

  // todo: check why this type is not public in FMQ interface?
  typedef std::unordered_map<std::string, std::vector<FairMQChannel>> FairMQMap;
  FairMQMap m;

  std::shared_ptr<FairMQTransportFactory> transportFactory;

public:
  ConsumerDataSampling(ConfigFile &cfg, std::string cfgEntryPoint)
      : Consumer(cfg, cfgEntryPoint), channels(1) {
    std::string address;
    // configuration parameter: | consumer-data-sampling-* | address | string |
    // ipc:///tmp/readout-pipe-1 | Address of the data sampling. |
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".address", address,
                                      "ipc:///tmp/readout-pipe-1");
    channels[0].UpdateName("data-out");
    channels[0].UpdateType("pub"); // pub or push?
    channels[0].UpdateMethod("bind");
    channels[0].UpdateAddress(address);
    channels[0].UpdateRateLogging(0);
    channels[0].UpdateSndBufSize(10);
    if (!channels[0].Validate()) {
      throw "ConsumerDataSampling: channel validation failed";
    }

    // todo: def "data-out" as const string to name output channel to which we
    // will push
    m.emplace(std::string("data-out"), channels);

    for (auto it : m) {
      std::cout << it.first << " = " << it.second.size() << " channels  "
                << std::endl;
      for (auto ch : it.second) {
        std::cout << ch.GetAddress() << std::endl;
      }
    }

    transportFactory = FairMQTransportFactory::CreateTransportFactory("zeromq");

    deviceThread = std::thread(&ConsumerDataSampling::runDevice, this);

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

  ~ConsumerDataSampling() {
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

    // we create a copy of the reference, in a newly allocated object, so that
    // reference is kept alive until this new object is destroyed in the
    // cleanupCallback
    DataBlockContainerReference *ptr = new DataBlockContainerReference(b);

    if (sender.GetCurrentState() != fair::mq::State::Running) {
      LOG(ERROR) << "ConsumerDataSampling: Trying to send data when the device "
                    "is not in RUN state";
      return -1;
    }

    std::unique_ptr<FairMQMessage> msgHeader(transportFactory->CreateMessage(
        (void *)&(b->getData()->header),
        (size_t)(b->getData()->header.headerSize), cleanupCallback,
        (void *)nullptr));
    std::unique_ptr<FairMQMessage> msgBody(transportFactory->CreateMessage(
        (void *)(b->getData()->data), (size_t)(b->getData()->header.dataSize),
        cleanupCallback, (void *)(ptr)));

    FairMQParts message;
    message.AddPart(std::move(msgHeader));
    message.AddPart(std::move(msgBody));

    sender.fChannels.at("data-out").at(0).Send(message);

    return 0;
  }

private:
  void runDevice() { sender.RunStateMachine(); }
};

std::unique_ptr<Consumer>
getUniqueConsumerDataSampling(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ConsumerDataSampling>(cfg, cfgEntryPoint);
}

#endif
