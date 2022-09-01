#include <ReadoutCard/CardFinder.h>

int main(int, char**) {
  // find roc cards
  auto cards = o2::roc::findCards(); 
  for (auto const& card : cards) {
    
    
    int numaNode = -1;
    std::string numapath = "/sys/bus/pci/devices/0000:"+ card.pciAddress.toString() + "/numa_node";
    FILE *fp = fopen(numapath.c_str(), "r");
    if (fp!=nullptr) {
      char buffer[16] = "";
      if (fgets(buffer, sizeof(buffer), fp) != nullptr) {
        if (sscanf(buffer, "%d", &numaNode) != 1) {
	  numaNode = -1;
	}
      }
      fclose(fp);
    }
    
    printf("%s %s #%s numa %d (roc lib) %d (/sys)\n", card.pciAddress.toString().c_str(),o2::roc::CardType::toString(card.cardType).c_str(), card.serialId.toString().c_str(), card.numaNode, numaNode);
    
  }
  return 0;
}
