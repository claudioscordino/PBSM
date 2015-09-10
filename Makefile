export CC=gcc
export CXX=g++
export CXXFLAGS=-std=c++11 -Wno-unused-variable -Wno-unused-but-set-variable -O0 -g -pthread -fPIC
export LIBNAME=libpbsm
export LIBVERSION=0.1

.PHONY: clean ./bin/$(LIBNAME).so ./bin/$(LIBNAME).a install doc

all: ./bin/$(LIBNAME).so ./bin/$(LIBNAME).a
	make -C apps

./bin/$(LIBNAME).so ./bin/$(LIBNAME).a:
	make -C src

doc:
	make -C doc

clean:
	make -C apps clean
	make -C src clean
	make -C doc clean

install: ./bin/$(LIBNAME).so ./bin/$(LIBNAME).a
	sudo cp -f -a ./bin/$(LIBNAME)* /usr/lib/ 
