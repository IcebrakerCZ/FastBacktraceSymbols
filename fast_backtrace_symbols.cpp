/*
  Improved performance by caching demangled symbols info.

  Copyright 2023 Jan Horak
*/

/*
  A hacky replacement for backtrace_symbols in glibc

  backtrace_symbols in glibc looks up symbols using dladdr which is limited in
  the symbols that it sees. libbacktracesymbols opens the executable and shared
  libraries using libbfd and will look up backtrace information using the symbol
  table and the dwarf line information.

  It may make more sense for this program to use libelf instead of libbfd.
  However, I have not investigated that yet.

  Derived from addr2line.c from GNU Binutils by Jeff Muizelaar

  Copyright 2007 Jeff Muizelaar
*/

/* addr2line.c -- convert addresses to line number and function name
   Copyright 1997, 1998, 1999, 2000, 2001, 2002 Free Software Foundation, Inc.
   Contributed by Ulrich Lauther <Ulrich.Lauther@mchp.siemens.de>

   This file was part of GNU Binutils.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#define fatal(a, b) abort()
#define bfd_fatal(a) abort()
#define bfd_nonfatal(a) abort()
#define list_matching_formats(a) abort()

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>
#include <bfd.h>
#include <dlfcn.h>
#include <link.h>
#include <unwind.h>

#include <map>
#include <shared_mutex>
#include <string>
#include <tuple>
#include <vector>


namespace
{

static std::shared_mutex backtrace_mutex;


class BacktraceSymbols
{
public:

  ~BacktraceSymbols()
  {
    free(m_static_syms);
    free(m_dynamic_syms);

    if (m_abfd)
    {
      bfd_close(m_abfd);
    }
  }


  bool initialize(const char* bin_name, void* addrs_base)
  {
    if (m_abfd != NULL)
    {
      return true;
    }

    m_filename = bin_name;
    m_addrs_base = addrs_base;

    m_abfd = bfd_openr(m_filename.c_str(), NULL);

    if (m_abfd == NULL)
    {
      return false;
    }

    if (bfd_check_format(m_abfd, bfd_archive))
    {
      fatal("%s: can not get addresses from archive", m_filename.c_str());
    }

    char **matching;
    if (!bfd_check_format_matches(m_abfd, bfd_object, &matching))
    {
      bfd_nonfatal(bfd_get_filename(m_abfd));

      if (bfd_get_error() == bfd_error_file_ambiguously_recognized)
      {
        list_matching_formats(matching);
        free(matching);
      }

      abort();
    }

    if ((bfd_get_file_flags(m_abfd) & HAS_SYMS) == 0)
    {
      //printf("backtrace_symbols.cpp::slurp_symtab: File '%s' have no symbols.\n", m_filename.c_str());
      return true;
    }

    unsigned int size;
    long static_symcount  = bfd_read_minisymbols(m_abfd, false, (void**) ((PTR) & m_static_syms) , &size);
    long dynamic_symcount = bfd_read_minisymbols(m_abfd, true , (void**) ((PTR) & m_dynamic_syms), &size);

    if (static_symcount < 0 || dynamic_symcount < 0)
    {
      bfd_fatal(bfd_get_filename(m_abfd));
    }

    bfd_map_over_sections(m_abfd, SaveSectionCallback, this);

    return true;
  }


  const std::string& DemangleSymbol(void* symbol_addr)
  {
    bfd_vma addr = (char*) symbol_addr - (char*) m_addrs_base;

    {
      std::shared_lock lock(backtrace_mutex);

      auto iter = m_addresses.find(addr);
      if (iter != m_addresses.end())
      {
        return iter->second;
      }
    }

    std::unique_lock lock(backtrace_mutex);

    auto iter = m_addresses.find(addr);
    if (iter != m_addresses.end())
    {
      return iter->second;
    }

    return UpdateAddress(addr);
  }


private:

  bool FindAddressInSections(bfd_vma pc, const char** filename, const char** functionname, unsigned int* line)
  {
    for (const auto& [vma, size, section] : m_sections)
    {
      if (pc < vma || pc >= vma + size)
      {
        continue;
      }


      if (   (   m_static_syms != NULL
              && bfd_find_nearest_line(m_abfd, section, m_static_syms, pc - vma, filename, functionname, line)
             )
          || (   m_dynamic_syms != NULL
              && bfd_find_nearest_line(m_abfd, section, m_dynamic_syms, pc - vma, filename, functionname, line)
             )
         )
      {
        return true;
      }
    }

    return false;
  }


  const std::string& UpdateAddress(bfd_vma addr)
  {
    std::string result;

    const char* filename = NULL;
    const char* functionname = NULL;
    unsigned int line = 0;

    if (!FindAddressInSections(addr, &filename, &functionname, &line))
    {
      result += "[0x";
      result += std::to_string(addr);
      result += "] \?\?() \?\?:0";

      return m_addresses.emplace(std::make_pair(addr, std::move(result))).first->second;
    }

    const char *demangled_functionname = NULL;

    if (functionname == NULL || functionname[0] == '\0')
    {
      demangled_functionname = "??";
    }
    else
    {
      demangled_functionname = bfd_demangle(m_abfd, functionname, 0);
      if (demangled_functionname == NULL)
      {
        demangled_functionname = functionname;
      }
    }

    if (filename == NULL || filename[0] == '\0')
    {
      filename = "??";
    }
    else
    {
      char *h = (char*) strrchr(filename, '/');
      if (h != NULL)
      {
        filename = h + 1;
      }
    }

    result += filename;
    result += ":";
    result += std::to_string(line);
    result += "\t";
    result += demangled_functionname;

    return m_addresses.emplace(std::make_pair(addr, std::move(result))).first->second;
  }


  static void SaveSectionCallback(bfd *abfd, asection *section, void *data)
  {
    if ((bfd_section_flags(section) & SEC_ALLOC) == 0)
    {
      return;
    }

    bfd_vma vma = bfd_section_vma(section);

    bfd_size_type size = bfd_section_size(section);

    BacktraceSymbols* item = (BacktraceSymbols*) data;

    item->m_sections.push_back(std::make_tuple(vma, size, section));
  }


public:

  std::string m_filename;
  void* m_addrs_base = NULL;

  bfd* m_abfd = NULL;

  asymbol **m_static_syms = NULL;
  asymbol **m_dynamic_syms = NULL;

  std::map<bfd_vma, std::string> m_addresses;

  std::vector<std::tuple<bfd_vma, bfd_size_type, asection*>> m_sections;

}; // struct BacktraceSymbols


class BacktraceFiles
{
public:

  BacktraceFiles()
  {
    bfd_init();

    dl_iterate_phdr(FindMatchingFileCallback, &m_symbol_intervals);
  }


  const std::string& FindMatchingSymbol(void* symbol_addr)
  {
    BacktraceSymbols& item = FindMatchingFile(symbol_addr);

    return item.DemangleSymbol(symbol_addr);
  }


private:

  struct SymbolInterval
  {
    void* min;
    void* max;

    // Note: We know that the intervals can't overlap.
    bool operator<(const SymbolInterval& other) const
    {
      return min < other.min && max <= other.max;
    }
  };

  BacktraceSymbols& FindMatchingFile(void* symbol_addr)
  {
    {
      std::shared_lock lock(backtrace_mutex);

      auto iter = m_symbol_intervals.find({symbol_addr, symbol_addr});
      if (iter != m_symbol_intervals.end())
      {
        return *iter->second;
      }
    }

    std::unique_lock lock(backtrace_mutex);

    dl_iterate_phdr(FindMatchingFileCallback, this);

    auto iter = m_symbol_intervals.find({symbol_addr, symbol_addr});
    if (iter != m_symbol_intervals.end())
    {
      return *iter->second;
    }

    printf("Symbol %p not found in the process.\n", symbol_addr);
    abort();
  }


  static int FindMatchingFileCallback(dl_phdr_info *info, size_t size, void *data)
  {
    BacktraceFiles& backtrace_files = *(BacktraceFiles*) data;

    /* This code is modeled from Gfind_proc_info-lsb.c:callback() from libunwind */
    ElfW(Addr) load_base = info->dlpi_addr;

    const ElfW(Phdr) *phdr = info->dlpi_phdr;

    for (long n = info->dlpi_phnum; --n >= 0; ++phdr)
    {
      if (phdr->p_type != PT_LOAD)
      {
        continue;
      }

      const char* bin_name = "/proc/self/exe";
      if (info->dlpi_name && strlen(info->dlpi_name))
      {
        bin_name = info->dlpi_name;
      }

      BacktraceSymbols& result = backtrace_files.m_filenames[bin_name];

      if (!result.initialize(bin_name, (void*) info->dlpi_addr))
      {
        backtrace_files.m_filenames.erase(bin_name);
        continue;
      }

      SymbolInterval symbol_interval {.min = (void*) (phdr->p_vaddr + load_base),
                                      .max = (void*) (phdr->p_vaddr + load_base + phdr->p_memsz)};

      //printf("{%s, %p} -> {%p, %p}\n", bin_name, (void*) info->dlpi_addr, symbol_interval.min, symbol_interval.max);
      backtrace_files.m_symbol_intervals[symbol_interval] = &result;
    }

    return 0;
  }


