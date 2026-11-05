// storage.h — the ONLY storage your predictor may use.
// DO NOT MODIFY THIS FILE. It is replaced with the original during grading.
//
// Every bit of state that survives from one predict()/update() call to the next
// must live in one of these objects, declared as a member of your predictor
// class and created in its constructor:
//
//   Register          one W-bit register              (e.g. global history)
//   Table             N entries x W bits, no tags      (e.g. 2-bit counters)
//   DirectMappedCache N entries, each valid+tag+data   (tagged, 1 way)
//   SetAssocCache     S sets x K ways, valid+tag+data, optional LRU
//
// The library counts bits automatically; bpsim refuses to run a predictor
// above STORAGE_BUDGET_BITS. Values are checked against their declared width,
// so a W-bit field really holds only W bits.
//
// Field widths are 1..64 bits. A field is unsigned (0 .. 2^W-1) by default or
// signed two's complement (-2^(W-1) .. 2^(W-1)-1) if created with Signed.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

constexpr uint64_t STORAGE_BUDGET_BITS = 64 * 1024;  // 64 Kbit = 8 KB

enum Signedness { Unsigned, Signed };
enum Replacement { LRU, Manual };

// ---- Ledger: counts bits as structures are created ------------------------

namespace storage_detail {

struct Ledger {
  bool open = false;  // structures may only be created while open
  uint64_t total = 0;
  std::vector<std::pair<std::string, uint64_t>> items;
};
inline Ledger& ledger() {
  static Ledger l;
  return l;
}

[[noreturn]] inline void fail(const std::string& what, const std::string& msg) {
  std::fprintf(stderr, "storage error [%s]: %s\n", what.empty() ? "unnamed" : what.c_str(),
               msg.c_str());
  std::exit(4);
}

inline void charge(const std::string& name, uint64_t bits) {
  Ledger& l = ledger();
  if (!l.open)
    fail(name,
         "storage may only be created as a member of your predictor, in its "
         "constructor (not in predict()/update(), not as a global)");
  l.total += bits;
  l.items.emplace_back(name, bits);
}

inline unsigned ceilLog2(uint64_t n) {
  unsigned b = 0;
  while ((1ull << b) < n) ++b;
  return b;
}

// A W-bit field: range and checks.
struct Field {
  unsigned width;
  bool isSigned;
  int64_t lo, hi;
  std::string name;

  Field(unsigned w, Signedness s, std::string n) : width(w), isSigned(s == Signed), name(std::move(n)) {
    if (w < 1 || w > 64) fail(name, "width must be 1..64 bits");
    if (isSigned) {
      lo = (w == 64) ? INT64_MIN : -(int64_t(1) << (w - 1));
      hi = (w == 64) ? INT64_MAX : (int64_t(1) << (w - 1)) - 1;
    } else {
      lo = 0;
      hi = (w >= 63) ? INT64_MAX : (int64_t(1) << w) - 1;
    }
  }
  void check(int64_t v) const {
    if (v < lo || v > hi)
      fail(name, "value " + std::to_string(v) + " does not fit in " + std::to_string(width) +
                     (isSigned ? "-bit signed" : "-bit unsigned") + " field");
  }
  int64_t satInc(int64_t v) const { return v < hi ? v + 1 : v; }
  int64_t satDec(int64_t v) const { return v > lo ? v - 1 : v; }
};

}  // namespace storage_detail

// Not copyable or movable: a copy would be untracked storage.
#define STORAGE_NO_COPY(T)        \
  T(const T&) = delete;           \
  T& operator=(const T&) = delete; \
  T(T&&) = delete;                \
  T& operator=(T&&) = delete;

// ---- Register --------------------------------------------------------------

class Register {
 public:
  explicit Register(unsigned bits, int64_t init = 0, Signedness s = Unsigned,
                    const std::string& name = "")
      : f_(bits, s, name), v_(init) {
    f_.check(init);
    storage_detail::charge(name.empty() ? "Register" : name, bits);
  }
  STORAGE_NO_COPY(Register)

  int64_t get() const { return v_; }
  void set(int64_t v) { f_.check(v); v_ = v; }
  void inc() { v_ = f_.satInc(v_); }  // saturating
  void dec() { v_ = f_.satDec(v_); }  // saturating

