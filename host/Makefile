# File:        host/Makefile
# Description: Builds the BBS core as a Linux program for testing.
#              make && ./bbs_host ../data 6400
#              make SAN=1   AddressSanitizer + UBSan build for pen testing
#              BBS_BACKUP_TEST_OPEN=1 ./bbs_host ...  holds the backup button
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -I../src
LDLIBS   += -lz
ifeq ($(SAN),1)
CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS  += -fsanitize=address,undefined
endif
SRCS      = $(wildcard ../src/core/*.cpp) platform_host.cpp main_host.cpp

bbs_host: $(SRCS) $(wildcard ../src/core/*.h) ../src/config.h ../src/platform/platform.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LDLIBS)

clean:
	rm -f bbs_host

.PHONY: clean
