
SRCS=	main.cpp args.cpp spec.cpp create.cpp file.cpp jail.cpp util.cpp
OBJS=   $(SRCS:.cpp=.o)

CXXFLAGS+=  -I/usr/local/include
LDFLAGS+=   -L/usr/local/lib -lyaml-cpp

CXXFLAGS+=  -Wall -std=c++17

all: crate

crate: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS)

install: crate
	install -s -m 04755 crate $(DESTDIR)$(PREFIX)/bin

install-local: crate
	sudo install -s -m 04755 -o 0 -g 0 crate crate.x

clean:
	rm $(OBJS) crate
