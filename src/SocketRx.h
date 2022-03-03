#include <thread>
#include <memory>
#include <atomic>
#include <string>
#include <vector>
#include <mutex>

#include <Common/Timer.h>
#include <InfoLogger/InfoLogger.hxx>


struct SocketRxClient {
  int fd;
  std::string name;
  AliceO2::Common::Timer t;
  unsigned long long bytesRx;
  unsigned long long bytesTx;
};

class SocketRx {
  public:
  enum Type {TCP, UDP};
  
  SocketRx(const std::string name, int port, AliceO2::InfoLogger::InfoLogger *theLog=nullptr, Type st=Type::TCP);
  ~SocketRx();
  
  // send a message to all connected clients
  // message: NUL-terminated string
  void broadcast(const char *message);
  AliceO2::InfoLogger::InfoLogger *theLog = nullptr;
      
  private:
  std::unique_ptr<std::thread> th;
  std::atomic<int> shutdownRequest;

  int portNumber=0;

  std::string serverName;
  
  Type socketType;
  std::mutex clientsLock;
  std::vector<SocketRxClient> clients;
  void closeClient(SocketRxClient &c);

  void run();
};
