#include <deque>
#include <mutex>
#include <string>
#include <functional>

// a metric to be stored in queue for later processing
// fields as for o2::monitoring::metric
struct ReadoutMonitoringMetric {
  std::string name;
  unsigned short int tag;
  uint64_t value;
};


// producer-consumer queue to define and publish metrics
// typical use:
// the module that pushes has no access to o2 Monitoring
// the module that publishes reads from the queue and publish them to o2 Monitoring
// the class is not aware of o2::monitoring, it's just a transient thread-safe storage

class ReadoutMonitoringQueue {
  public:
  
  ReadoutMonitoringQueue();
  ~ReadoutMonitoringQueue();
  
  // push an element in the queue
  void push(ReadoutMonitoringMetric);
  
  // execute provided functions on all elements in the queue
  // (and remove them from the queue)
  void execute(std::function<void(const ReadoutMonitoringMetric &)>); 
  
  // remove all elements in queue
  void clear();
  
  private:
  std::mutex qMutex;
  std::deque<ReadoutMonitoringMetric> q;
};

extern ReadoutMonitoringQueue gReadoutMonitoringQueue;
