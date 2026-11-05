// always_taken.cpp — worked example 1: no state at all, so 0 bits.
#include "predictor.h"

class AlwaysTaken : public BranchPredictor {
 public:
  bool predict(uint64_t) override { return true; }
  void update(uint64_t, bool) override {}
};

REGISTER_PREDICTOR("always_taken", AlwaysTaken);
