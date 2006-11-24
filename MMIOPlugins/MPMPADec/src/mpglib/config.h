/* The number of bytes in a double.  */
#define SIZEOF_DOUBLE sizeof(double)

/* The number of bytes in a float.  */
#define SIZEOF_FLOAT sizeof(float)

/* The number of bytes in a int.  */
#define SIZEOF_INT sizeof(int)

/* The number of bytes in a long.  */
#define SIZEOF_LONG sizeof(long)

/* The number of bytes in a long double.  */
#define SIZEOF_LONG_DOUBLE sizeof(long double)

/* The number of bytes in a short.  */
#define SIZEOF_SHORT sizeof(short)

/* The number of bytes in a unsigned int.  */
#define SIZEOF_UNSIGNED_INT sizeof(unsigned int)

/* The number of bytes in a unsigned long.  */
#define SIZEOF_UNSIGNED_LONG sizeof(unsigned long)

/* The number of bytes in a unsigned short.  */
#define SIZEOF_UNSIGNED_SHORT sizeof(unsigned short)

/* Define if you have the ANSI C header files.  */
#define STDC_HEADERS

/* Define if you have the <errno.h> header file.  */
#define HAVE_ERRNO_H

/* Define if you have the <fcntl.h> header file.  */
#define HAVE_FCNTL_H

/* Define if you have the <limits.h> header file.  */
#define HAVE_LIMITS_H

/* Name of package */
#define PACKAGE "lame"

/* Version number of package */
#define VERSION "3.96"

/* Define if compiler has function prototypes */
#define PROTOTYPES 1

/* enable VBR bitrate histogram */
#define BRHIST 1

/* IEEE754 compatible machine */
#define TAKEHIRO_IEEE754_HACK 1

/* faster log implementation with less but enough precission */
/*#define USE_FAST_LOG 1*/

#define HAVE_STRCHR
#define HAVE_MEMCPY

#if defined(__WATCOMC__)
#if (__WATCOMC__ < 1200)
#define uint8_t unsigned char
#define uint16_t unsigned short
#define uint32_t unsigned int
#define uint64_t unsigned long long

#define int8_t signed char
#define int16_t signed short
#define int32_t signed int
#define int64_t signed long long
#endif
#endif

typedef long double ieee854_float80_t;
typedef double      ieee754_float64_t;
typedef float       ieee754_float32_t;

#ifdef HAVE_MPGLIB
# define DECODE_ON_THE_FLY 1
#endif

#ifdef LAME_ACM
/* memory hacking for driver purposes */
#define calloc(x,y) acm_Calloc(x,y)
#define free(x)     acm_Free(x)
#define malloc(x)   acm_Malloc(x)

#include <stddef.h>
void *acm_Calloc( size_t num, size_t size );
void *acm_Malloc( size_t size );
void acm_Free( void * mem);
#endif /* LAME_ACM */

#define LAME_LIBRARY_BUILD

