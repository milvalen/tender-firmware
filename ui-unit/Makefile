CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
LIB := lib/tableboard/src

.PHONY: test clean

test: build/test_tableboard
	./build/test_tableboard

build/test_tableboard: test/test_tableboard.cpp $(LIB)/tableboard.cpp $(LIB)/tableboard.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -I$(LIB) test/test_tableboard.cpp $(LIB)/tableboard.cpp -o $@

clean:
	rm -rf build
