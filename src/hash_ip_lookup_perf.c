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

#include <vppinfra/mem.h>
#include <vnet/ip/ip_packet.h>
#include <vnet/ip/ip4_packet.h>
#include <vnet/tcp/tcp_packet.h>

#include <vppinfra/bihash_16_8.h>
#include <vppinfra/bihash_template.h>
#include <vppinfra/bihash_template.c>

#include <vppinfra/cuckoo_16_8.h>
#include <vppinfra/cuckoo_template.h>
#include <vppinfra/cuckoo_template.c>

#include "stats.h"
#include "upstream.h"
#include "cache.h"

#define OPTIMIZE 1
#define FRAME_SIZE 256
#define NORMALIZE_KEYS 1

typedef union
{
  struct
  {
    union
    {
      u32 spi;
      struct
      {
	u16 port_lo;
	u16 port_hi;
      };
      struct
      {
	u8 type;
	u8 code;
      };
    };
    u8 unused;
    u8 proto;
    u16 unused2;
    u32 ip_addr_lo;
    u32 ip_addr_hi;
  };
  u8x16u as_u8x16u;
} __clib_packed ip4_key_t;

STATIC_ASSERT_SIZEOF (ip4_key_t, 16);

typedef union
{
  clib_bihash_kv_16_8_t b;
  clib_cuckoo_kv_16_8_t c;
  struct
  {
    ip4_key_t key;
    u64 value;
  };
} ip4_kv_t;

STATIC_ASSERT_SIZEOF (ip4_kv_t, 24);

typedef int (add_frame_fn_t) (void *, ip4_kv_t *, int);
typedef void (calc_key_and_hash_fn_t) (void *, u8 **, int, ip4_kv_t *);
typedef int (search_frame_fn_t) (void *, int, ip4_kv_t *);

static const u8 l4_mask_bits[256] = {
  [IP_PROTOCOL_ICMP] = 16,
  [IP_PROTOCOL_IGMP] = 8,
  [IP_PROTOCOL_TCP] = 32,
  [IP_PROTOCOL_UDP] = 32,
  [IP_PROTOCOL_IPSEC_ESP] = 32,
  [IP_PROTOCOL_IPSEC_AH] = 32,
};

static const u64 tcp_udp_bitmask = ((1 << IP_PROTOCOL_TCP) |
				    (1 << IP_PROTOCOL_UDP));
static const u8x16 key_shuff_no_norm =
  { 0, 1, 2, 3, -1, 5, -1, -1, 8, 9, 10, 11, 12, 13, 14, 15 };
static const u8x16 key_shuff_norm =
  { 2, 3, 0, 1, -1, 5, -1, -1, 12, 13, 14, 15, 8, 9, 10, 11 };
static const u8x16 src_ip_byteswap_x2 =
  { 11, 10, 9, 8, -1, -1, -1, -1, 11, 10, 9, 8, -1, -1, -1, -1 };
static const u8x16 dst_ip_byteswap_x2 =
  { 15, 14, 13, 12, -1, -1, -1, -1, 15, 14, 13, 12, -1, -1, -1, -1 };


static_always_inline void
calc_key (ip4_header_t * ip, ip4_kv_t * kv, int calc_hash)
{
  u8 pr;
  u8x16 key, swap;
  u32 l4_hdr;

  /* load last 16 bytes of ip header into 128-bit register */
  key = *(u8x16u *) ((u8 *) ip + 4);
  pr = ip->protocol;

  swap = key_shuff_no_norm;

  if (NORMALIZE_KEYS)
    {
      i64x2 norm, zero = { };
      /* byteswap src and dst ip and splat into all 4 elts of u32x4, then
       * compare so result will hold all ones if we need to swap src and dst
       * signed vector type is used as */
      norm = (((i64x2) u8x16_shuffle (key, src_ip_byteswap_x2)) >
	      ((i64x2) u8x16_shuffle (key, dst_ip_byteswap_x2)));

      /* we only normalize tcp and tcp, for other cases we reset all bits to 0 */
      norm &= i64x2_splat ((1ULL << pr) & tcp_udp_bitmask) != zero;

      /* if norm is zero, we don't need to normalize so nothing happens here */
      swap += (key_shuff_norm - key_shuff_no_norm) & (u8x16) norm;
    }

  /* overwrite first 4 bytes with first 0 - 4 bytes of l4 header */
  l4_hdr = *(u32 *) ip4_next_header (ip) & pow2_mask (l4_mask_bits[pr]);
  key = (u8x16) u32x4_insert ((u32x4) key, l4_hdr, 0);

  key = u8x16_shuffle (key, swap);

  /* store key */
  *((u8x16u *) (&kv->key)) = key;

  if (calc_hash)
    {
      u64 hash = 0;
      hash = _mm_crc32_u64 (hash, u64x2_extract (key, 0));
      kv->value = _mm_crc32_u64 (hash, u64x2_extract (key, 1));
    }
}

