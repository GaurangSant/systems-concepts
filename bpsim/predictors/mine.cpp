// mine.cpp — Part B: your own predictor (perceptron, loop, TAGE-style, or a new idea).
//
// Keep ALL state in storage.h objects declared as members below (Register,
// Table, DirectMappedCache, SetAssocCache). No other member variables, statics,
// globals, containers, or heap allocation. Sizes are your choice; bpsim counts
// the bits and the total must stay within 64 Kbit (check with ./bpsim -s mine).
#include "predictor.h"

class MyPredictor : public BranchPredictor {
  // TODO: constants, e.g.  static constexpr unsigned INDEX_BITS = ...;
  // TODO: storage,   e.g.  Table counters_{1u << INDEX_BITS, 2, 1, Unsigned, "counters"};

 public:
  bool predict(uint64_t pc) override {
    // TODO
    (void)pc;
    return false;
  }

  void update(uint64_t pc, bool taken) override {
    // TODO
    (void)pc;
    (void)taken;
  }
};

REGISTER_PREDICTOR("mine", MyPredictor);
