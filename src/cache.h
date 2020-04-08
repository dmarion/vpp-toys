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

static void
cache_flush ()
{
  static u8 *flush_buffer = 0;
  u32 flush_buffer_size = 200 << 20;	/* 100 MB */
  if (flush_buffer == 0)
    {
      flush_buffer = clib_mem_alloc_aligned (flush_buffer_size, 64);
      if (flush_buffer == 0)
	clib_panic ("failed to alloc %u bytes of memory", flush_buffer_size);
    }
  for (u32 i = 0; i < flush_buffer_size; i += 64)
    flush_buffer[i]++;
}
