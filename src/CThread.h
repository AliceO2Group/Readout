#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <atomic>
#include <thread>

#include <InfoLogger/InfoLogger.hxx>

// a class to implement controllable looping threads
// user just need to overload the "doLoop()" method
// or to provide a callback function to the base class constructor


class CThread {
  public:
    enum CallbackResult {Ok,Idle,Done,Error};

    CThread(CThread::CallbackResult (*vLoopCallback)(void *) = NULL , void *vLoopArg = NULL, std::string vThreadName = "", int loopSleepTime=1000);
    ~CThread();
    
    void start(); // start thread loop
    void stop();  // request thread termination
    void join();  // wait thread terminates
    
    std::string getName();  // returns thread name
    
     
    
  private:
    std::atomic<int> shutdown;    // flag set to 1 to request thread termination
    std::atomic<int> running;     // flag set to 1 when thread running
    std::thread *theThread;

    std::string name;   // name of the thread, used in debug printouts
    int loopSleepTime;  // sleep time between 2 loop calls
    
    CallbackResult (*loopCallback)(void *);  // callback provided at create time
    void *loopArg;                // arg to be passed to callback function
   
    CallbackResult doLoop();   // function called at each thread iteration. Returns a result code.
    static void threadMain(CThread *e); // this is the (internal) thread entry point
    
    AliceO2::InfoLogger::InfoLogger theLog;
};
