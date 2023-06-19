CXXFLAGS  = -m64 -std=c++17 -Wall -Werror -O3 -gdwarf
CXXFLGAS += -fno-omit-frame-pointer
CXXFLAGS += $(EXTRA_CXXFLAGS)

LDFLAGS   = -rdynamic
LDFLAGS  += $(EXTRA_LDFLAGS)

TEST_CXXFLAGS = $(CXXFLAGS)
TEST_LDFLAGS  = $(LDFLAGS) -Wl,-rpath,$(PWD)

TEST_A_CXXFLAGS = $(CXXFLAGS) -fPIC -O0
TEST_A_LDFLAGS  = $(LDFLAGS)

TEST_B_CXXFLAGS = $(CXXFLAGS) -fPIC -O0
TEST_B_LDFLAGS  = $(LDFLAGS)

LIB_CXXFLAGS  = $(CXXFLAGS) -fPIC
LIB_LDFLAGS   = $(LDFLAGS) -lbfd

TEST_ARGS ?= 20


.PHONY: all
all: test libtest_a.so libtest_b.so libfast_backtrace_symbols.so


.PHONY: clean
clean:
	rm -f test $(TEST_OBJS)
	rm -f libtest_a.so $(TEST_A_OBJS)
	rm -f libtest_b.so $(TEST_B_OBJS)
	rm -f libfast_backtrace_symbols.so $(LIB_OBJS)


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


TEST_A_SRCS = test_a.cpp
TEST_A_OBJS = $(TEST_A_SRCS:.cpp=.o)
$(TEST_A_OBJS): %.o: %.cpp %.h
	$(CXX) $(TEST_A_CXXFLAGS) -c -o $@ $<

libtest_a.so: $(TEST_A_OBJS)
	$(CXX) -o $@ $^ -shared $(TEST_A_LDFLAGS)


TEST_B_SRCS = test_b.cpp
TEST_B_OBJS = $(TEST_B_SRCS:.cpp=.o)
$(TEST_B_OBJS): %.o: %.cpp %.h
	$(CXX) $(TEST_B_CXXFLAGS) -c -o $@ $<

libtest_b.so: $(TEST_B_OBJS)
	$(CXX) -o $@ $^ -shared $(TEST_B_LDFLAGS)


LIB_SRCS = fast_backtrace_symbols.cpp
LIB_OBJS = $(LIB_SRCS:.cpp=.o)
$(LIB_OBJS): %.o: %.cpp
	$(CXX) $(LIB_CXXFLAGS) -c -o $@ $<


libfast_backtrace_symbols.so: $(LIB_OBJS)
	$(CXX) -o $@ $^ -shared $(LIB_LDFLAGS)
