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
  [5] = "transitions",
  [6] = "uops",
  [7] = "cachelines",
};

#define PERF_INTEL_CODE(event, umask, edge, any, inv, cmask) \
  ((event) | (umask) << 8 | (edge) << 18 | (any) << 21 | (inv) << 23 |  (cmask) << 24)

/* EventCode, UMask, EdgeDetect, AnyThread, Invert, CounterMask
 * counter_unit, name, suffix, description */
#define foreach_perf_x86_event \
  _(0x00, 0x02, 0, 0, 0, 0x00, 4, CPU_CLK_UNHALTED, THREAD, \
    "Core cycles when the thread is not in halt state") \
  _(0x00, 0x03, 0, 0, 0, 0x00, 4, CPU_CLK_UNHALTED, REF_TSC, \
    "Reference cycles when the core is not in halt state.") \
  _(0x03, 0x02, 0, 0, 0, 0x00, 2, LD_BLOCKS, STORE_FORWARD, \
    "Loads blocked due to overlapping with a preceding store that cannot be" \
    " forwarded.") \
  _(0x08, 0x01, 0, 0, 0, 0x00, 2, DTLB_LOAD_MISSES, MISS_CAUSES_A_WALK, \
    "Load misses in all DTLB levels that cause page walks") \
  _(0x08, 0x02, 0, 0, 0, 0x00, 2, DTLB_LOAD_MISSES, WALK_COMPLETED_4K, \
    "Page walk completed due to a demand data load to a 4K page") \
  _(0x08, 0x04, 0, 0, 0, 0x00, 2, DTLB_LOAD_MISSES, WALK_COMPLETED_2M_4M, \
    "Page walk completed due to a demand data load to a 2M/4M page") \
  _(0x08, 0x08, 0, 0, 0, 0x00, 2, DTLB_LOAD_MISSES, WALK_COMPLETED_1G, \
    "Page walk completed due to a demand data load to a 1G page") \
  _(0x08, 0x0E, 0, 0, 0, 0x00, 2, DTLB_LOAD_MISSES, WALK_COMPLETED, \
    "Load miss in all TLB levels causes a page walk that completes. (All " \
    "page sizes)") \
  _(0x08, 0x10, 0, 0, 0, 0x00, 4, DTLB_LOAD_MISSES, WALK_PENDING, \
    "Counts 1 per cycle for each PMH that is busy with a page walk for a " \
    "load. EPT page walk duration are excluded in Skylake.") \
  _(0x08, 0x20, 0, 0, 0, 0x00, 2, DTLB_LOAD_MISSES, STLB_HIT, \
    "Loads that miss the DTLB and hit the STLB.") \
  _(0x0D, 0x01, 0, 0, 0, 0x00, 0, INT_MISC, RECOVERY_CYCLES, \
    "Core cycles the allocator was stalled due to recovery from earlier " \
    "clear event for this thread (e.g. misprediction or memory nuke)") \
  _(0x0E, 0x01, 0, 0, 0, 0x00, 6, UOPS_ISSUED, ANY, \
    "Uops that Resource Allocation Table (RAT) issues to Reservation " \
    "Station (RS)") \
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
  _(0x3C, 0x00, 1, 0, 0, 0x01, 5, CPU_CLK_UNHALTED, RING0_TRANS, \
    "Counts when there is a transition from ring 1, 2 or 3 to ring 0.") \
  _(0x48, 0x01, 0, 0, 0, 0x01, 4, L1D_PEND_MISS, PENDING_CYCLES, \
    "Cycles with L1D load Misses outstanding.") \
  _(0x48, 0x01, 0, 0, 0, 0x00, 4, L1D_PEND_MISS, PENDING, \
    "L1D miss outstandings duration in cycles") \
  _(0x48, 0x02, 0, 0, 0, 0x00, 0, L1D_PEND_MISS, FB_FULL, \
    "Number of times a request needed a FB entry but there was no entry " \
    "available for it. That is the FB unavailability was dominant reason " \
    "for blocking the request. A request includes cacheable/uncacheable " \
    "demands that is load, store or SW prefetch.") \
  _(0x51, 0x01, 0, 0, 0, 0x00, 0, L1D, REPLACEMENT, \
    "L1D data line replacements") \
  _(0x51, 0x04, 0, 0, 0, 0x00, 0, L1D, M_EVICT, \
    "L1D data line evictions") \
  _(0x83, 0x02, 0, 0, 0, 0x00, 0, ICACHE_64B, IFTAG_MISS, \
    "Instruction fetch tag lookups that miss in the instruction cache " \
    "(L1I). Counts at 64-byte cache-line granularity.") \
  _(0x9C, 0x01, 0, 0, 0, 0x00, 0, IDQ_UOPS_NOT_DELIVERED, CORE, \
    "Uops not delivered to Resource Allocation Table (RAT) per thread when " \
    "backend of the machine is not stalled") \
  _(0xC0, 0x00, 0, 0, 0, 0x00, 1, INST_RETIRED, ANY_P, \
    "Number of instructions retired. General Counter - architectural event") \
  _(0xC2, 0x02, 0, 0, 0, 0x00, 0, UOPS_RETIRED, RETIRE_SLOTS, \
    "Retirement slots used.") \
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
  _(0xF0, 0x40, 0, 0, 0, 0x00, 7, L2_TRANS, L2_WB, \
    "L2 writebacks that access L2 cache") \
  _(0xF1, 0x1F, 0, 0, 0, 0x00, 7, L2_LINES_IN, ALL, \
    "L2 cache lines filling L2") \
  _(0xFE, 0x02, 0, 0, 0, 0x00, 7, IDI_MISC, WB_UPGRADE, \
    "Counts number of cache lines that are allocated and written back to L3" \
    " with the intention that they are more likely to be reused shortly") \
  _(0xFE, 0x04, 0, 0, 0, 0x00, 7, IDI_MISC, WB_DOWNGRADE, \
    "Counts number of cache lines that are dropped and not written back to " \
    "L3 as they are deemed to be less likely to be reused shortly") \