  // Shift left by one and put `bit` in the lowest position; the oldest bit falls
  // off the top. For history registers (unsigned only).
  void shiftIn(bool bit) {
    if (f_.isSigned) storage_detail::fail(f_.name, "shiftIn() needs an unsigned register");
    uint64_t mask = (f_.width == 64) ? ~0ull : ((1ull << f_.width) - 1);
    v_ = int64_t(((uint64_t(v_) << 1) | (bit ? 1 : 0)) & mask);
  }
  unsigned width() const { return f_.width; }

 private:
  storage_detail::Field f_;
  int64_t v_;
};

// ---- Table: N entries x W bits, indexed directly, no tags ------------------

class Table {
 public:
  Table(uint64_t entries, unsigned bitsPerEntry, int64_t init = 0, Signedness s = Unsigned,
        const std::string& name = "")
      : f_(bitsPerEntry, s, name), v_(entries, init) {
    if (entries == 0) storage_detail::fail(name, "table needs at least one entry");
    f_.check(init);
    storage_detail::charge(name.empty() ? "Table" : name, entries * bitsPerEntry);
  }
  STORAGE_NO_COPY(Table)

  int64_t get(uint64_t i) const { return v_[at(i)]; }
  void set(uint64_t i, int64_t v) { f_.check(v); v_[at(i)] = v; }
  void inc(uint64_t i) { auto& x = v_[at(i)]; x = f_.satInc(x); }  // saturating
  void dec(uint64_t i) { auto& x = v_[at(i)]; x = f_.satDec(x); }  // saturating

  uint64_t size() const { return v_.size(); }
  int64_t maxValue() const { return f_.hi; }
  int64_t minValue() const { return f_.lo; }

 private:
  uint64_t at(uint64_t i) const {
    if (i >= v_.size())
      storage_detail::fail(f_.name, "index " + std::to_string(i) + " out of range (size " +
                                        std::to_string(v_.size()) + ")");
    return i;
  }
  storage_detail::Field f_;
  std::vector<int64_t> v_;
};

// ---- SetAssocCache: S sets x K ways, each entry = valid + tag + data ------
//
// Tags are truncated to tagBits automatically (partial tags, as in hardware).
// With LRU replacement each set also pays K * ceil(log2 K) bits of LRU state.
// With Manual replacement you choose the victim way yourself (no LRU bits);
// use the data field (or another Table) for any "useful" bits you need.

class SetAssocCache {
 public:
  SetAssocCache(uint64_t sets, unsigned ways, unsigned tagBits, unsigned dataBits,
                Replacement r = LRU, Signedness s = Unsigned, const std::string& name = "")
      : f_(dataBits, s, name), sets_(sets), ways_(ways), tagBits_(tagBits), repl_(r),
        e_(sets * ways) {
    if (sets == 0 || ways == 0) storage_detail::fail(name, "need at least one set and one way");
    if (tagBits > 64) storage_detail::fail(name, "tagBits must be 0..64");
    lruBits_ = (r == LRU && ways > 1) ? storage_detail::ceilLog2(ways) : 0;
    for (uint64_t st = 0; st < sets; ++st)
      for (unsigned w = 0; w < ways; ++w) e_[st * ways + w].age = w;
    uint64_t perEntry = 1 + tagBits + dataBits + lruBits_;
    storage_detail::charge(name.empty() ? "SetAssocCache" : name, sets * ways * perEntry);
  }
  STORAGE_NO_COPY(SetAssocCache)

  // Way holding `tag` in `set`, or -1 on a miss. Does not change LRU order.
  int lookup(uint64_t set, uint64_t tag) const {
    tag = trunc(tag);
    for (unsigned w = 0; w < ways_; ++w) {
      const Entry& x = e_[idx(set, w)];
      if (x.valid && x.tag == tag) return int(w);
    }
    return -1;
  }

  bool valid(uint64_t set, unsigned way) const { return e_[idx(set, way)].valid; }
  int64_t read(uint64_t set, unsigned way) const { return live(set, way).data; }
  void write(uint64_t set, unsigned way, int64_t v) { f_.check(v); live(set, way).data = v; }
  void inc(uint64_t set, unsigned way) { auto& d = live(set, way).data; d = f_.satInc(d); }
  void dec(uint64_t set, unsigned way) { auto& d = live(set, way).data; d = f_.satDec(d); }

