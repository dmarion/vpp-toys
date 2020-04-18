/*
  Copyright (c) 2020 Damjan Marion

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <vppinfra/format.h>
#include <vppinfra/mem.h>
#include <vppinfra/time.h>

#include "perf.h"
#include "upstream.h"

u16 __clib_noinline
__clib_section (".store8_load16")
store8_load16 (u8 * buffer, u32 buffer_size, u32 count)
{
  u32 rv = 0;
  u32 mask = buffer_size - 1;

  for (int i = 0; i < count; i++)
    {
      u32 offset = (2 * i) & mask;

      /* single byte store */
      buffer[offset] = i;
      asm volatile ("":::"memory");
      /* two byte read from the same location */
      rv += *(u16 *) (buffer + offset);
      //rv += data[i] << 8 | data[i+1];
    }
  return rv;
}

u16 __clib_noinline
__clib_section (".store8_load8")
store8_load8 (u8 * buffer, u32 buffer_size, u32 count)
{
  u32 rv = 0;
  u32 mask = buffer_size - 1;

  for (int i = 0; i < count; i++)
    {
      u32 offset = (2 * i) & mask;

      /* single byte store */
      buffer[offset] = i;
      asm volatile ("":::"memory");
      /* two byte read from the same location */
      rv += buffer[offset] << 8 | buffer[offset + 1];
    }
  return rv;
}

int
main (int argc, char **argv)
{
  clib_error_t *err;
  u32 buffer_size = 1 << 13;	/* 8k, 25% of L1 cache */
  u32 count = 1 << 20;		/* 1M */
  u8 *buffer;

  perf_main_t _pm = {
    .events[0] = PERF_E_CPU_CLK_UNHALTED_THREAD_P,
    .events[1] = PERF_E_LD_BLOCKS_STORE_FORWARD,
    .n_events = 2,
    .n_snapshots = 3,
    .verbose = 2,
  }, *pm = &_pm;

  clib_mem_init (0, 8 << 20);
  if ((err = perf_init (pm)))
    {
      clib_error_report (err);
      clib_error_free (err);
      exit (1);
    }

  buffer = clib_mem_alloc_aligned (buffer_size, CLIB_CACHE_LINE_BYTES);

  /* initialize buffer and bring it into L1 cache */
  for (int i = 0; i < buffer_size; i++)
    buffer[i] = i;
  _mm_mfence ();

  perf_get_counters (pm);
  store8_load8 (buffer, buffer_size, count);
  perf_get_counters (pm);
  store8_load16 (buffer, buffer_size, count);
  perf_get_counters (pm);

  fformat (stdout, "\ntwo 8-bit loads after 8-bit store: \n  %U\n",
	   format_perf_counters_diff, pm, 0, 1);
  fformat (stdout, "  %lu ops, %.2f clocks / op\n", count,
	   (f64) perf_get_counter_diff (pm, 0, 0, 1) / count);
  fformat (stdout, "\none 16-bit load after 8-bit-store: \n  %U\n",
	   format_perf_counters_diff, pm, 1, 2);
  fformat (stdout, "  %lu ops, %.2f clocks / op\n", count,
	   (f64) perf_get_counter_diff (pm, 0, 1, 2) / count);
  fformat (stdout, "\nperformance hit: %.2f clocks/op\n",
	   (f64) (perf_get_counter_diff (pm, 0, 1, 2) -
	   perf_get_counter_diff (pm, 0, 0, 1)) / count);
  perf_free (pm);
}
