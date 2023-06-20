#include "test_a.h"

#include <chrono>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>

#include <assert.h>
#include <execinfo.h>


constexpr unsigned MAX_DEPTH = 200;

std::set<unsigned> requested_depths {20};

constexpr unsigned cycles_count = 1000000;


bool backtrace_symbols_test(const unsigned DEPTH)
{
  assert(DEPTH <= MAX_DEPTH);

  // Stop the recursive calls
  if (DEPTH == MAX_DEPTH)
  {
    return false;
  }

  // Continue with the recursive calls but do not test backtrace gather.
  if (requested_depths.count(DEPTH) == 0)
  {
    return true;
  }

  std::cout << "depth: " << DEPTH << std::endl;

  std::chrono::nanoseconds duration {0};

  unsigned used_size;

  // backtrace_symbols only
  for (unsigned i = 0; i < cycles_count; ++i)
  {
    void *buffer[MAX_DEPTH];

    used_size = backtrace(buffer, DEPTH);

    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

    char **bt_symbols = backtrace_symbols(buffer, used_size);

    duration += std::chrono::high_resolution_clock::now() - start;

    if (i == 0)
    {
      std::cout << "backtrace begin" << std::endl;

      for (unsigned n = 0; n < used_size; ++n)
      {
        std::cout << "backtrace (depth=" << n << "): " << bt_symbols[n] << std::endl;
      }

      std::cout << "backtrace end" << std::endl;
      std::cout << std::endl;
    }

    free(bt_symbols);

    assert(used_size == DEPTH);
  }

  std::cout << "backtrace_symbols only duration: "
            << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()
            << " ms  -  " << cycles_count << "x calls (backtrace depth " << used_size << ")" << std::endl;
  std::cout << std::endl;

  // backtrace + backtrace_symbols + free
  duration = decltype(duration)(0);

  for (unsigned i = 0; i < cycles_count; ++i)
  {
    void *buffer[MAX_DEPTH];

    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

    used_size = backtrace(buffer, DEPTH);

    char **bt_symbols = backtrace_symbols(buffer, used_size);

    free(bt_symbols);

    duration += std::chrono::high_resolution_clock::now() - start;

    assert(used_size == DEPTH);
  }

  std::cout << "(backtrace + backtrace_symbols + free) duration: "
            << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()
            << " ms  -  " << cycles_count << "x calls (backtrace depth " << used_size << ")" << std::endl;
  std::cout << std::endl;

  return true;
}


int main(int argc, char* argv[])
{
  if (argc > 1)
  {
    requested_depths.clear();
  }

  for (int i = 1; i < argc; ++i)
  {
    std::stringstream s;
    s << argv[i];

    unsigned requested_depth;
    s >> requested_depth;

    if (requested_depth >= MAX_DEPTH)
    {
      std::cerr << "ERROR: requested depth " << requested_depth
                << " is equal or greated than max depth " << MAX_DEPTH
                << std::endl;
      return 1;
    }

    requested_depths.insert(requested_depth);
  }

  std::cout << std::endl;
  std::cout << "Requested depths: {";
  std::copy(requested_depths.cbegin(), --requested_depths.cend(),
            std::ostream_iterator<unsigned>(std::cout, ", "));
  std::cout << *--requested_depths.cend();
  std::cout << "}" << std::endl;

  std::cout << std::endl;
  backtrace_symbols_test_a(0, backtrace_symbols_test);

  return 0;
}
