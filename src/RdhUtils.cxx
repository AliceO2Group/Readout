#include "RdhUtils.h"
RdhHandle::RdhHandle(void *data) {
  rdhPtr=(RDH *)data;
}

RdhHandle::~RdhHandle(){
}

void RdhHandle::dumpRdh() {
  printf("RDH @ 0x%p\n",(void *)rdhPtr);
  printf("Version       = 0x%02X\n",(int)getHeaderVersion());
  printf("Block length  = %d 256b words\n",(int)getBlockLength());
  printf("FEE Id        = %d\n",(int)getFeeId());
  printf("Link Id       = %d\n",(int)getLinkId());
  printf("Header size   = %d\n",(int)getHeaderSize());
}

int RdhHandle::validateRdh(std::string &err) {
  int retCode=0;
  // expecting RDH V1
  if (getHeaderVersion()!=1) {
    err+="Wrong header version\n";
    retCode++;
  }
  // expecting 2x256 bits for header
  if (getHeaderSize()!=2) {
    err+="Wrong header size\n";
    retCode++;  
  }
  // expecting linkId 0-31
  if (getLinkId()>31) {
    err+="Wrong link ID\n";
    retCode++;  
  }
  // expecting block length <= 256 x 256bits words (8kB)
  if (getBlockLength()>256) {
    err+="Wrong block length\n";
    retCode++;  

  }
  // check FEE Id ?  
  return retCode;
}

void RdhHandle::reset() {
  for (int i=0;i<RDH_WORDS/2;i++) {
    rdhPtr->lwords[i]=0;
  }
}
