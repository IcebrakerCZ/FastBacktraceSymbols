CXXFLAGS  = -m64 -std=c++17 -Wall -Werror -O3 -ggdb3
CXXFLGAS += -fno-omit-frame-pointer
CXXFLAGS += $(EXTRA_CXXFLAGS)

LDFLAGS   = -rdynamic
LDFLAGS  += $(EXTRA_LDFLAGS)


TEST_CXXFLAGS = $(CXXFLAGS)
TEST_LDFLAGS  = $(LDFLAGS) -Wl,-rpath,$(PWD)


LIB_CXXFLAGS  = $(CXXFLAGS) -fPIC
LIB_LDFLAGS   = $(LDFLAGS)

LIB_TEST_A_CXXFLAGS = $(LIB_CXXFLAGS) -O0
LIB_TEST_A_LDFLAGS  = $(LIB_LDFLAGS)

LIB_TEST_B_CXXFLAGS = $(LIB_CXXFLAGS) -O0
LIB_TEST_B_LDFLAGS  = $(LIB_LDFLAGS)

LIB_FAST_BACKTRACE_SYMBOLS_CXXFLAGS  = $(LIB_CXXFLAGS)
#LIB_FAST_BACKTRACE_SYMBOLS_CXXFLAGS += -nostdlib -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free
LIB_FAST_BACKTRACE_SYMBOLS_LDFLAGS  = $(LIB_LDFLAGS)
LIB_FAST_BACKTRACE_SYMBOLS_LDFLAGS += -Wl,-Bsymbolic
LIB_FAST_BACKTRACE_SYMBOLS_LDFLAGS += -ldl
LIB_FAST_BACKTRACE_SYMBOLS_LDFLAGS += -lbfd
#LIB_FAST_BACKTRACE_SYMBOLS_LDFLAGS += /usr/lib/x86_64-linux-gnu/libbfd.a
#LIB_FAST_BACKTRACE_SYMBOLS_LDFLAGS += /usr/lib/x86_64-linux-gnu/libiberty.a
#LIB_FAST_BACKTRACE_SYMBOLS_LDFLAGS += /usr/lib/x86_64-linux-gnu/libunwind.a
#LIB_FAST_BACKTRACE_SYMBOLS_LDFLAGS += -static-libstdc++
#LIB_FAST_BACKTRACE_SYMBOLS_LDFLAGS += -B /usr/share/libhugetlbfs -Wl,--hugetlbfs-align -lhugetlbfs


TEST_ARGS ?= 20


.PHONY: all
all: test libtest_a.so libtest_b.so libfast_backtrace_symbols.so


.PHONY: clean
clean:
	rm -f test $(TEST_OBJS)
	rm -f libtest_a.so $(LIB_TEST_A_OBJS)
	rm -f libtest_b.so $(LIB_TEST_B_OBJS)
	rm -f libfast_backtrace_symbols.so $(LIB_FAST_BACKTRACE_SYMBOLS_OBJS) glibc_versions.h


.PHONY: run
run: run-test-with-fast-backtrace-symbols run-test-without-fast-backtrace-symbols
	@echo

.PHONY: run-test-with-fast-backtrace-symbols
run-test-with-fast-backtrace-symbols: test libfast_backtrace_symbols.so
	@echo
	./fast_backtrace_symbols ./test $(TEST_ARGS)

.PHONY: run-test-without-fast-backtrace-symbols
run-test-without-fast-backtrace-symbols: test
	@echo
	./test $(TEST_ARGS)


TEST_SRCS = test.cpp
TEST_OBJS = $(TEST_SRCS:.cpp=.o)
$(TEST_OBJS): %.o: %.cpp
	$(CXX) $(TEST_CXXFLAGS) -c -o $@ $<

test: $(TEST_OBJS) libtest_a.so libtest_b.so
	$(CXX) -o $@ $< $(TEST_LDFLAGS) -L. -ltest_a -ltest_b


LIB_TEST_A_SRCS = test_a.cpp
LIB_TEST_A_OBJS = $(LIB_TEST_A_SRCS:.cpp=.o)
$(LIB_TEST_A_OBJS): %.o: %.cpp %.h
	$(CXX) $(LIB_TEST_A_CXXFLAGS) -c -o $@ $<

libtest_a.so: $(LIB_TEST_A_OBJS)
	$(CXX) -o $@ $^ -shared $(LIB_TEST_A_LDFLAGS)


LIB_TEST_B_SRCS = test_b.cpp
LIB_TEST_B_OBJS = $(LIB_TEST_B_SRCS:.cpp=.o)
$(LIB_TEST_B_OBJS): %.o: %.cpp %.h
	$(CXX) $(LIB_TEST_B_CXXFLAGS) -c -o $@ $<

libtest_b.so: $(LIB_TEST_B_OBJS)
	$(CXX) -o $@ $^ -shared $(LIB_TEST_B_LDFLAGS)


LIB_FAST_BACKTRACE_SYMBOLS_SRCS = fast_backtrace_symbols.cpp
LIB_FAST_BACKTRACE_SYMBOLS_OBJS = $(LIB_FAST_BACKTRACE_SYMBOLS_SRCS:.cpp=.o)
$(LIB_FAST_BACKTRACE_SYMBOLS_OBJS): %.o: %.cpp glibc_versions.h
	$(CXX) $(LIB_FAST_BACKTRACE_SYMBOLS_CXXFLAGS) -c -o $@ $<


libfast_backtrace_symbols.so: $(LIB_FAST_BACKTRACE_SYMBOLS_OBJS)
	$(CXX) -o $@ $^ -shared $(LIB_FAST_BACKTRACE_SYMBOLS_LDFLAGS)


glibc_versions.h:
	strings /lib/x86_64-linux-gnu/libc.so.6                                      \
	    | grep GLIBC_                                                            \
	    | grep -v GLIBC_PRIVATE                                                  \
	    | sort -rV                                                               \
	    | sed -E -e '1 { s/^(.*)$$/const char* glibc_versions[] = { "\1"/ }'     \
	             -e '2,999 { s/^(.*)$$/                               , "\1"/ }' \
	    > $@
	echo "                              };" >> $@
