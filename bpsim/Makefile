# bpsim — build everything under src/ and predictors/.
CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra

SRCS := $(wildcard src/*.cpp) $(wildcard predictors/*.cpp)
OBJS := $(SRCS:.cpp=.o)

bpsim: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp src/predictor.h src/storage.h
	$(CXX) $(CXXFLAGS) -Isrc -c $< -o $@

# Generate the synthetic public traces (needs python3).
traces:
	python3 tools/gen_trace.py traces

# Run every registered predictor on every trace in traces/.
all-results: bpsim
	@for p in $$(./bpsim -l | awk '{print $$1}'); do \
	  ./bpsim -p $$p traces/*.gz; echo; done

# Check predictors against the storage rules.
check:
	python3 tools/check_rules.py predictors/*.cpp

clean:
	rm -f bpsim $(OBJS)

.PHONY: traces all-results check clean
