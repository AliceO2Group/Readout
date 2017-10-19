// Utilities to handle RDH content from CRU data


#include <string>


// flat RDH definition
#define RDH_WORDS 16
typedef struct {
  union {
    uint32_t words[RDH_WORDS];
    uint64_t lwords[RDH_WORDS/2];
  };
} RDH;


// Utility class to access RDH fields and check them
class RdhHandle {
  public:
  // create a handle to RDH structure pointed by argument
  RdhHandle(void *data);

  // destructor
  ~RdhHandle();
  
  // check RDH content
  // returns 0 on success, number of errors found otherwise
  // Error message sets accordingly
  int validateRdh(std::string &err);
  
  // print RDH content 
  void dumpRdh();
 
  // access RDH fields
  // functions defined inline here
  inline uint8_t getHeaderVersion() {
    return rdhPtr->words[3] & 0xF;  
  } 
  inline uint16_t getBlockLength() {
    return (uint16_t) ((rdhPtr->words[3] >> 4 ) & 0xFFFF);
  }
  inline uint16_t getBlockLengthBytes() {
    return getBlockLength()*32;
  }
  inline uint16_t getFeeId() {
    //return ((rdhPtr->words[3] >> 20 ) & 0xFFF) | ((rdhPtr->words[2] & 0xF) << 12); 
    return ((rdhPtr->lwords[1] >> 20 ) & 0xFFFF);
  }  
  inline uint8_t getLinkId() {
    return (rdhPtr->words[2] >> 4 ) & 0xFF;
  }  
  inline uint8_t getHeaderSize() {
    return (rdhPtr->words[2] >> 12 ) & 0xFF;
  }
  
  private:
  RDH *rdhPtr;  // pointer to RDH in memory
};
