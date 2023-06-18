#include "test.h"
#include "test_a.h"

void backtrace_symbols_test_b(unsigned depth)
{
  ++depth;

  if (!backtrace_symbols_test(depth))
  {
    return;
  }

  backtrace_symbols_test_a(depth);
}