  // Mark a way most-recently-used (LRU caches only).
  void touch(uint64_t set, unsigned way) {
    needLRU("touch()");
    live(set, way);
    makeMRU(set, way);
  }

  // LRU caches: put (tag, data) in an invalid way if any, else evict the LRU way.
  // The new entry becomes MRU. Returns the way used.
  int insert(uint64_t set, uint64_t tag, int64_t data) {
    needLRU("insert()");
    unsigned victim = 0;
    bool found = false;
    for (unsigned w = 0; w < ways_ && !found; ++w)
      if (!e_[idx(set, w)].valid) victim = w, found = true;
    if (!found)
      for (unsigned w = 0; w < ways_; ++w)
        if (e_[idx(set, w)].age == ways_ - 1) victim = w;
    insertAt(set, victim, tag, data);
    return int(victim);
  }

  // Any cache: overwrite a specific way with (tag, data). Becomes MRU under LRU.
  void insertAt(uint64_t set, unsigned way, uint64_t tag, int64_t data) {
    f_.check(data);
    Entry& x = e_[idx(set, way)];
    x.valid = true;
    x.tag = trunc(tag);
    x.data = data;
    if (lruBits_) makeMRU(set, way);
  }

  void invalidate(uint64_t set, unsigned way) { e_[idx(set, way)].valid = false; }

  uint64_t sets() const { return sets_; }
  unsigned ways() const { return ways_; }

 private:
  struct Entry {
    bool valid = false;
    uint64_t tag = 0;
    int64_t data = 0;
    unsigned age = 0;  // 0 = MRU
  };
  uint64_t trunc(uint64_t t) const {
    return tagBits_ >= 64 ? t : (tagBits_ == 0 ? 0 : t & ((1ull << tagBits_) - 1));
  }
  uint64_t idx(uint64_t set, unsigned way) const {
    if (set >= sets_)
      storage_detail::fail(f_.name, "set " + std::to_string(set) + " out of range (sets " +
                                        std::to_string(sets_) + ")");
    if (way >= ways_)
      storage_detail::fail(f_.name, "way " + std::to_string(way) + " out of range (ways " +
                                        std::to_string(ways_) + ")");
    return set * ways_ + way;
  }
  Entry& live(uint64_t set, unsigned way) {
    Entry& x = e_[idx(set, way)];
    if (!x.valid) storage_detail::fail(f_.name, "access to an invalid way");
    return x;
  }
  const Entry& live(uint64_t set, unsigned way) const {
    return const_cast<SetAssocCache*>(this)->live(set, way);
  }
  void needLRU(const char* fn) const {
    if (repl_ != LRU) storage_detail::fail(f_.name, std::string(fn) + " needs an LRU cache");
  }
  void makeMRU(uint64_t set, unsigned way) {
    unsigned old = e_[idx(set, way)].age;
    for (unsigned w = 0; w < ways_; ++w) {
      Entry& x = e_[idx(set, w)];
      if (x.age < old) ++x.age;
    }
    e_[idx(set, way)].age = 0;
  }

  storage_detail::Field f_;
  uint64_t sets_;
  unsigned ways_, tagBits_, lruBits_ = 0;
  Replacement repl_;
  std::vector<Entry> e_;
};

// ---- DirectMappedCache: N entries, each valid + tag + data ------------------

class DirectMappedCache {
 public:
  DirectMappedCache(uint64_t entries, unsigned tagBits, unsigned dataBits,
                    Signedness s = Unsigned, const std::string& name = "")
      : c_(entries, 1, tagBits, dataBits, Manual, s, name.empty() ? "DirectMappedCache" : name) {}
  STORAGE_NO_COPY(DirectMappedCache)

  bool hit(uint64_t i, uint64_t tag) const { return c_.lookup(i, tag) == 0; }
  int64_t read(uint64_t i) const { return c_.read(i, 0); }
  void write(uint64_t i, int64_t v) { c_.write(i, 0, v); }
  void inc(uint64_t i) { c_.inc(i, 0); }
  void dec(uint64_t i) { c_.dec(i, 0); }
  void insert(uint64_t i, uint64_t tag, int64_t data) { c_.insertAt(i, 0, tag, data); }
  void invalidate(uint64_t i) { c_.invalidate(i, 0); }
  uint64_t size() const { return c_.sets(); }

 private:
  SetAssocCache c_;
};
