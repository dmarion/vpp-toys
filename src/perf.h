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

#include <linux/perf_event.h>
#include <sys/ioctl.h>

static char *perf_x86_event_counter_unit[] = {
  [0] = "",
  [1] = "instructions",
  [2] = "loads",
  [3] = "stores",
  [4] = "cycles",
};

#define PERF_INTEL_CODE(event, umask, edge, any, inv, cmask) \
  ((event) | (umask) << 8 | (edge) << 18 | (any) << 21 | (inv) << 23 |  (cmask) << 24)

/* EventCode, UMask, EdgeDetect, AnyThread, Invert, CounterMask
 * counter_unit, name, suffix, description */
#define foreach_perf_x86_event \
  _(0x00, 0x02, 0, 0, 0, 0x00, 4, CPU_CLK_UNHALTED, THREAD, \
    "Core cycles when the thread is not in halt state") \
  _(0x03, 0x02, 0, 0, 0, 0x00, 2, LD_BLOCKS, STORE_FORWARD, \
    "Loads blocked due to overlapping with a preceding store that cannot be" \
    " forwarded.") \
  _(0x28, 0x07, 0, 0, 0, 0x00, 4, CORE_POWER, LVL0_TURBO_LICENSE, \
    "Core cycles where the core was running in a manner where Turbo may be " \
    "clipped to the Non-AVX turbo schedule.") \
  _(0x28, 0x18, 0, 0, 0, 0x00, 4, CORE_POWER, LVL1_TURBO_LICENSE, \
    "Core cycles where the core was running in a manner where Turbo may be " \
    "clipped to the AVX2 turbo schedule.") \
  _(0x28, 0x20, 0, 0, 0, 0x00, 4, CORE_POWER, LVL2_TURBO_LICENSE, \
    "Core cycles where the core was running in a manner where Turbo may be " \
    "clipped to the AVX512 turbo schedule.") \
  _(0x28, 0x40, 0, 0, 0, 0x00, 4, CORE_POWER, THROTTLE, \
    "Core cycles the core was throttled due to a pending power level " \
    "request.") \
  _(0x3C, 0x00, 0, 0, 0, 0x00, 4, CPU_CLK_UNHALTED, THREAD_P, \
    "Thread cycles when thread is not in halt state") \
  _(0x3C, 0x00, 0, 1, 0, 0x00, 4, CPU_CLK_UNHALTED, THREAD_P_ANY, \
    "Core cycles when at least one thread on the physical core is not in " \
    "halt state.") \
  _(0x3C, 0x00, 1, 0, 0, 0x01, 0, CPU_CLK_UNHALTED, RING0_TRANS, \
    "Counts when there is a transition from ring 1, 2 or 3 to ring 0.") \
  _(0xD0, 0x81, 0, 0, 0, 0x00, 2, MEM_INST_RETIRED, ALL_LOADS, \
    "All retired load instructions.") \
  _(0xD0, 0x82, 0, 0, 0, 0x00, 3, MEM_INST_RETIRED, ALL_STORES, \
    "All retired store instructions.") \
  _(0xD1, 0x01, 0, 0, 0, 0x00, 2, MEM_LOAD_RETIRED, L1_HIT, \
    "Retired load instructions with L1 cache hits as data sources") \
  _(0xD1, 0x02, 0, 0, 0, 0x00, 2, MEM_LOAD_RETIRED, L2_HIT, \
    "Retired load instructions with L2 cache hits as data sources") \
  _(0xD1, 0x04, 0, 0, 0, 0x00, 2, MEM_LOAD_RETIRED, L3_HIT, \
    "Retired load instructions with L3 cache hits as data sources") \
  _(0xD1, 0x08, 0, 0, 0, 0x00, 2, MEM_LOAD_RETIRED, L1_MISS, \
    "Retired load instructions missed L1 cache as data sources") \
  _(0xD1, 0x10, 0, 0, 0, 0x00, 2, MEM_LOAD_RETIRED, L2_MISS, \
    "Retired load instructions missed L2 cache as data sources") \
  _(0xD1, 0x20, 0, 0, 0, 0x00, 2, MEM_LOAD_RETIRED, L3_MISS, \
    "Retired load instructions missed L3 cache as data sources") \
  _(0xD1, 0x40, 0, 0, 0, 0x00, 2, MEM_LOAD_RETIRED, FB_HIT, \
    "Retired load instructions which data sources were load missed L1 but " \
    "hit FB due to preceding miss to the same cache line with data not " \
    "ready") \
  _(0xD2, 0x01, 0, 0, 0, 0x00, 2, MEM_LOAD_L3_HIT_RETIRED, XSNP_MISS, \
    "Retired load instructions which data sources were L3 hit and cross-" \
    "core snoop missed in on-pkg core cache.") \
  _(0xD2, 0x02, 0, 0, 0, 0x00, 2, MEM_LOAD_L3_HIT_RETIRED, XSNP_HIT, \
    "Retired load instructions which data sources were L3 and cross-core " \
    "snoop hits in on-pkg core cache") \
  _(0xD2, 0x04, 0, 0, 0, 0x00, 2, MEM_LOAD_L3_HIT_RETIRED, XSNP_HITM, \
    "Retired load instructions which data sources were HitM responses from " \
    "shared L3") \
  _(0xD2, 0x08, 0, 0, 0, 0x00, 2, MEM_LOAD_L3_HIT_RETIRED, XSNP_NONE, \
    "Retired load instructions which data sources were hits in L3 without " \
    "snoops required") \
  _(0xD3, 0x01, 0, 0, 0, 0x00, 2, MEM_LOAD_L3_MISS_RETIRED, LOCAL_DRAM, \
    "Retired load instructions which data sources missed L3 but serviced " \
    "from local dram") \
  _(0xD3, 0x02, 0, 0, 0, 0x00, 2, MEM_LOAD_L3_MISS_RETIRED, REMOTE_DRAM, \
    "Retired load instructions which data sources missed L3 but serviced " \
    "from remote dram") \
  _(0xD3, 0x04, 0, 0, 0, 0x00, 2, MEM_LOAD_L3_MISS_RETIRED, REMOTE_HITM, \
    "Retired load instructions whose data sources was remote HITM") \
  _(0xD3, 0x08, 0, 0, 0, 0x00, 2, MEM_LOAD_L3_MISS_RETIRED, REMOTE_FWD, \
    "Retired load instructions whose data sources was forwarded from a " \
    "remote cache") \

