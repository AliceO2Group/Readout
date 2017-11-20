#include <stdint.h>

typedef uint64_t CounterValue;
 
class CounterStats {
  public:
  CounterStats();
  
  // do not mix set() and increment()
  
  void set(CounterValue value);         // assign new value
  void increment(CounterValue value=1); // increment new value
  void reset(); // restore all stats to zero
  
  CounterValue get(); // get latest value
  CounterValue getTotal();  // get total of previous values set
  double getAverage();
  CounterValue getMinimum();
  CounterValue getMaximum();
  CounterValue getCount();
  
  private:
  CounterValue value; // last value set

  // derived statistics
  CounterValue sum; // sum of previous values set (to compute average)
  CounterValue nValues; // number of time value was set (to compute average)
  CounterValue min; // minimum value set
  CounterValue max; // maximum value set
  
  void updateStats(); // update statistics from latest value
};
