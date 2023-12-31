/*
	h2stdinc.h
	includes the minimum necessary stdc headers,
	defines common and / or missing types.
*/

#ifndef __H2STDINC_H
#define __H2STDINC_H

#include <u.h>
#include <libc.h>
#include <stdio.h>

#define uint32_t u32int
#define int32_t s32int
#define int16_t s16int
#define uint16_t u16int
#define uint64_t u64int
#define int64_t s64int

#define intptr_t vlong
#define uintptr_t uvlong
#define ptrdiff_t vlong

#define size_t uvlong

#undef PI

#define strcasecmp cistrcmp
#define strncasecmp cistrncmp


/*==========================================================================*/

#ifndef NULL
#if defined(__cplusplus)
#define	NULL		0
#else
#define	NULL		((void *)0)
#endif
#endif

#define	H2MAXCHAR	((char)0x7f)
#define	H2MAXSHORT	((short)0x7fff)
#define	H2MAXINT	((int)0x7fffffff)	/* max positive 32-bit integer */
#define	H2MINCHAR	((char)0x80)
#define	H2MINSHORT	((short)0x8000)
#define	H2MININT	((int)0x80000000)	/* max negative 32-bit integer */

/* make sure enums are the size of ints for structure packing */
typedef enum {
	THE_DUMMY_VALUE
} THE_DUMMY_ENUM;


/*==========================================================================*/

typedef unsigned char		byte;

#undef true
#undef false
#if defined(__cplusplus)
/* some structures have boolean members and the x86 asm code expect
 * those members to be 4 bytes long. therefore, boolean must be 32
 * bits and it can NOT be binary compatible with the 8 bit C++ bool.  */
typedef int	boolean;
#else
typedef enum {
	false = 0,
	true  = 1
} boolean;
#endif

/*==========================================================================*/

/* math */
#define	FRACBITS		16
#define	FRACUNIT		(1 << FRACBITS)

typedef int	fixed_t;


/*==========================================================================*/

/* compatibility with M$ types */
#if !defined(_WIN32)
#define	PASCAL
#define	FAR
#define	APIENTRY
#endif	/* ! WINDOWS */

/*==========================================================================*/

/* compiler specific definitions */

#if !defined(__GNUC__)
#define	__attribute__(x)
#endif	/* __GNUC__ */

/* argument format attributes for function
 * pointers are supported for gcc >= 3.1
 */
#if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 0))
#define	__fp_attribute__	__attribute__
#else
#define	__fp_attribute__(x)
#endif

/* function optimize attribute is added
 * starting with gcc 4.4.0
 */
#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ > 3))
#define	__no_optimize		__attribute__((__optimize__("0")))
#else
#define	__no_optimize
#endif

/*==========================================================================*/

/* Provide a substitute for offsetof() if we don't have one.
 * This variant works on most (but not *all*) systems...
 */
#ifndef offsetof
#define offsetof(t,m) ((size_t)&(((t *)0)->m))
#endif

/*==========================================================================*/


#endif	/* __H2STDINC_H */

