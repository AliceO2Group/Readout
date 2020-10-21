#include <atomic>
#include <memory>
#include <string>
#include <thread>

class ZmqServer {
public:
  ZmqServer() { init(); };
  ZmqServer(const std::string &url) {
    cfgAddress = url;
    init();
  };

  ~ZmqServer();
  int publish(void *msgBody, int msgSize);

private:
  void init();

  std::string cfgAddress = "tcp://127.0.0.1:50001";

  void *context = nullptr;
  void *zh = nullptr;

  std::unique_ptr<std::thread> th;  // thread receiving data
  std::atomic<int> shutdownRequest; // flag to be set to 1 to stop thread
  void run();                       // code executed in separate thread
};
