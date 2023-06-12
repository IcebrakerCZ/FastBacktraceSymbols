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

#define fatal(a, b) exit(1)
#define bfd_fatal(a) exit(1)
#define bfd_nonfatal(a) exit(1)
#define list_matching_formats(a) exit(1)

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>
#include <bfd.h>
#include <dlfcn.h>
#include <link.h>

#include <map>
#include <shared_mutex>
#include <string>
#include <tuple>
#include <vector>

struct BacktraceSymbols
{
  ~BacktraceSymbols()
  {
    free(static_syms);
    free(dynamic_syms);

    bfd_close(abfd);
  }

  std::string filename;
  bfd* abfd = NULL;

  asymbol **static_syms = NULL;
  asymbol **dynamic_syms = NULL;

  std::map<bfd_vma, std::string> addresses;

  std::vector<std::tuple<bfd_vma, bfd_size_type, asection*>> sections;


bool find_address_in_sections(bfd_vma pc, const char** filename, const char** functionname, unsigned int* line)
{
  for (const auto& [vma, size, section] : sections)
  {
    if (pc < vma || pc >= vma + size)
    {
      continue;
    }


    if (   (static_syms != NULL && bfd_find_nearest_line(abfd, section, static_syms, pc - vma, filename, functionname, line))
        || (dynamic_syms != NULL && bfd_find_nearest_line(abfd, section, dynamic_syms, pc - vma, filename, functionname, line))
       )
    {
      return true;
    }
  }

  return false;
}


static void save_section(bfd *abfd, asection *section, void *data)
{
  if ((bfd_section_flags(section) & SEC_ALLOC) == 0)
  {
    return;
  }

  bfd_vma vma = bfd_section_vma(section);

  bfd_size_type size = bfd_section_size(section);

  BacktraceSymbols& item = *((BacktraceSymbols*) data);

  item.sections.push_back(std::make_tuple(vma, size, section));
}


void slurp_sections()
{
  bfd_map_over_sections(abfd, save_section, this);
//  printf("BacktraceSymbols::slurp_sections: size=%lu\n", sections.size());
}

}; // struct BacktraceSymbols


/* Read in the symbol table.  */

static void slurp_symtab(BacktraceSymbols& item)
{
  if ((bfd_get_file_flags(item.abfd) & HAS_SYMS) == 0)
  {
    printf("backtrace_symbols.cpp::slurp_symtab: File '%s' have no symbols.\n", item.filename.c_str());
    return;
  }

  unsigned int size;
  long static_symcount  = bfd_read_minisymbols(item.abfd, false, (void**) ((PTR) & item.static_syms), &size);
  long dynamic_symcount = bfd_read_minisymbols(item.abfd, true /* dynamic */, (void**) ((PTR) & item.dynamic_syms), &size);

  if (static_symcount < 0 && dynamic_symcount < 0)
  {
    bfd_fatal(bfd_get_filename(item.abfd));
  }
}

static std::string translate_addresses_buf(BacktraceSymbols& item, bfd_vma addr)
{
  std::string result;

  const char* filename = NULL;
  const char* functionname = NULL;
  unsigned int line = 0;

  if (!item.find_address_in_sections(addr, &filename, &functionname, &line))
  {
    result += "[0x";
    result += std::to_string(addr);
    result += "] \?\?() \?\?:0";
    return result;
  }

  const char *demangled_functionname = NULL;

  if (functionname == NULL || functionname[0] == '\0')
  {
    demangled_functionname = "??";
  }
  else
  {
    demangled_functionname = bfd_demangle(item.abfd, functionname, 0);
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

  return result;
}
/* Process a file.  */

static std::string process_file(BacktraceSymbols& item, bfd_vma addr)
{
  if (item.abfd == NULL)
  {
    item.abfd = bfd_openr(item.filename.c_str(), NULL);

    if (item.abfd == NULL)
    {
      bfd_fatal(item.filename);
    }

    if (bfd_check_format(item.abfd, bfd_archive))
    {
      fatal("%s: can not get addresses from archive", item.filename);
    }

    char **matching;
    if (!bfd_check_format_matches(item.abfd, bfd_object, &matching))
    {
      bfd_nonfatal(bfd_get_filename(item.abfd));

      if (bfd_get_error() == bfd_error_file_ambiguously_recognized)
      {
        list_matching_formats(matching);
        free(matching);
      }

      exit(1);
    }
  }

  if (item.static_syms == NULL && item.dynamic_syms == NULL)
  {
    slurp_symtab(item);
    item.slurp_sections();
  }

  return translate_addresses_buf(item, addr);
}

struct file_match
{
  const char *file = NULL;
  void *address = NULL;
  void *base = NULL;
  void *hdr = NULL;
};

static int find_matching_file(struct dl_phdr_info *info, size_t size, void *data)
{
  struct file_match *match = (struct file_match *) data;
  /* This code is modeled from Gfind_proc_info-lsb.c:callback() from libunwind */
  long n;
  const ElfW(Phdr) *phdr;

  ElfW(Addr) load_base = info->dlpi_addr;

  phdr = info->dlpi_phdr;

  for (n = info->dlpi_phnum; --n >= 0; ++phdr)
  {
    if (phdr->p_type == PT_LOAD)
    {
      ElfW(Addr) vaddr = phdr->p_vaddr + load_base;
      if (match->address >= (void*) vaddr && match->address < (void*) ((char*) vaddr + phdr->p_memsz))
      {
        /* we found a match */
        match->file = info->dlpi_name;
        match->base = (void*) info->dlpi_addr;
        return 1;
      }
    }
  }

  return 0;
}


std::shared_mutex filenames_map_mutex;

std::map<std::string, BacktraceSymbols> filenames_map;


char **backtrace_symbols(void *const *buffer, int stack_depth)
{
  bfd_init();

  char **locations = (char**) malloc(stack_depth * sizeof(char*));

  for (int x = stack_depth - 1; x >= 0; --x)
  {
    struct file_match match;
    match.address = buffer[x];
    dl_iterate_phdr(find_matching_file, &match);

    bfd_vma addr = (char*) buffer[x] - (char*) match.base;

    const char* bin_name = "/proc/self/exe";
    if (match.file && strlen(match.file))
    {
      bin_name = match.file;
    }

    BacktraceSymbols* item = nullptr;
    {
      std::shared_lock lock(filenames_map_mutex);

      auto iter = filenames_map.find(bin_name);
      if (iter != filenames_map.end())
      {
        item = &iter->second;
      }
    }

    if (item == nullptr)
    {
      std::unique_lock lock(filenames_map_mutex);

      item = &filenames_map[bin_name];
      item->filename = bin_name;
    }


    std::string* symbol = nullptr;
    {
      std::shared_lock lock(filenames_map_mutex);

      auto iter = item->addresses.find(addr);
      if (iter != item->addresses.end())
      {
        symbol = &iter->second;
      }
    }

    if (symbol == nullptr)
    {
      std::unique_lock lock(filenames_map_mutex);

      symbol = &item->addresses[addr];

      if (symbol->empty())
      {
        *symbol = process_file(*item, addr);
      }
    }

    locations[x] = symbol->data();
  }

  return locations;
}
