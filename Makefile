PWD := $(CURDIR)
GCCDEVDIR := $(HOME)/gcc-dev
GCCDIR := $(GCCDEVDIR)/gcc-install/bin

CXX = $(GCCDIR)/g++
# In some of the case, target g++ will get fail when loading the plugin.
# For such case, use the host g++.
HOST_CXX := g++
#HOST_CXX := $(CXX)

# Flags for the C++ compiler: enable C++11 and all the warnings,
# -fno-rtti is required for GCC plugins
CXXFLAGS := -std=c++11
CXXFLAGS += -fno-rtti
CXXFLAGS += -Wall
#CXXFLAGS += --verbose

# Workaround for an issue of -std=c++11 and the current GCC headers
CXXFLAGS += -Wno-literal-suffix

# Determine the plugin-dir and add it to the flags
PLUGINDIR=$(shell $(CXX) -print-file-name=plugin)
CXXFLAGS += -I$(PLUGINDIR)/include

SO := plugin-ownership.so

SRC := src/ownership.cc
#SRC := src/graph.cc
OBJ := $(SRC:.cc=.o)

# top level goal: build our plugin as a shared library
all: $(SO)

$(SO) : $(OBJ)
	$(HOST_CXX) $(LDFLAGS) -shared -o $@ $<

%.o : %.cc
	$(HOST_CXX) $(CXXFLAGS) -fPIC -c -o $@ $<

clean:
	rm -f $(OBJ)
	rm -f $(SO)

check: $(SO)
	$(CXX) -fplugin=./$(SO) -c -x c++ -std=c++11 /dev/null -o /dev/null

indent:
	clang-format -i $(SRC)

cscope:
	find $(PWD)/../ -name "*.cc" -o -name "*.h" > $(PWD)/../cscope.files
	cscope -b -q

.PHONY: all clean check
