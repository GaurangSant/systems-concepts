// onebit.cpp — Part A: a Table of last outcomes, indexed by low-order PC bits.
//
// Keep ALL state in storage.h objects declared as members below (Register,
// Table, DirectMappedCache, SetAssocCache). No other member variables, statics,
// globals, containers, or heap allocation. Sizes are your choice; bpsim counts
// the bits and the total must stay within 64 Kbit (check with ./bpsim -s onebit).
#include "predictor.h"

class OneBitPredictor : public BranchPredictor {
  Table predictor{65536, 1, 0, Unsigned, "one bit"};
  

 public:
  bool predict(uint64_t pc) override {
    int row = (pc >> 2) % 65536;
    return (predictor.get(row));

    (void)pc;
  }

  void update(uint64_t pc, bool taken) override {    
    int row = (pc >> 2) % 65536;
    predictor.set(row, taken);  
    
    (void)pc;
    (void)taken;
  }
};

REGISTER_PREDICTOR("onebit", OneBitPredictor);
