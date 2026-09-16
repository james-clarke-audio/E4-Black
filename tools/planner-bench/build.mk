# Native build of the route planner, for scoring routes against real maze files
# without a robot. Same planner.cpp the firmware compiles.
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra -I../../Source/Program/inc
plan: main.cpp ../../Source/Program/src/planner.cpp ../../Source/Program/src/diagonal.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^
clean:
	rm -f plan
.PHONY: clean
