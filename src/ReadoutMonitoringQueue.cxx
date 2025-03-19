#include "ReadoutMonitoringQueue.h"

ReadoutMonitoringQueue::ReadoutMonitoringQueue() {
}

ReadoutMonitoringQueue::~ReadoutMonitoringQueue() {
}
  
void ReadoutMonitoringQueue::push(ReadoutMonitoringMetric m) {
  std::unique_lock<std::mutex> lock(qMutex);
  q.push_front(std::move(m));
}

void ReadoutMonitoringQueue::execute(std::function<void(const ReadoutMonitoringMetric &)> f) {
  for (;;) {
    ReadoutMonitoringMetric m;
    
    {
      std::unique_lock<std::mutex> lock(qMutex);
      if (q.empty()) {
	break;
      }
      m = std::move(q.back());
      q.pop_back();
    }
    
    f(m);
  }
} 
  
void ReadoutMonitoringQueue::clear() {
  std::unique_lock<std::mutex> lock(qMutex);
  q.clear();
}

ReadoutMonitoringQueue gReadoutMonitoringQueue;