typedef enum
{
#define _(event, umask, edge, any, inv, cmask, unit, name, suffix, desc) \
    PERF_E_##name##_##suffix = PERF_INTEL_CODE(event, umask, edge, any, inv, cmask),
  foreach_perf_x86_event
#undef _
} perf_x86_event_type_t;

typedef struct
{
  u64 code;
  char *name;
  u8 unit;
} perf_x86_event_data_t;

static format_function_t format_perf_event_name;
static format_function_t format_perf_event_unit;
static format_function_t format_perf_counters;

static perf_x86_event_data_t perf_x86_event_data[] = {
#define _(event, umask, edge, any, inv, cmask, unit, name, suffix, desc) \
      {PERF_INTEL_CODE(event, umask, edge, any, inv, cmask), #name "." #suffix, unit},
  foreach_perf_x86_event
#undef _
  {},
};

typedef struct
{
#define PERF_MAX_EVENTS 6	/* 2 fixed and 4 programmable */
  u64 events[PERF_MAX_EVENTS];
  int n_events;
  int group_fd;
  struct perf_event_mmap_page *mmap_pages[PERF_MAX_EVENTS];
  u8 verbose;
} perf_main_t;

static inline clib_error_t *
perf_init (perf_main_t * pm)
{
  clib_error_t *err = 0;
  int page_size = getpagesize ();

  pm->group_fd = -1;

  for (int i = 0; i < pm->n_events; i++)
    pm->mmap_pages[i] = MAP_FAILED;

  for (int i = 0; i < pm->n_events; i++)
    {
      int fd;

      struct perf_event_attr pe = {
	.size = sizeof (struct perf_event_attr),
	.type = PERF_TYPE_RAW,
	.config = pm->events[i],
	.disabled = 1,
	.exclude_kernel = 1,
	.exclude_hv = 1,
      };

      fd = syscall (__NR_perf_event_open, &pe, /* pid */ 0, /* cpu */ -1,
		    /* group_fd */ pm->group_fd, /* flags */ 0);

      if (fd == -1)
	{
	  err = clib_error_return_unix (0, "perf_event_open");
	  goto error;
	}

      if (i == 0)
	pm->group_fd = fd;

      pm->mmap_pages[i] = mmap (0, page_size, PROT_READ, MAP_SHARED, fd, 0);

      if (pm->mmap_pages[i] == MAP_FAILED)
	{
	  err = clib_error_return_unix (0, "mmap");
	  goto error;
	}
    }

  if (ioctl (pm->group_fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1)
    {
      err = clib_error_return_unix (0, "ioctl(PERF_EVENT_IOC_ENABLE)");
      goto error;
    }

  if (pm->verbose >= 2)
    for (int i = 0; i < pm->n_events; i++)
      {
	u8 v;
	fformat (stderr, "event %u: %U (event=0x%02x, umask=0x%02x",
		 i, format_perf_event_name, pm, i,
		 pm->events[i] & 0xff, (pm->events[i] >> 8) & 0xff);
	if ((v = (pm->events[i] >> 18) & 1))
	  fformat (stderr, ", edge=%u", v);
	if ((v = (pm->events[i] >> 19) & 1))
	  fformat (stderr, ", pc=%u", v);
	if ((v = (pm->events[i] >> 21) & 1))
	  fformat (stderr, ", any=%u", v);
	if ((v = (pm->events[i] >> 23) & 1))
	  fformat (stderr, ", inv=%u", v);
	if ((v = (pm->events[i] >> 24) & 0xff))
	  fformat (stderr, ", cmask=0x%02x", v);
	fformat (stderr, ") hw counter id 0x%x\n",
		 pm->mmap_pages[i]->index + pm->mmap_pages[i]->offset);
      }

  return 0;
error:
  for (int i = 0; i < pm->n_events; i++)
    if (pm->mmap_pages[i] != MAP_FAILED)
      munmap (pm->mmap_pages[i], page_size);
  if (pm->group_fd != -1)
    close (pm->group_fd);
  return err;
}

static inline void
perf_free (perf_main_t * pm)
{
  int page_size = getpagesize ();
  ioctl (pm->group_fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
  for (int i = 0; i < pm->n_events; i++)
    munmap (pm->mmap_pages[i], page_size);
  close (pm->group_fd);
}

static_always_inline void
perf_get_counters (perf_main_t * pm, u64 * counters)
{
  asm volatile ("":::"memory");
  for (int i = 0; i < clib_min (pm->n_events, PERF_MAX_EVENTS); i++)
    counters[i] = _rdpmc (pm->mmap_pages[i]->index +
			  pm->mmap_pages[i]->offset);
  asm volatile ("":::"memory");
}

static u8 *
format_perf_event_name (u8 * s, va_list * args)
{
  perf_main_t *pm = va_arg (*args, perf_main_t *);
  u32 event_index = va_arg (*args, u32);
  perf_x86_event_data_t *d = perf_x86_event_data;

  while (d->name)
    {
      if (pm->events[event_index] == d->code)
	return format (s, "%s", d->name);
      d++;
    }

  return format (s, "UNKNOWN-0x%04lx", pm->events[event_index]);
}

static u8 *
format_perf_event_unit (u8 * s, va_list * args)
{
  perf_main_t *pm = va_arg (*args, perf_main_t *);
  u32 event_index = va_arg (*args, u32);
  perf_x86_event_data_t *d = perf_x86_event_data;

  while (d->name)
    {
      if (pm->events[event_index] == d->code)
	return format (s, "%s", perf_x86_event_counter_unit[d->unit]);
      d++;
    }

  return s;
}

static __clib_unused u8 *
format_perf_counters (u8 * s, va_list * args)
{
  perf_main_t *pm = va_arg (*args, perf_main_t *);
  u64 *counters = va_arg (*args, u64 *);
  u32 indent = format_get_indent (s);

  for (int i = 0; i < pm->n_events; i++)
    {
      s = format (s, "%s%U%-40U%11lu %U", i ? "\n" : "",
		  format_white_space, i ? indent : 0,
		  format_perf_event_name, pm, i,
		  counters[i], format_perf_event_unit, pm, i);
    }
  return s;
}
