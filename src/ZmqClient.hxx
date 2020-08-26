#include <string>
#include <memory>
#include <thread>
#include <atomic>

class ZmqClient {
public:
  ZmqClient();
  ~ZmqClient();
  int publish(void *msgBody, int msgSize);
private:
  std::string cfgAddress="tcp://127.0.0.1:50001";
  
  void *context=nullptr;
  void *zh=nullptr;
  
  std::unique_ptr<std::thread> th;  // thread receiving data
  std::atomic<int> shutdownRequest; // flag to be set to 1 to stop thread
  void run(); // code executed in separate thread

};
