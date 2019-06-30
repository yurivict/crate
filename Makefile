
SRCS=	main.cpp args.cpp spec.cpp create.cpp file.cpp jail.cpp util.cpp
OBJS=   $(SRCS:.cpp=.o)

CXXFLAGS+=  -I/usr/local/include
LDFLAGS+=   -L/usr/local/lib -lyaml-cpp

CXXFLAGS+=  -Wall -std=c++17

all: crate

crate: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS)

install:
	install crate $(DESTDIR)$(PREFIX)/bin

clean:
	rm $(OBJS) crate
