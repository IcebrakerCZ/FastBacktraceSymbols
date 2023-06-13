CXXFLAGS  = -m64 -std=c++17 -Wall -Werror -O3 -ggdb3
CXXFLGAS += -fno-omit-frame-pointer
CXXFLAGS += $(EXTRA_CXXFLAGS)

LDFLAGS   = -Wl,--export-dynamic
LDFLAGS  += $(EXTRA_LDFLAGS)

LIB_CXXFLAGS = $(CXXFLAGS) -fPIC
LIB_LDFLAGS  = $(LDFLAGS) -lbfd


.PHONY: all
all: libfast_backtrace_symbols.so


.PHONY: clean
clean:
	rm -f libfast_backtrace_symbols.so $(LIB_OBJS)


LIB_SRCS = fast_backtrace_symbols.cpp
LIB_OBJS = $(LIB_SRCS:.cpp=.o)
$(LIB_OBJS): %.o: %.cpp
	$(CXX) $(LIB_CXXFLAGS) -c -o $@ $<


libfast_backtrace_symbols.so: $(LIB_OBJS)
	$(CXX) -o $@ $^ -shared $(LIB_LDFLAGS)
