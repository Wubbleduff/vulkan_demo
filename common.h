
#pragma once

#include <stdint.h>
#include <immintrin.h>

typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef int32_t  s32;
typedef uint64_t u64;
typedef int64_t  s64;
typedef float    f32;
typedef double   f64;

_Static_assert(sizeof(f32) == 4, "Unexpected type size.");
_Static_assert(sizeof(f64) == 8, "Unexpected type size.");

#define u8_MIN   0
#define u8_MAX   0xFF
#define s8_MIN  -0x80
#define s8_MAX   0x7F
#define u16_MIN  0
#define u16_MAX  0xFFFF
#define s16_MIN -0x8000
#define s16_MAX  0x7FFF
#define u32_MIN  0
#define u32_MAX  0xFFFFFFFF
#define s32_MIN (-0x7FFFFFFF - 1)
#define s32_MAX  0x7FFFFFFF
#define u64_MIN  0
#define u64_MAX  0xFFFFFFFFFFFFFFFF
#define s64_MIN (-0x7FFFFFFFFFFFFFFF - 1)
#define s64_MAX  0x7FFFFFFFFFFFFFFF

#define INFINITY (f32)(1e300*1e300)

#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STRING STRINGIZE(__LINE__)
void assert_fn(const u64 c, const char* msg, ...);
#define ASSERT(c, msg, ...) assert_fn((u64)(c), (__FILE__ ":" LINE_STRING "  " msg "\n"), ##__VA_ARGS__)

#define ARRAY_COUNT(N) (sizeof(N) / sizeof((N)[0]))

void *memcpy(void *dst, const void *src, size_t count);
void *memset(void *dst, int c, size_t count);
#define COPY_ARRAY(DST, SRC) \
    do { \
        _Static_assert(sizeof(DST) == sizeof(SRC), "Array sizes do not match."); \
        memcpy((DST), (SRC), sizeof(SRC)); \
    } while(0)

#define COPY(DST, SRC, NUM) \
    do { \
        _Static_assert(sizeof((DST)[0]) == sizeof((SRC)[0]), "Element sizes do not match."); \
        memcpy((DST), (SRC), sizeof((SRC)[0]) * (NUM)); \
    } while(0)

#define ZERO_ARRAY(a) \
    do { \
        memset((a), 0, sizeof(a)); \
    } while(0)

#define KB(N) ((N) * 1024ULL)
#define MB(N) ((N) * 1024ULL * 1024ULL)
#define GB(N) ((N) * 1024ULL * 1024ULL * 1024ULL)

////////////////////////////////////////////////////////////////////////////////
// min
static inline u8  min_u8(u8 a, u8 b)    { return a < b ? a : b; }
static inline s8  min_s8(s8 a, s8 b)    { return a < b ? a : b; }
static inline u16 min_u16(u16 a, u16 b) { return a < b ? a : b; }
static inline s16 min_s16(s16 a, s16 b) { return a < b ? a : b; }
static inline u32 min_u32(u32 a, u32 b) { return a < b ? a : b; }
static inline s32 min_s32(s32 a, s32 b) { return a < b ? a : b; }
static inline u64 min_u64(u64 a, u64 b) { return a < b ? a : b; }
static inline s64 min_s64(s64 a, s64 b) { return a < b ? a : b; }
static inline f32 min_f32(f32 a, f32 b) { return a < b ? a : b; }
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// max
static inline u8  max_u8(u8 a, u8 b)    { return a > b ? a : b; }
static inline s8  max_s8(s8 a, s8 b)    { return a > b ? a : b; }
static inline u16 max_u16(u16 a, u16 b) { return a > b ? a : b; }
static inline s16 max_s16(s16 a, s16 b) { return a > b ? a : b; }
static inline u32 max_u32(u32 a, u32 b) { return a > b ? a : b; }
static inline s32 max_s32(s32 a, s32 b) { return a > b ? a : b; }
static inline u64 max_u64(u64 a, u64 b) { return a > b ? a : b; }
static inline s64 max_s64(s64 a, s64 b) { return a > b ? a : b; }
static inline f32 max_f32(f32 a, f32 b) { return a > b ? a : b; }
////////////////////////////////////////////////////////////////////////////////

static inline u32 f32_bits_as_u32(f32 a)
{
    union { u32 i; f32 f; } result;
    result.f = a;
    return result.i;
}

static inline f32 u32_bits_as_f32(u32 a)
{
    union { u32 i; f32 f; } result;
    result.i = a;
    return result.f;
}

static inline f32 nan_f32(void)
{
    return u32_bits_as_f32(0x7FFFFFFF);
}

static inline u32 is_nan(f32 a)
{
    return a != a;
}

static inline u32 truncate_power_of_2_u32(const u32 a)
{
    return a == 0 ? 0 : (1 << (31 - _lzcnt_u32(a)));
}

