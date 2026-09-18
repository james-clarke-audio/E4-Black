# The bench-maze tools, built against the firmware's own planner sources.
#   make -f build.mk bench     search a section for the rig that shows a turn best
#   make -f build.mk checktxt  plan the shipped .txt files and print the routes
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra -I../../Source/Program/inc
SRC = ../../Source/Program/src/planner.cpp ../../Source/Program/src/diagonal.cpp ../../Source/Program/src/native.cpp

bench: bench.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

checktxt: checktxt.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f bench checktxt
.PHONY: clean