int __clib_noinline
__clib_section (".add_frame_bihash")
add_frame_bihash (void *t, ip4_kv_t * ikv, int n_left)
{
  clib_bihash_kv_16_8_t *kv = &ikv->b;
  while (OPTIMIZE && n_left >= 4)
    {
      if (n_left >= 12)
	{
	  clib_bihash_kv_16_8_t *pkv = kv + 8;
	  clib_bihash_prefetch_bucket_16_8 (t, pkv[0].value);
	  clib_bihash_prefetch_bucket_16_8 (t, pkv[1].value);
	  clib_bihash_prefetch_bucket_16_8 (t, pkv[2].value);
	  clib_bihash_prefetch_bucket_16_8 (t, pkv[3].value);
	}

      if (n_left >= 8)
	{
	  clib_bihash_kv_16_8_t *pkv = kv + 4;
	  clib_bihash_prefetch_data_16_8 (t, pkv[0].value);
	  clib_bihash_prefetch_data_16_8 (t, pkv[1].value);
	  clib_bihash_prefetch_data_16_8 (t, pkv[2].value);
	  clib_bihash_prefetch_data_16_8 (t, pkv[3].value);
	}

      kv[0].value = 0;
      kv[1].value = 1;
      kv[2].value = 2;
      kv[3].value = 3;
      if (clib_bihash_add_del_inline_16_8 (t, kv + 0, 2, 0, 0))
	return -1;
      if (clib_bihash_add_del_inline_16_8 (t, kv + 1, 2, 0, 0))
	return -1;
      if (clib_bihash_add_del_inline_16_8 (t, kv + 2, 2, 0, 0))
	return -1;
      if (clib_bihash_add_del_inline_16_8 (t, kv + 3, 2, 0, 0))
	return -1;

      kv += 4;
      n_left -= 4;
    }

  while (n_left)
    {
      kv[0].value = n_left;
      if (clib_bihash_add_del_inline_16_8 (t, kv, 2, 0, 0))
	return -1;
      kv++;
      n_left--;
    }
  return 0;
}

int __clib_noinline
__clib_section (".add_frame_cuckoo")
add_frame_cuckoo (void *t, ip4_kv_t * ikv, int n_left)
{
  clib_cuckoo_kv_16_8_t *kv = &ikv->c;

  while (n_left)
    {
      kv[0].value = n_left;
      if (clib_cuckoo_add_del_16_8 (t, kv, 1 /* is_add */ ,
				    0 /* overwrite */ ))
	return -1;
      kv++;
      n_left--;
    }
  return 0;
}

