CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pthread \
  -I.deps/boost/usr/include -I.deps/json/include -Isrc
LDFLAGS ?= -pthread

SRCS := src/ais.cpp src/track.cpp src/predict.cpp src/udp_listener.cpp src/http_server.cpp src/config.cpp
OBJS := $(SRCS:.cpp=.o)

all: build/ais_maneuver228 build/ais_maneuver228_tests

build:
	mkdir -p build

$(OBJS): %.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/ais_maneuver228: $(OBJS) src/main.o | build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/ais_maneuver228_tests: $(SRCS:.cpp=.o) tests/test_ais.o | build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

tests/test_ais.o: tests/test_ais.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/main.o: src/main.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f src/*.o tests/*.o build/ais_maneuver228 build/ais_maneuver228_tests

.PHONY: all clean test
test: build/ais_maneuver228_tests
	./build/ais_maneuver228_tests
