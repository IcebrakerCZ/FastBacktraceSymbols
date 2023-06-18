#include "test.h"
#include "test_b.h"

void backtrace_symbols_test_a(unsigned depth)
{
  ++depth;

  if (!backtrace_symbols_test(depth))
  {
    return;
  }

  backtrace_symbols_test_b(depth);
}
