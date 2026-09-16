# File:        host/Makefile
# Description: Builds the BBS core as a Linux program for testing.
#              make && ./bbs_host ../data 6400
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -I../src
SRCS      = $(wildcard ../src/core/*.cpp) platform_host.cpp main_host.cpp

bbs_host: $(SRCS) $(wildcard ../src/core/*.h) ../src/config.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $(SRCS)

clean:
	rm -f bbs_host

.PHONY: clean
