#ifndef _ASM_SPARC64_UNALIGNED_H_
#define _ASM_SPARC64_UNALIGNED_H_

/* Sparc can't handle unaligned accesses. */

#include <linux/string.h>


/* Use memmove here, so gcc does not insert a __builtin_memcpy. */

#define get_unaligned(ptr) \
  ({ __typeof__(*(ptr)) __tmp; memmove(&__tmp, (ptr), sizeof(*(ptr))); __tmp; })

#define put_unaligned(val, ptr)				\
  ({ __typeof__(*(ptr)) __tmp = (val);			\
     memmove((ptr), &__tmp, sizeof(*(ptr)));		\
     (void)0; })

#endif /* _ASM_SPARC64_UNALIGNED_H */
