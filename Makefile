
SRCS=	main.cpp args.cpp spec.cpp create.cpp run.cpp file.cpp locs.cpp misc.cpp util.cpp
OBJS=   $(SRCS:.cpp=.o)

PREFIX   ?=  /usr/local
CXXFLAGS +=  `pkg-config --cflags yaml-cpp`
LDFLAGS  +=  `pkg-config --libs yaml-cpp` -ljail

CXXFLAGS+=  -Wall -std=c++17

all: crate

crate: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS)

install: crate
	install -s -m 04755 crate $(DESTDIR)$(PREFIX)/bin

install-local: crate
	sudo install -s -m 04755 -o 0 -g 0 crate crate.x

clean:
	rm -f $(OBJS) crate

# shortcuts
a: all
c: clean
l: install-local
