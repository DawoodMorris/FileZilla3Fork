CXXFLAGS=-O2 -g -Wall -Wextra -pedantic -std=c++17 `pkg-config --cflags libfilezilla`
LDFLAGS=`pkg-config --libs libfilezilla`

all: ftpstresstest

ftpstresstest: ftpstresstest.cpp
	g++ $(CXXFLAGS) $< $(LDFLAGS) -o $@

clean:
	rm ftpstresstest
