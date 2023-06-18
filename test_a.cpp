#include "test_b.h"

#include <iostream>

void backtrace_symbols_test_a(unsigned depth, bool (*callback)(unsigned))
{
  ++depth;

  if (!callback(depth))
  {
    return;
  }

  backtrace_symbols_test_b(depth, callback);
}
