
SRCS=   main.cpp args.cpp spec.cpp create.cpp run.cpp locs.cpp cmd.cpp mount.cpp net.cpp ctx.cpp scripts.cpp misc.cpp util.cpp err.cpp
OBJS=   $(SRCS:.cpp=.o)

PREFIX   ?=  /usr/local
CXXFLAGS +=  `pkg-config --cflags yaml-cpp`
LDFLAGS  +=  `pkg-config --libs yaml-cpp`
LIBS     +=  -ljail

CXXFLAGS+=  -Wall -std=c++17

all: crate

crate: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

install: crate
	install -s -m 04755 crate $(DESTDIR)$(PREFIX)/bin
	gzip -9 < crate.5 > $(DESTDIR)$(PREFIX)/man/man5/crate.5.gz

install-local: crate.x

install-examples:
	@mkdir -p $(DESTDIR)$(PREFIX)/share/examples/crate
	@for e in `ls examples/`; do \
		install examples/$$e $(DESTDIR)$(PREFIX)/share/examples/crate/$$e; \
	done;

crate.x: crate
	sudo install -s -m 04755 -o 0 -g 0 crate crate.x

clean:
	rm -f $(OBJS) crate lst-all-script-sections.h

# generated sources
lst-all-script-sections.h: create.cpp run.cpp
	@(echo "static std::set<std::string> allScriptSections = {\"\"" && \
	  grep -h "runScript(" create.cpp run.cpp | sed -e 's|.*runScript(|, |; s|);||' && \
	  echo "};" \
	 ) > $@
	@touch spec.cpp
	@echo "generate $@"
spec.cpp: lst-all-script-sections.h

# shortcuts
a: all
c: clean
l: install-local
e: install-examples
