#ifdef WITH_FAIRMQ

#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQMessage.h>
#include <fairmq/FairMQTransportFactory.h>
#include <fairmq/zeromq/FairMQTransportFactoryZMQ.h>
#include <memory>

class FMQReceiver : public FairMQDevice
{
  public:

    FMQReceiver() { 
    
    transportFactory = new FairMQTransportFactoryZMQ();
    
    }
    ~FMQReceiver() { 
    
    delete transportFactory;
    }

  protected:   
    
     int msgCount=0;
     int msgBytes=0;
     FairMQTransportFactory *transportFactory;
    
    void Run() override {
       while (CheckCurrentState(RUNNING)) {


    std::unique_ptr<FairMQMessage> msg(transportFactory->CreateMessage());

    if (fChannels.at("data-in").at(0).Receive(msg) > 0) {
      msgBytes+=msg->GetSize();
      msgCount++;
      printf("%d messages, %d bytes\n",msgCount,msgBytes);
//      cout << "Received message: \""
//      << string(static_cast<char *>(msg->GetData()), msg->GetSize())
//      << "\"" << endl;
    }



         usleep(200000);
       }
    }
};

int main() {
   std::vector<FairMQChannel> channels(1);
   FMQReceiver fd;

// todo: check why this type is not public in FMQ interface?  
     typedef std::unordered_map<std::string, std::vector<FairMQChannel>> FairMQMap;   
    FairMQMap m;
    




       
    channels[0].UpdateType("sub");  // pub or push?
    channels[0].UpdateMethod("connect");
    channels[0].UpdateAddress("tcp://localhost:5555");
    channels[0].UpdateRateLogging(0);    
    channels[0].UpdateSndBufSize(10);    
    if (!channels[0].ValidateChannel()) {
      throw "ConsumerFMQ: channel validation failed";
    }


    // todo: def "data-out" as const string to name output channel to which we will push
    m.emplace(std::string("data-in"),channels);
    
    for (auto it : m) {
      std::cout << it.first << " = " << it.second.size() << " channels  " << std::endl;
      for (auto ch : it.second) {
        std::cout << ch.GetAddress() <<std::endl;
      }
    }
      
    fd.fChannels = m;
    fd.SetTransport("zeromq");
    fd.ChangeState(FairMQStateMachine::Event::INIT_DEVICE);
    fd.WaitForEndOfState(FairMQStateMachine::Event::INIT_DEVICE);
    fd.ChangeState(FairMQStateMachine::Event::INIT_TASK);
    fd.WaitForEndOfState(FairMQStateMachine::Event::INIT_TASK);
    fd.ChangeState(FairMQStateMachine::Event::RUN);

//    fd.InteractiveStateLoop();
  
  
  
    sleep(5);
  
  

    fd.ChangeState(FairMQStateMachine::Event::STOP);
    fd.ChangeState(FairMQStateMachine::Event::RESET_TASK);
    fd.WaitForEndOfState(FairMQStateMachine::Event::RESET_TASK);
    fd.ChangeState(FairMQStateMachine::Event::RESET_DEVICE);
    fd.WaitForEndOfState(FairMQStateMachine::Event::RESET_DEVICE);
    fd.ChangeState(FairMQStateMachine::Event::END);










  return 0;
}

#else
#include<stdio.h>
int main() {

  printf("Not compiled with FMQ, exiting\n");
  return 0;
}
#endif
