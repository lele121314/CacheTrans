# Makefile wrapper for waf

all:
	./waf

# free free to change this part to suit your requirements
configure:
	CXXFLAGS='-std=c++11' ./waf configure --enable-examples --enable-tests

build:
	./waf build

install:
	./waf install

clean:
	./waf clean

distclean:
	./waf distclean
