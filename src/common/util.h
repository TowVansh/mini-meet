/* Small shared helpers: monotonic clock, logging, sleeping, byte packing. */
#ifndef MM_UTIL_H
#define MM_UTIL_H

#include <stdint.h>
#include <stddef.h>

uint64_t now_ms(void);           /* monotonic milliseconds */
uint64_t now_us(void);           /* monotonic microseconds */
uint64_t wall_ms(void);          /* Unix epoch milliseconds (for CSV timestamps) */
void     sleep_ms(unsigned ms);

void log_msg(const char *tag, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)))
#endif
    ;

uint32_t rand_u32(void);

/* Big-endian (network order) packing. */
static inline void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static inline uint16_t get_u16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t get_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* Compare 16-bit RTP sequence numbers with wrap-around: a is newer than b. */
static inline int seq_newer(uint16_t a, uint16_t b) { return (int16_t)(a - b) > 0; }

#endif
