#include <sys/user.h> /* For PAGE_SIZE */

#ifndef PAGE_SIZE
#define PAGE_SIZE (4096)
#endif

extern int do_shared_array_work(void);

/* We'd like the arrays big enough that the page granularity doesn't
 * skew the figures */
#define ARRAY_SIZE (PAGE_SIZE * 100)

/* And to make it really easy, ask gcc to put things on page boundaries */
typedef unsigned char BigArr[ARRAY_SIZE] __attribute__ ((aligned(PAGE_SIZE)));

