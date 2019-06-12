#include "Consumer.h"

int Consumer::pushData(DataSetReference &bc) {
   int success=0;
   int error=0;
   for (auto &b : *bc) {
     if (!pushData(b)) {
       success++;
     } else {
       error++;
     }
   }
   if (error) {
     return -error; // return a negative number indicating number of errors
   }
   return success; // return a positive number indicating number of success
}
