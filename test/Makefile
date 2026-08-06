# Build and run the host-side motion core tests.
#   make        - build the test utility
#   make test   - build then run the automated suite non-interactively
#   make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++14 -O2 -Wall -Wextra -I../firmware

test_motion: test_motion.cpp ../firmware/ceiling_motion.h
	$(CXX) $(CXXFLAGS) test_motion.cpp -o test_motion

.PHONY: test clean
test: test_motion
	printf '1\n0\n' | ./test_motion

clean:
	rm -f test_motion
