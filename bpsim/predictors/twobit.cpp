// twobit.cpp — Part A: a table of 2-bit saturating counters, indexed by PC (bimodal).
//
// Keep ALL state in storage.h objects declared as members below (Register,
// Table, DirectMappedCache, SetAssocCache). No other member variables, statics,
// globals, containers, or heap allocation. Sizes are your choice; bpsim counts
// the bits and the total must stay within 64 Kbit (check with ./bpsim -s twobit).
#include "predictor.h"

class TwoBitPredictor : public BranchPredictor {
  static constexpr unsigned INDEX_BITS = 15;
  static constexpr unsigned TOTAL_ENTRIES = 1u << INDEX_BITS;
  Table predictor{TOTAL_ENTRIES, 2, 00, Unsigned, "two bit"};

 public:
  bool predict(uint64_t pc) override {
    int row = (pc >> 2) % TOTAL_ENTRIES;
    return (predictor.get(row));

    (void)pc;
  }

  void update(uint64_t pc, bool taken) override {
    int row = (pc >> 2) % TOTAL_ENTRIES;
    int updatePredict = predictor.get(row);
    if(taken){
      if(updatePredict != 11) predictor.set(row, ++updatePredict);
    }else{
      if(updatePredict != 00) predictor.set(row, --updatePredict);
    }
    
    
    (void)pc;
    (void)taken;
  }
};

REGISTER_PREDICTOR("twobit", TwoBitPredictor);
