/*
 * JDefs.h
 *
 * JojoDiff global definitions
 *
 * Copyright (C) 2002-2011 Joris Heirbaut
 *
 * This file is part of JojoDiff.
 *
 * JojoDiff is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _JDEFS_H
#define _JDEFS_H

#include <stdio.h>

#ifdef _DEBUG
#define debug           1       /* Include debug code? */
#else
#define debug           0       /* Include debug code? */
#endif

/*
 * Default definitions (for GCC/Linux)
 */
#if defined _FILE_OFFSET_BITS && _FILE_OFFSET_BITS == 64
#define JDIFF_LARGEFILE
#define off_t off_t
#define PRIzd "lld"
#else
#ifdef _LARGEFILE64_SOURCE
#define JDIFF_LARGEFILE
#define off_t off64_t
#ifdef __MINGW32__
#define PRIzd "I64d"
#else
#define PRIzd "lld"
#endif
#define fopen   fopen64
#define fclose  fclose
#define fseek   fseeko64
#define ftell   ftello64
#else
#define off_t off_t
#define PRIzd "ld"
#endif
#endif

#ifdef JDIFF_LARGEFILE
#if debug
#define P8zd    "%10" PRIzd
#else
#define P8zd    "%12" PRIzd
#endif
#else
#define P8zd    "%8" PRIzd
#endif

/*
 * Global definitions
 */
#define uchar unsigned char
#define ulong unsigned long int         // unsigned long
#define null  NULL

#ifdef _LARGESAMPLE
#define hkey  unsigned long long int    // 64-bit hash keys
#define PRIhkey "llx"                   // format to print a hkey
#else
#define hkey  unsigned long int         // 32-bit hash keys
#define PRIhkey "lx"                    // format to print a hkey
#endif /* _LARGESAMPLE */

#define EOB (EOF - 1)                    // End-Of-Buffer constant
const int SMPSZE = (int) sizeof(hkey) * 8 ;  										// Number of bytes in a sample
const off_t MAX_OFF_T = (((off_t)-1) ^ (((off_t) 1) << (sizeof(off_t) * 8 - 1))) ;	// Largest positive offset

/**
 * Output routine constants
 */
#define ESC     0xA7    /* Escape       */
#define MOD     0xA6    /* Modify       */
#define INS     0xA5    /* Insert       */
#define DEL     0xA4    /* Delete       */
#define EQL     0xA3    /* Equal        */
#define BKT     0xA2    /* Backtrace    */

#endif /* _JDEFS_H */
