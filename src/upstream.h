
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

static_always_inline void
clib_prefetch_l2_load (void *p)
{
  _mm_prefetch (p, _MM_HINT_T1);
}

#ifndef __clib_noinline
#define __clib_noinline __attribute__ ((noinline))
#endif

#ifndef __clib_section
#define __clib_section(s) __attribute__ ((section(s)))
#endif

#ifndef u32x4_insert
#define u32x4_insert(v, x, i) \
  (u32x4) _mm_insert_epi32((__m128i) (v), x, i)
#endif

#ifndef u64x2_extract
#define u64x2_extract(v, i) \
  (u64) _mm_extract_epi64((__m128i) (v), i)
#endif
