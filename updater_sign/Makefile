all: updater_sign

key.hpp: key.hpp.in
	test -f key.hpp || cp key.hpp.in key.hpp

updater_sign: sign.cpp key.hpp
	g++ -O2 -std=c++17 `pkg-config --cflags libfilezilla` sign.cpp `pkg-config --libs libfilezilla --static` -o updater_sign



clean:
	rm -f *.o updater_sign
