#include "RdhUtils.h"
RdhHandle::RdhHandle(void *data) {
  rdhPtr=(o2::Header::RAWDataHeader *)data;
}

RdhHandle::~RdhHandle(){
}

void RdhHandle::dumpRdh() {
  printf("RDH @ 0x%p\n",(void *)rdhPtr);
  printf("Version       = 0x%02X\n",(int)getHeaderVersion());
  printf("Block length  = %d bytes\n",(int)getBlockLength());
  printf("FEE Id        = %d\n",(int)getFeeId());
  printf("Link Id       = %d\n",(int)getLinkId());
  printf("Header size   = %d\n",(int)getHeaderSize());
  //printf("%04X %04X %04X %04X\n",rdhPtr->word3,rdhPtr->word2,rdhPtr->word1,rdhPtr->word0);
}

int RdhHandle::validateRdh(std::string &err) {
  int retCode=0;
  // expecting RDH V3
  if (getHeaderVersion()!=3) {
    err+="Wrong header version\n";
    retCode++;
  }
  // expecting 16*32bits=64 bytes for header
  if (getHeaderSize()!=64) {
    err+="Wrong header size\n";
    retCode++;  
  }
  // expecting linkId 0-31
  if (getLinkId()>31) {
    err+="Wrong link ID\n";
    retCode++;  
  }
  // expecting block length <= 8kB
  if (getBlockLength()>8*1024) {
    err+="Wrong block length\n";
    retCode++;  

  }
  // check FEE Id ?  
  return retCode;
}