static_always_inline void
calc_key_and_hash_four (clib_bihash_16_8_t * t, u8 ** hdr,
			ip4_kv_t * kv, int hdr_prefetch_stride,
			int data_prefetch_stride)
{
  u8 **ph = hdr + hdr_prefetch_stride;
  ip4_kv_t *pd = kv + data_prefetch_stride;

  if (hdr_prefetch_stride)
    clib_prefetch_load (ph[0]);
  calc_key ((ip4_header_t *) hdr[0], kv + 0, 1);

  clib_bihash_prefetch_bucket_16_8 (t, kv[0].value);
  if (data_prefetch_stride)
    clib_bihash_prefetch_data_16_8 (t, pd[0].value);

  if (hdr_prefetch_stride)
    clib_prefetch_load (ph[1]);
  calc_key ((ip4_header_t *) hdr[1], kv + 1, 1);

  clib_bihash_prefetch_bucket_16_8 (t, kv[1].value);
  if (data_prefetch_stride)
    clib_bihash_prefetch_data_16_8 (t, pd[1].value);

  if (hdr_prefetch_stride)
    clib_prefetch_load (ph[2]);
  calc_key ((ip4_header_t *) hdr[2], kv + 2, 1);

  clib_bihash_prefetch_bucket_16_8 (t, kv[2].value);
  if (data_prefetch_stride)
    clib_bihash_prefetch_data_16_8 (t, pd[2].value);

  if (hdr_prefetch_stride)
    clib_prefetch_load (ph[3]);
  calc_key ((ip4_header_t *) hdr[3], kv + 3, 1);

  clib_bihash_prefetch_bucket_16_8 (t, kv[3].value);
  if (data_prefetch_stride)
    clib_bihash_prefetch_data_16_8 (t, pd[3].value);
}

void __clib_noinline
__clib_section (".calc_key_and_hash_bihash")
calc_key_and_hash_bihash (void *t, u8 ** hdr, int n, ip4_kv_t * kv)
{
  int n_left = n;

  if (OPTIMIZE == 0 || n < 32)
    goto one_by_one;

  /* first 32 - header and bucket prefetch */
  for (; n_left >= n - 12; hdr += 4, kv += 4, n_left -= 4)
    calc_key_and_hash_four (t, hdr, kv, 4, 0);

  for (; n_left >= 12; hdr += 4, kv += 4, n_left -= 4)
    calc_key_and_hash_four (t, hdr, kv, 8, -16);

  for (; n_left >= 4; hdr += 4, kv += 4, n_left -= 4)
    calc_key_and_hash_four (t, hdr, kv, 0, -16);

  if (1)
    for (int i = -16; i <= 0; i++)
      clib_bihash_prefetch_data_16_8 (t, kv[-i].value);

one_by_one:
  while (n_left)
    {
      calc_key ((ip4_header_t *) hdr[0], kv, 1);

      kv++;
      hdr++;
      n_left--;
    }
}

void __clib_noinline
__clib_section (".calc_key_and_cuckoo")
calc_key_and_hash_cuckoo (void *t, u8 ** hdr, int n, ip4_kv_t * kv)
{
  int n_left = n;

  if (OPTIMIZE == 0)
    goto one_by_one;

  for (; n_left >= 12; hdr += 4, kv += 4, n_left -= 4)
    calc_key_and_hash_four (t, hdr, kv, 8, 0);

  for (; n_left >= 4; hdr += 4, kv += 4, n_left -= 4)
    calc_key_and_hash_four (t, hdr, kv, 0, 0);

one_by_one:
  while (n_left)
    {
      calc_key ((ip4_header_t *) hdr[0], kv, 1);

      kv++;
      hdr++;
      n_left--;
    }
}

int __clib_noinline
__clib_section (".search_frame_bihash")
search_frame_bihash (void *t, int n_left, ip4_kv_t * ikv)
{
  u32 n_hit = n_left;
  clib_bihash_kv_16_8_t *kv = &ikv->b;

  while (OPTIMIZE && n_left >= 4)
    {
      if (n_left >= 12)
	{
	  clib_bihash_kv_16_8_t *pkv = kv + 8;
	  clib_bihash_prefetch_bucket_16_8 (t, pkv[0].value);
	  clib_bihash_prefetch_bucket_16_8 (t, pkv[1].value);
	  clib_bihash_prefetch_bucket_16_8 (t, pkv[2].value);
	  clib_bihash_prefetch_bucket_16_8 (t, pkv[3].value);
	}

      if (n_left >= 8)
	{
	  clib_bihash_kv_16_8_t *pkv = kv + 4;
	  clib_bihash_prefetch_data_16_8 (t, pkv[0].value);
	  clib_bihash_prefetch_data_16_8 (t, pkv[1].value);
	  clib_bihash_prefetch_data_16_8 (t, pkv[2].value);
	  clib_bihash_prefetch_data_16_8 (t, pkv[3].value);
	}

      if (clib_bihash_search_inline_with_hash_16_8 (t, kv[0].value, kv + 0))
	n_hit--;
      if (clib_bihash_search_inline_with_hash_16_8 (t, kv[1].value, kv + 1))
	n_hit--;
      if (clib_bihash_search_inline_with_hash_16_8 (t, kv[2].value, kv + 2))
	n_hit--;
      if (clib_bihash_search_inline_with_hash_16_8 (t, kv[3].value, kv + 3))
	n_hit--;

      kv += 4;
      n_left -= 4;
    }

  while (n_left)
    {
      if (clib_bihash_search_inline_with_hash_16_8 (t, kv[0].value, kv))
	n_hit--;

      kv++;
      n_left--;
    }
  return n_hit;
}

