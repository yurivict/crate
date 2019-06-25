
SRCS=	main.cpp args.cpp config.cpp create.cpp jail.cpp
OBJS=   $(SRCS:.cpp=.o)

CXXFLAGS+=  -I/usr/local/include -std=c++17
LDFLAGS+=   -L/usr/local/lib -lyaml

all: crate

crate: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS)

