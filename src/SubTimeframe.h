// definition of a header message for a subtimeframe
// subtimeframe made of 1 message with this header
// followed by 1 message for each heartbeat-frame
// All data come from the same data source (same linkId - but possibly different FEE ids)

struct SubTimeframe {
  uint32_t timeframeId; // id of timeframe
  uint32_t numberOfHBF; // number of HB frames (i.e. following messages)
  uint8_t linkId; // common link id of all data in this HBframe
};
