PWD := $(CURDIR)
GCCDIR := $(PWD)/../../gcc-install/bin

CXX = $(GCCDIR)/g++
#CXX := g++

SO := ../plugin-ownership.so

all: $(SO) test.cc
	$(CXX) -fplugin=./$(SO) -c test.cc -o /dev/null 2> test.dot
	dot -Tpdf test.dot > test.pdf
	xdg-open test.pdf

$(SO):
	make -C ../ all
