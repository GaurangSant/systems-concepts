// main.cpp — bpsim: replays branch traces through a predictor.
// DO NOT MODIFY. Students only add files under predictors/.
//
// Usage:
//   ./bpsim -l                          list registered predictors
//   ./bpsim -p <name> <trace> [...]     run one predictor on one or more traces
//   ./bpsim -s <name>                   show the storage breakdown of a predictor
//
// Trace format (plain text, optionally .gz):
//   # instructions: <total dynamic instructions in the program>
//   <pc in hex> <0|1>          one line per executed conditional branch
//   lines starting with '#' are comments.

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "predictor.h"

// ---- Registry -----------------------------------------------------------

static std::map<std::string, PredictorFactory>& registry() {
  static std::map<std::string, PredictorFactory> r;
  return r;
}

bool registerPredictor(const std::string& name, PredictorFactory factory) {
  if (!registry().emplace(name, factory).second) {
    std::fprintf(stderr, "error: predictor \"%s\" registered twice\n", name.c_str());
    std::exit(2);
  }
  return true;
}

// ---- Construction under the storage ledger ---------------------------------
// Storage objects may only be created while the ledger is open, i.e. inside the
// predictor's constructor. The ledger then reports the total bits.

static std::unique_ptr<BranchPredictor> create(PredictorFactory f, uint64_t& bits) {
  auto& l = storage_detail::ledger();
  l.open = true;
  l.total = 0;
  l.items.clear();
  auto bp = f();
  l.open = false;
  bits = l.total;
  return bp;
}

// ---- Trace reading --------------------------------------------------------

struct TraceFile {
  FILE* f = nullptr;
  bool piped = false;

  explicit TraceFile(const std::string& path) {
    bool gz = path.size() > 3 && path.compare(path.size() - 3, 3, ".gz") == 0;
    if (gz) {
      std::string cmd = "gzip -dc '" + path + "'";
      f = popen(cmd.c_str(), "r");
      piped = true;
    } else {
      f = std::fopen(path.c_str(), "r");
    }
  }
  ~TraceFile() {
    if (f) piped ? pclose(f) : std::fclose(f);
  }
};

struct Result {
  uint64_t instructions = 0;  // from header; 0 if missing
  uint64_t branches = 0;
  uint64_t mispredictions = 0;
};

static bool runTrace(BranchPredictor& bp, const std::string& path, Result& res) {
  TraceFile tf(path);
  if (!tf.f) {
    std::fprintf(stderr, "error: cannot open %s\n", path.c_str());
    return false;
  }
  char line[256];
  uint64_t lineNo = 0;
  while (std::fgets(line, sizeof line, tf.f)) {
    ++lineNo;
    if (line[0] == '#') {
      std::sscanf(line, "# instructions: %" SCNu64, &res.instructions);
      continue;
    }
    if (line[0] == '\n' || line[0] == '\0') continue;

    char* end = nullptr;
    uint64_t pc = std::strtoull(line, &end, 16);
    while (*end == ' ' || *end == '\t') ++end;
    if (end == line || (*end != '0' && *end != '1')) {
      std::fprintf(stderr, "error: %s:%" PRIu64 ": malformed line\n", path.c_str(), lineNo);
      return false;
    }
    bool taken = (*end == '1');

    bool guess = bp.predict(pc);
    if (guess != taken) ++res.mispredictions;
    bp.update(pc, taken);
    ++res.branches;
  }
  return true;
}

// ---- Main ----------------------------------------------------------------

static void usage() {
  std::fprintf(stderr,
               "usage: bpsim -l\n"
               "       bpsim -p <predictor> <trace> [<trace> ...]\n"
               "       bpsim -s <predictor>\n");
}

int main(int argc, char** argv) {
  std::string name;
  std::vector<std::string> traces;

  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "-l")) {
      for (auto& [n, f] : registry()) {
        uint64_t b;
        create(f, b);
        std::printf("%-16s %8" PRIu64 " bits%s\n", n.c_str(), b,
                    b > STORAGE_BUDGET_BITS ? "  OVER BUDGET" : "");
      }
      return 0;
    } else if (!std::strcmp(argv[i], "-s") && i + 1 < argc) {
      auto it = registry().find(argv[++i]);
      if (it == registry().end()) {
        std::fprintf(stderr, "error: unknown predictor \"%s\" (try -l)\n", argv[i]);
        return 2;
      }
      uint64_t b;
      create(it->second, b);
      for (auto& [n, bits] : storage_detail::ledger().items)
        std::printf("  %-24s %8" PRIu64 " bits\n", n.c_str(), bits);
      std::printf("  %-24s %8" PRIu64 " / %" PRIu64 " bits\n", "TOTAL", b, STORAGE_BUDGET_BITS);
      return 0;
    } else if (!std::strcmp(argv[i], "-p") && i + 1 < argc) {
      name = argv[++i];
    } else if (argv[i][0] == '-') {
      usage();
      return 2;
    } else {
      traces.push_back(argv[i]);
    }
  }
  if (name.empty() || traces.empty()) {
    usage();
    return 2;
  }

  auto it = registry().find(name);
  if (it == registry().end()) {
    std::fprintf(stderr, "error: unknown predictor \"%s\" (try -l)\n", name.c_str());
    return 2;
  }

  // Budget check on a fresh instance.
  uint64_t bits;
  create(it->second, bits);
  if (bits > STORAGE_BUDGET_BITS) {
    std::fprintf(stderr, "error: %s uses %" PRIu64 " bits, budget is %" PRIu64 "\n",
                 name.c_str(), bits, STORAGE_BUDGET_BITS);
    return 3;
  }

  std::printf("predictor: %s (%" PRIu64 " bits)\n", name.c_str(), bits);
  std::printf("%-28s %12s %12s %9s %8s\n", "trace", "branches", "mispredicts", "accuracy", "MPKI");

  double mpkiSum = 0;
  int mpkiCount = 0;
  for (const auto& t : traces) {
    uint64_t b;
    auto bp = create(it->second, b);  // fresh predictor per trace
    Result r;
    if (!runTrace(*bp, t, r)) return 1;

    std::string shortName = t.substr(t.find_last_of('/') + 1);
    double acc = r.branches ? 100.0 * (r.branches - r.mispredictions) / r.branches : 0;
    std::printf("%-28s %12" PRIu64 " %12" PRIu64 " %8.2f%%", shortName.c_str(), r.branches,
                r.mispredictions, acc);
    if (r.instructions) {
      double mpki = 1000.0 * r.mispredictions / r.instructions;
      std::printf(" %8.3f\n", mpki);
      mpkiSum += mpki;
      ++mpkiCount;
    } else {
      std::printf(" %8s\n", "n/a");
    }
  }
  if (mpkiCount > 1) std::printf("average MPKI: %.3f\n", mpkiSum / mpkiCount);
  return 0;
}
