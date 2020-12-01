#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

class ZmqClient
{
 public:
  ZmqClient(const std::string& url = "tcp://127.0.0.1:50001", const int maxMsgSize = 1024L * 1024L);
  ~ZmqClient();

  int setCallback(std::function<int(void* msg, int msgSize)>);
  void setPause(int); // to pause/unpause data RX

 private:
  std::string cfgAddress;
  int cfgMaxMsgSize;

  void* context = nullptr;
  void* zh = nullptr;
  void* msgBuffer = nullptr;

  bool isPaused = false;

  std::unique_ptr<std::thread> th;  // thread receiving data
  std::atomic<int> shutdownRequest; // flag to be set to 1 to stop thread
  void run();                       // code executed in separate thread
  std::function<int(void* msg, int msgSize)> callback = nullptr;
};