int __clib_noinline
__clib_section (".search_frame_cuckoo")
search_frame_cuckoo (void *t, int n_left, ip4_kv_t * ikv)
{
  u32 n_hit = n_left;
  clib_cuckoo_kv_16_8_t *kv = &ikv->c;

  while (n_left)
    {
      if (clib_cuckoo_search_inline_with_hash_16_8 (t, kv[0].value, kv))
	n_hit--;

      kv++;
      n_left--;
    }
  return n_hit;
}

static void
cuckoo_garbage_collect_cb (clib_cuckoo_16_8_t * h, void *ctx)
{
  clib_cuckoo_garbage_collect_16_8 (h);
}

int
main (int argc, char *argv[])
{
  unformat_input_t _input, *in = &_input;
  int i;
  u32 seed = random_default_seed ();
  stats_main_t stats_main = { }, *sm = &stats_main;
  ip4_kv_t kv[FRAME_SIZE];
  u8 **headers;
  add_frame_fn_t *add_frame;
  search_frame_fn_t *search_frame;
  calc_key_and_hash_fn_t *calc_key_and_hash;
  format_function_t *format_hash;
  void *t;

  /* configurable parameters - defaults */
  u32 n_elts = 10 << 20;
  u32 n_samples = 32;
  u32 log2_n_buckets = 20;
  u32 hash_mem_size_mb = 1ULL << 10;
  int use_cuckoo = 0;
  u32 verbose = 0;

  clib_mem_init (0, 1ULL << 30);

  unformat_init_command_line (in, argv);
  while (unformat_check_input (in) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (in, "num-elts %u", &n_elts))
	;
      else if (unformat (in, "num-samples %u", &n_samples))
	;
      else if (unformat (in, "log2-num-buckets %u", &log2_n_buckets))
	;
      else if (unformat (in, "hash-mem-size-mb %u", &hash_mem_size_mb))
	;
      else if (unformat (in, "verbose %u", &verbose))
	;
      else if (unformat (in, "cuckoo"))
	use_cuckoo = 1;
      else
	clib_panic ("unknown input '%U'", format_unformat_error, in);
    }
  unformat_free (in);

  n_elts = (n_elts / FRAME_SIZE) * FRAME_SIZE;

  fformat (stderr, "config: num-elts %u num-samples %u log2-num-buckets %u "
	   "hash-mem-size-mb %lu verbose %u%s\n",
	   n_elts, n_samples, log2_n_buckets, hash_mem_size_mb, verbose,
	   use_cuckoo ? " cuckoo" : "");


  if (use_cuckoo)
    {
      t = clib_mem_alloc_aligned (sizeof (clib_cuckoo_16_8_t),
				  CLIB_CACHE_LINE_BYTES);
      clib_memset (t, 0, sizeof (clib_cuckoo_16_8_t));
      clib_cuckoo_init_16_8 (t, "ip4", 1ULL << log2_n_buckets,
			     cuckoo_garbage_collect_cb, NULL);
      add_frame = &add_frame_cuckoo;
      search_frame = &search_frame_cuckoo;
      calc_key_and_hash = &calc_key_and_hash_cuckoo;
      format_hash = &format_cuckoo_16_8;
    }
  else
    {
      t = clib_mem_alloc_aligned (sizeof (clib_bihash_16_8_t),
				  CLIB_CACHE_LINE_BYTES);
      clib_memset (t, 0, sizeof (clib_bihash_16_8_t));
      clib_bihash_init_16_8 (t, "ip4", 1ULL << log2_n_buckets,
			     (u64) hash_mem_size_mb << 20);
      add_frame = &add_frame_bihash;
      search_frame = &search_frame_bihash;
      calc_key_and_hash = &calc_key_and_hash_bihash;
      format_hash = &format_bihash_16_8;
    }

  headers = clib_mem_alloc (n_elts * sizeof (void *));

  stats_init (sm, n_elts, n_samples, 2);

  for (i = 0; i < n_elts; i++)
    {
      int sz = sizeof (ip4_header_t) + sizeof (tcp_header_t);
      u8 *p = clib_mem_alloc (sz);
      ip4_header_t *ip = (ip4_header_t *) p;
      tcp_header_t *tcp = (tcp_header_t *) (p + sizeof (ip4_header_t));

      clib_memset (p, 0, sz);
      ip->ip_version_and_header_length = 0x45;
      ip->ttl = 64;
      ip->src_address.as_u32 = clib_host_to_net_u32 (0x80000000 + i);
      ip->dst_address.as_u32 = clib_host_to_net_u32 (0x81000000 + i);
      ip->protocol = IP_PROTOCOL_TCP;
      tcp->src_port = clib_host_to_net_u16 (1024);
      tcp->dst_port = clib_host_to_net_u16 (80);
      headers[i] = p;
    }

  fformat (stderr, "%u ip4 headers created...\n", n_elts);

  for (i = 0; i < n_elts; i++)
    {
      int j = random_u32 (&seed) % n_elts;
      u8 *tmp = headers[i];
      headers[i] = headers[j];
      headers[j] = tmp;
    }

  fformat (stderr, "header pointers randomized ...\n");

  for (i = 0; i < n_elts; i++)
    _mm_clflush (headers[i]);
  fformat (stderr, "header cache flushed ...\n");

  stats_reset (sm);
  stats_add_series (sm, 0, "Create key and hash");
  stats_add_series (sm, 1, "Add");
  cache_flush ();

  for (i = 0; i < n_elts; i += FRAME_SIZE)
    {
      int rv;
      u64 a, b, c;
      u32 signature;

      /* bring headers into LLC */
      for (int x = 0; x < FRAME_SIZE; x++)
	_mm_prefetch (headers[i + x], _MM_HINT_T2);

      asm volatile ("":::"memory");
      a = __rdtscp (&signature);
      calc_key_and_hash (t, headers + i, FRAME_SIZE, kv);
      b = __rdtscp (&signature);
      rv = add_frame (t, kv, FRAME_SIZE);
      c = __rdtscp (&signature);
      asm volatile ("":::"memory");

      if (rv)
	clib_panic ("hash collision\n");

      stats_add (sm, 0, FRAME_SIZE, b - a);
      stats_add (sm, 1, FRAME_SIZE, c - b);
    }

  fformat (stderr, "\nhash add entry stats (ticks/entry):\n%U\n",
	   format_stats, sm);

  fformat (stderr, "\nhash stats:\n%U\n", format_hash, t, verbose);

  stats_reset (sm);
  stats_add_series (sm, 1, "Search");
  cache_flush ();

  for (i = 0; i < n_elts; i += FRAME_SIZE)
    {
      int rv;
      u64 a, b, c;
      u32 signature;

      /* bring headers into LLC */
      for (int x = 0; x < FRAME_SIZE; x++)
	_mm_prefetch (headers[i + x], _MM_HINT_T2);

      asm volatile ("":::"memory");
      a = __rdtscp (&signature);
      calc_key_and_hash (t, headers + i, FRAME_SIZE, kv);
      b = __rdtscp (&signature);
      rv = search_frame (t, FRAME_SIZE, kv);
      c = __rdtscp (&signature);
      asm volatile ("":::"memory");

      if (rv != FRAME_SIZE)
	clib_panic ("search failed\n");

      stats_add (sm, 0, FRAME_SIZE, b - a);
      stats_add (sm, 1, FRAME_SIZE, c - b);
    }
  fformat (stderr, "\nhash search entry stats (ticks/entry):\n%U\n",
	   format_stats, sm);
}
