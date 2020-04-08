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

typedef struct
{
  u64 min, max, total, cnt;
} stats_elt_t;

typedef struct
{
  char **names;
  u64 n_samples, n_elts, *n_added;
  stats_elt_t *elts;
} stats_main_t;

static_always_inline void
stats_reset (stats_main_t * s)
{
  stats_elt_t *e;
  u64 *x;
  vec_foreach (e, s->elts)
  {
    clib_memset (e, 0, sizeof (stats_elt_t));
    e->min = ~0;
  }
  vec_foreach (x, s->n_added) x[0] = 0;
}

static_always_inline void
stats_init (stats_main_t * s, int n_elts, int n_samples, int n_series)
{
  s->n_elts = n_elts;
  s->n_samples = n_samples;
  vec_validate_aligned (s->elts, n_series * n_samples - 1,
			CLIB_CACHE_LINE_BYTES);
  vec_validate_aligned (s->n_added, n_series - 1, CLIB_CACHE_LINE_BYTES);
  vec_validate_aligned (s->names, n_series - 1, CLIB_CACHE_LINE_BYTES);
  stats_reset (s);
}

static_always_inline void
stats_add_series (stats_main_t * s, int i, char *name)
{
  s->names[i] = name;
}

static_always_inline void
stats_add (stats_main_t * s, u32 series, u32 n, u64 val)
{
  stats_elt_t *e = s->elts;
  e += series * s->n_samples;
  e += (s->n_added[series] / (s->n_elts / s->n_samples));
  s->n_added[series] += n;
  e->total += val;
  e->cnt += n;
  val /= n;
  e->min = e->min > val ? val : e->min;
  e->max = e->max < val ? val : e->max;
}

static u8 *
format_stats (u8 * s, va_list * args)
{
  stats_main_t *sm = va_arg (*args, stats_main_t *);
  stats_elt_t *t = 0;

  vec_validate (t, vec_len (sm->names) - 1);
  for (int j = 0; j < vec_len (sm->names); j++)
    t[j].min = ~0;

  s = format (s, "\n      ");
  for (int j = 0; j < vec_len (sm->names); j++)
    s = format (s, "        %-32s", sm->names[j]);

  s = format (s, "\n      ");
  for (int j = 0; j < vec_len (sm->names); j++)
    s = format (s, "        %8s%8s%8s%8s", "elts", "avg", "min", "max");
  s = format (s, "\n");

  for (int i = 0; i < sm->n_samples; i++)
    {
      s = format (s, "  [%02u]", i);
      for (int j = 0; j < vec_len (sm->n_added); j++)
	{
	  stats_elt_t *e = sm->elts + j * sm->n_samples + i;
	  s = format (s, "        %8lu%8lu%8lu%8lu",
		      e->cnt, e->total / e->cnt, e->min, e->max);
	  t[j].cnt += e->cnt;
	  t[j].total += e->total;
	  t[j].min = t[j].min < e->min ? t[j].min : e->min;
	  t[j].max = t[j].max > e->max ? t[j].max : e->max;
	}
      s = format (s, "\n");
    }

  s = format (s, "Total:");
  for (int i = 0; i < vec_len (sm->names); i++)
    s = format (s, "        %8lu%8lu%8lu%8lu",
		t[i].cnt, t[i].total / t[i].cnt, t[i].min, t[i].max);
  s = format (s, "\n");

  vec_free (t);
  return s;
}