typedef enum
{
#define _(event, umask, edge, any, inv, cmask, unit, name, suffix, desc) \
    PERF_E_##name##_##suffix,
  foreach_perf_x86_event
#undef _
    PERF_E_N_EVENTS,
} perf_event_type_t;

typedef enum
{
  PERF_B_NONE = 0,
  PERF_B_MEM_LOAD_RETIRED_HIT_MISS,
  PERF_B_INST_PER_CYCLE,
  PERF_B_DTLB_LOAD_MISSES,
  PERF_B_TOP_DOWN,
} perf_bundle_t;

typedef struct
{
  u64 code;
  char *name;
  char *suffix;
  u8 unit;
} perf_event_data_t;

static format_function_t format_perf_counters;

static perf_event_data_t perf_event_data[PERF_E_N_EVENTS] = {
#define _(event, umask, edge, any, inv, cmask, unit, name, suffix, desc) \
      {PERF_INTEL_CODE(event, umask, edge, any, inv, cmask), #name, #suffix, unit},
  foreach_perf_x86_event
#undef _
};

typedef struct
{
#define PERF_MAX_EVENTS 7	/* 3 fixed and 4 programmable */
  u64 events[PERF_MAX_EVENTS];
  int n_events;
  int group_fd;
  struct perf_event_mmap_page *mmap_pages[PERF_MAX_EVENTS];
  u8 verbose;
  u32 n_snapshots;
  u32 n_ops;
  u64 *counters;
  u64 *next_counter;
  format_function_t *bundle_format_fn;
} perf_main_t;

#include <vppinfra/cpu.h>

static inline u32
get_base_freq ()
{
  u32 eax = 0, ebx = 0, ecx = 0, edx = 0;
  __get_cpuid (0, &eax, &ebx, &ecx, &edx);
  if (eax >= 0x15)
    {
      u32 max_leaf = eax;
      /*
         CPUID Leaf 0x15 - Time Stamp Counter and Nominal Core Crystal Clock Info
         eax - denominator of the TSC/”core crystal clock” ratio
         ebx - numerator of the TSC/”core crystal clock” ratio
         ecx - nominal frequency of the core crystal clock in Hz
         edx - reseved
       */

      __get_cpuid (0x15, &eax, &ebx, &ecx, &edx);
      if (ebx && ecx)
	return ((u64) ecx * ebx / eax) / 1000000;

      if (max_leaf >= 0x16)
	{
	  /*
	     CPUID Leaf 0x16 - Processor Frequency Information Leaf
	     eax - Bits 15 - 00: Processor Base Frequency (in MHz).
	   */

	  __get_cpuid (0x16, &eax, &ebx, &ecx, &edx);
	  if (eax)
	    return (eax & 0xffff);
	}
    }
  return 0;
}

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
	.config = perf_event_data[pm->events[i]].code,
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
    fformat (stderr, "Base Frequency: %lu MHz\n", get_base_freq ());
  if (pm->verbose >= 2)
    for (int i = 0; i < pm->n_events; i++)
      {
	u8 v;
	perf_event_data_t *d = perf_event_data + pm->events[i];
	u64 code = d->code;

	fformat (stderr, "event %u: %s.%s (event=0x%02x, umask=0x%02x",
		 i, d->name, d->suffix, code & 0xff, (code >> 8) & 0xff);
	if ((v = (code >> 18) & 1))
	  fformat (stderr, ", edge=%u", v);
	if ((v = (code >> 19) & 1))
	  fformat (stderr, ", pc=%u", v);
	if ((v = (code >> 21) & 1))
	  fformat (stderr, ", any=%u", v);
	if ((v = (code >> 23) & 1))
	  fformat (stderr, ", inv=%u", v);
	if ((v = (code >> 24) & 0xff))
	  fformat (stderr, ", cmask=0x%02x", v);
	fformat (stderr, ") hw counter id 0x%x\n",
		 pm->mmap_pages[i]->index + pm->mmap_pages[i]->offset);
      }

  if (pm->n_snapshots < 2)
    pm->n_snapshots = 2;

  vec_validate_aligned (pm->counters, pm->n_snapshots * pm->n_events,
			CLIB_CACHE_LINE_BYTES);

  pm->next_counter = pm->counters;

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
  vec_free (pm->counters);
  ioctl (pm->group_fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
  for (int i = 0; i < pm->n_events; i++)
    munmap (pm->mmap_pages[i], page_size);
  close (pm->group_fd);
}

static_always_inline void
perf_get_counters (perf_main_t * pm)
{
  int i;
  asm volatile ("":::"memory");
  for (i = 0; i < clib_min (pm->n_events, PERF_MAX_EVENTS); i++)
    pm->next_counter[i] = _rdpmc (pm->mmap_pages[i]->index +
				  pm->mmap_pages[i]->offset);
  pm->next_counter[i] = __rdtsc ();
  pm->next_counter += pm->n_events + 1;
  asm volatile ("":::"memory");
}


u64
perf_get_counter_diff (perf_main_t * pm, int event_index, int a, int b)
{
  u64 *c = pm->counters + b * (pm->n_events + 1);
  u64 *p = pm->counters + a * (pm->n_events + 1);
  return c[event_index] - p[event_index];
}

u64
perf_get_tsc_diff (perf_main_t * pm, int a, int b)
{
  return perf_get_counter_diff (pm, pm->n_events, a, b);
}

static __clib_unused u8 *
format_perf_counters_diff (u8 * s, va_list * args)
{
  perf_main_t *pm = va_arg (*args, perf_main_t *);
  int a = va_arg (*args, int);
  int b = va_arg (*args, int);
  u8 *t = 0;

  s = format (s, "\n");
  for (int i = 0; i < pm->n_events; i++)
    s = format (s, "%20s", perf_event_data[pm->events[i]].name);
  s = format (s, "\n");
  for (int i = 0; i < pm->n_events; i++)
    s = format (s, "%20s", perf_event_data[pm->events[i]].suffix);
  s = format (s, "\n");
  for (int i = 0; i < pm->n_events; i++)
    {
      int unit = perf_event_data[pm->events[i]].unit;
      t = format (t, "(%s)%c", perf_x86_event_counter_unit[unit], 0);
      s = format (s, "%20s", t);
      vec_reset_length (t);
    }
  s = format (s, "\n");

  if (a == b)
    {
      for (int j = 1; j < pm->n_snapshots; j++)
	{
	  for (int i = 0; i < pm->n_events; i++)
	    s = format (s, "%20lu", perf_get_counter_diff (pm, i, j - 1, j));
	  s = format (s, "\n");
	}
    }
  else
    {
      for (int i = 0; i < pm->n_events; i++)
	s = format (s, "%20lu", perf_get_counter_diff (pm, i, a, b));
      s = format (s, "\n");
    }
  vec_free (t);

  return s;
}

static __clib_unused u8 *
format_perf_counters (u8 * s, va_list * args)
{
  perf_main_t *pm = va_arg (*args, perf_main_t *);
  u32 base_freq = get_base_freq ();
  u64 duration = perf_get_tsc_diff (pm, 0, pm->n_snapshots - 1);

  s = format (s, "Duration: %u ticks, %.2f msec\n",
	      duration, (f64) duration / (1e3 * base_freq));

  s = format (s, "%U\n", format_perf_counters_diff, pm, 0, 0);

  if (pm->bundle_format_fn)
    s = format (s, "\n%U", pm->bundle_format_fn, pm);
  return s;
}

static u8 *
format_perf_b_mem_load_retired_hit_miss (u8 * s, va_list * args)
{
  perf_main_t *pm = va_arg (*args, perf_main_t *);
  u64 l1miss = perf_get_counter_diff (pm, 1, 0, 1);
  u64 l2miss = perf_get_counter_diff (pm, 2, 0, 1);
  u64 l3miss = perf_get_counter_diff (pm, 3, 0, 1);
  u64 l1hit = perf_get_counter_diff (pm, 0, 0, 1);
  u64 l2hit = l1miss - l2miss;
  u64 l3hit = l2miss - l3miss;

  s = format (s, "Cache  %10s%10s%8s%8s\n",
	      "hits", "misses", "miss %", "miss/op");
  s = format (s, "L1     %10lu%10lu%8.2f%8.2f\n",
	      l1hit, l1miss, (f64) (100 * l1miss) / (l1hit + l1miss),
	      (f64) l1miss / pm->n_ops);
  s = format (s, "L2     %10lu%10lu%8.2f%8.2f\n",
	      l2hit, l2miss, (f64) (100 * l2miss) / (l2hit + l2miss),
	      (f64) l2miss / pm->n_ops);
  s = format (s, "L3     %10lu%10lu%8.2f%8.2f\n",
	      l3hit, l3miss, (f64) (100 * l3miss) / (l3hit + l3miss),
	      (f64) l3miss / pm->n_ops);

  return s;
}

static u8 *
format_perf_b_inst_per_cycle (u8 * s, va_list * args)
{
  perf_main_t *pm = va_arg (*args, perf_main_t *);
  u64 PERF_E_INST_RETIRED_ANY_P = perf_get_counter_diff (pm, 0, 0, 1);
  u64 PERF_E_CPU_CLK_UNHALTED_THREAD_P = perf_get_counter_diff (pm, 1, 0, 1);
  u64 PERF_E_CPU_CLK_UNHALTED_REF_TSC = perf_get_counter_diff (pm, 2, 0, 1);

  s = format (s, "CPU Frequency:          %5.2f GHz\n",
	      (f64) get_base_freq () * PERF_E_CPU_CLK_UNHALTED_THREAD_P /
	      (PERF_E_CPU_CLK_UNHALTED_REF_TSC) / 1000);

  s = format (s, "Instructions per cycle: %0.2f\n",
	      (f64) PERF_E_INST_RETIRED_ANY_P /
	      PERF_E_CPU_CLK_UNHALTED_THREAD_P);

  return s;
}

static u8 *
format_perf_b_top_down (u8 * s, va_list * args)
{
  perf_main_t *pm = va_arg (*args, perf_main_t *);
  u64 CPU_CLK_UNHALTED_CORE = perf_get_counter_diff (pm, 1, 0, 1);
  u64 UOPS_ISSUED_ANY = perf_get_counter_diff (pm, 3, 0, 1);
  u64 UOPS_RETIRED_RETIRE_SLOTS = perf_get_counter_diff (pm, 4, 0, 1);
  u64 IDQ_UOPS_NOT_DELIVERED_CORE = perf_get_counter_diff (pm, 5, 0, 1);
  u64 INT_MISC_RECOVERY_CYCLES = perf_get_counter_diff (pm, 6, 0, 1);

  s = format (s, "Front End   = %5.2f %%\n",
	      (f64) IDQ_UOPS_NOT_DELIVERED_CORE /
	      (4 * CPU_CLK_UNHALTED_CORE) * 100);

  s = format (s, "Speculation = %5.2f %%\n", (f64)
	      (UOPS_ISSUED_ANY - UOPS_RETIRED_RETIRE_SLOTS +
	       (4 * INT_MISC_RECOVERY_CYCLES)) /
	      (4 * CPU_CLK_UNHALTED_CORE) * 100);

  s = format (s, "Retiring    = %5.2f %%\n", (f64)
	      UOPS_RETIRED_RETIRE_SLOTS / (4 * CPU_CLK_UNHALTED_CORE) * 100);

  s = format (s, "Back End    = %5.2f %%\n", (f64)
	      (1 - ((f64) (IDQ_UOPS_NOT_DELIVERED_CORE + UOPS_ISSUED_ANY +
			   (4 * INT_MISC_RECOVERY_CYCLES)) /
		    (4 * CPU_CLK_UNHALTED_CORE))) * 100);

  return s;
}

static inline clib_error_t *
perf_init_bundle (perf_main_t * pm, perf_bundle_t b)
{
  switch (b)
    {
    case PERF_B_MEM_LOAD_RETIRED_HIT_MISS:
      pm->events[0] = PERF_E_MEM_LOAD_RETIRED_L1_HIT;
      pm->events[1] = PERF_E_MEM_LOAD_RETIRED_L1_MISS;
      pm->events[2] = PERF_E_MEM_LOAD_RETIRED_L2_MISS;
      pm->events[3] = PERF_E_MEM_LOAD_RETIRED_L3_MISS;
      pm->n_events = 4;
      pm->bundle_format_fn = &format_perf_b_mem_load_retired_hit_miss;
      break;
    case PERF_B_INST_PER_CYCLE:
      pm->events[0] = PERF_E_INST_RETIRED_ANY_P;
      pm->events[1] = PERF_E_CPU_CLK_UNHALTED_THREAD_P;
      pm->n_events = 2;
      pm->bundle_format_fn = &format_perf_b_inst_per_cycle;
      break;
    case PERF_B_DTLB_LOAD_MISSES:
      pm->events[0] = PERF_E_DTLB_LOAD_MISSES_MISS_CAUSES_A_WALK;
      pm->events[1] = PERF_E_DTLB_LOAD_MISSES_WALK_COMPLETED;
      pm->events[2] = PERF_E_DTLB_LOAD_MISSES_WALK_PENDING;
      pm->events[3] = PERF_E_DTLB_LOAD_MISSES_STLB_HIT;
      pm->n_events = 4;
      break;
    case PERF_B_TOP_DOWN:
      pm->events[0] = PERF_E_INST_RETIRED_ANY_P;
      pm->events[1] = PERF_E_CPU_CLK_UNHALTED_THREAD_P;
      pm->events[2] = PERF_E_CPU_CLK_UNHALTED_REF_TSC;
      pm->events[3] = PERF_E_UOPS_ISSUED_ANY;
      pm->events[4] = PERF_E_UOPS_RETIRED_RETIRE_SLOTS;
      pm->events[5] = PERF_E_IDQ_UOPS_NOT_DELIVERED_CORE;
      pm->events[6] = PERF_E_INT_MISC_RECOVERY_CYCLES;
      pm->n_events = 7;
      pm->bundle_format_fn = &format_perf_b_top_down;
      break;
    default:
      break;
    };
  return perf_init (pm);
}
