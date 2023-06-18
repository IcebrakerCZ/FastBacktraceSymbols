#include "test_a.h"

#include <iostream>

void backtrace_symbols_test_b(unsigned depth, bool (*callback)(unsigned))
{
  ++depth;

  if (!callback(depth))
  {
    return;
  }

  backtrace_symbols_test_a(depth, callback);
}
