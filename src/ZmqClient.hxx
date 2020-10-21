#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

class ZmqClient {
public:
  ZmqClient(const std::string &url = "tcp://127.0.0.1:50001");
  ~ZmqClient();

  int setCallback(std::function<int(void *msg, int msgSize)>);

private:
  std::string cfgAddress;

  void *context = nullptr;
  void *zh = nullptr;

  std::unique_ptr<std::thread> th;  // thread receiving data
  std::atomic<int> shutdownRequest; // flag to be set to 1 to stop thread
  void run();                       // code executed in separate thread
  std::function<int(void *msg, int msgSize)> callback = nullptr;
};
