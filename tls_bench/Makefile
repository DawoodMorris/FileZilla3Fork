CXXFLAGS=-O2 -g -Wall -Wextra -pedantic -std=c++17 `pkg-config --cflags libfilezilla`
LDFLAGS=`pkg-config --libs libfilezilla`

all: client server

server: server.cpp
	g++ $(CXXFLAGS) $< $(LDFLAGS) -o $@

client: client.cpp
	g++ $(CXXFLAGS) $< $(LDFLAGS) -o $@

clean:
	rm -f client server
