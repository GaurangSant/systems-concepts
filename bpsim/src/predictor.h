// predictor.h — the only interface between bpsim and your predictors.
// DO NOT MODIFY THIS FILE. It is replaced with the original during grading.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "storage.h"

class BranchPredictor {
 public:
  virtual ~BranchPredictor() = default;

  // Called once per conditional branch, BEFORE its outcome is known.
  // Return true for "taken".
  virtual bool predict(uint64_t pc) = 0;

  // Called right after predict() for the same branch, with the real outcome.
  // Every predict() is followed by exactly one update() for the same pc
  // before the next predict(), and nothing changes in between, so update()
  // can recompute anything predict() looked up.
  virtual void update(uint64_t pc, bool taken) = 0;
};

// Storage is NOT reported by you: bpsim adds up every Register, Table,
// DirectMappedCache and SetAssocCache your constructor creates (storage.h).

// ---- Registration -------------------------------------------------------
// Put this line once at the bottom of your .cpp file:
//   REGISTER_PREDICTOR("gshare", GsharePredictor);
// and bpsim can then run it with:  ./bpsim -p gshare <trace>

using PredictorFactory = std::unique_ptr<BranchPredictor> (*)();
bool registerPredictor(const std::string& name, PredictorFactory factory);

#define REGISTER_PREDICTOR(NAME, CLASS)                                  \
  static bool registered_##CLASS = registerPredictor(                    \
      NAME, []() -> std::unique_ptr<BranchPredictor> {                   \
        return std::make_unique<CLASS>();                                \
      })
