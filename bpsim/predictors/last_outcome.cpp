// last_outcome.cpp — worked example 2: how to keep state.
// Predicts that this branch does whatever the PREVIOUS branch (any branch) did.
// Its only state is one 1-bit Register, so bpsim reports 1 bit.
//
// Rules shown here:
//   - state lives ONLY in storage.h objects (Register, Table, DirectMappedCache,
//     SetAssocCache), declared as members and created in the constructor;
//   - constants are `static constexpr`;
//   - locals inside predict()/update() are fine: they are wires, not storage.
#include "predictor.h"

class LastOutcome : public BranchPredictor {
  static constexpr unsigned HIST_BITS = 1;
  Register last_{HIST_BITS, /*init=*/1, Unsigned, "last outcome"};

 public:
  bool predict(uint64_t) override { return last_.get() == 1; }
  void update(uint64_t, bool taken) override { last_.shiftIn(taken); }
};

REGISTER_PREDICTOR("last_outcome", LastOutcome);