private:

  std::map<SymbolInterval, BacktraceSymbols*> m_symbol_intervals;

  std::map<std::string, BacktraceSymbols> m_filenames;

}; // class BacktraceFiles


static BacktraceFiles backtrace_files;


} // anonymous namespace


extern "C"
{
  char **backtrace_symbols(void *const *buffer, int stack_depth);
}


char **backtrace_symbols(void *const *buffer, int stack_depth)
{
  char ** locations = (char**) malloc(stack_depth * sizeof(char*));

  for (int x = stack_depth - 1; x >= 0; --x)
  {
    locations[x] = (char*) backtrace_files.FindMatchingSymbol(buffer[x]).c_str();
  }

  return locations;
}


struct trace_arg
{
  void **array;
  _Unwind_Word cfa;
  int cnt;
  int size;
};


static inline void *
unwind_arch_adjustment (void *prev, void *addr)
{
  return addr;
}


static _Unwind_Reason_Code
backtrace_helper (struct _Unwind_Context *ctx, void *a)
{
  struct trace_arg *arg = (struct trace_arg *) a;

  /* We are first called with address in the __backtrace function.
     Skip it.  */
  if (arg->cnt != -1)
    {
      arg->array[arg->cnt] = (void *) _Unwind_GetIP (ctx);
      if (arg->cnt > 0)
	arg->array[arg->cnt]
	  = unwind_arch_adjustment (arg->array[arg->cnt - 1],
				    arg->array[arg->cnt]);

      /* Check whether we make any progress.  */
      _Unwind_Word cfa = _Unwind_GetCFA (ctx);

      if (arg->cnt > 0 && arg->array[arg->cnt - 1] == arg->array[arg->cnt]
	 && cfa == arg->cfa)
       return _URC_END_OF_STACK;
      arg->cfa = cfa;
    }
  if (++arg->cnt == arg->size)
    return _URC_END_OF_STACK;
  return _URC_NO_REASON;
}


char **fast_backtrace_symbols(void **array, int size, int* used_size)
{
  if (size <= 0)
  {
    return NULL;
  }

  struct trace_arg arg = {.array = array, .cfa = 0, .cnt = -1 , .size = size};

  _Unwind_Backtrace(backtrace_helper, &arg);

  /* _Unwind_Backtrace seems to put NULL address above
     _start.  Fix it up here.  */
  if (arg.cnt > 1 && arg.array[arg.cnt - 1] == NULL)
  {
    --arg.cnt;
  }

  if (arg.cnt == -1)
  {
    return NULL;
  }

  *used_size = arg.cnt;

  return backtrace_symbols(array, *used_size);
}
