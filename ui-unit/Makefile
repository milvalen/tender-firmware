CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
LIB := lib/tableboard/src
CORE := $(LIB)/tableboard.cpp $(LIB)/app.cpp
HDRS := $(LIB)/tableboard.h $(LIB)/app.h
SIM_SRC := $(wildcard sim/*.cpp)
SIM_HDRS := $(wildcard sim/*.h)

.PHONY: test sim clean

test: build/test_tableboard build/test_app build/test_selftest build/tnd01-sim
	./build/test_tableboard
	./build/test_app
	./build/test_selftest
	./build/tnd01-sim --autoplay good
	./build/tnd01-sim --autoplay sloppy --expect-fail

build/test_tableboard: test/test_tableboard.cpp $(CORE) $(HDRS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -I$(LIB) test/test_tableboard.cpp $(CORE) -o $@

build/test_app: test/test_app.cpp $(CORE) $(HDRS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -I$(LIB) test/test_app.cpp $(CORE) -o $@

build/test_selftest: test/test_selftest.cpp $(LIB)/selftest.cpp $(LIB)/selftest.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -I$(LIB) test/test_selftest.cpp $(LIB)/selftest.cpp -o $@

build/tnd01-sim: $(SIM_SRC) $(SIM_HDRS) $(CORE) $(HDRS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -I$(LIB) $(SIM_SRC) $(CORE) -o $@

# Desk test: play a shift on the screen simulator. Add ARGS="--two-keys" for the 2-key mode.
sim: build/tnd01-sim
	./build/tnd01-sim $(ARGS)

clean:
	rm -rf build
