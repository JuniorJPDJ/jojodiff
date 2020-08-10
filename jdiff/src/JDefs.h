/*
 * JDefs.h
 *
 * JojoDiff global definitions
 *
 * Copyright (C) 2002-2020 Joris Heirbaut
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
 *
 * Author                Version Date       Modification
 * --------------------- ------- -------    -----------------------
 * Joris Heirbaut        v0.8.2  06-12-2011 Use jfopen/jfclose/jfseek/jfread to avoid interference with LARGEFILE redefinitions
 */

#ifndef _JDEFS_H
#define _JDEFS_H

#include <stdio.h>

#define JDIFF_VERSION   "0.8.4 (beta) 2020"
#define JDIFF_COPYRIGHT "Copyright (C) 2002-2020 Joris Heirbaut"

#define XSTR(x) STR(x)
#define STR(x) #x

#ifdef _DEBUG
#define debug           1       /**< Include debug code */
#else
#define debug           0       /**< Do not include debug code */
#endif

/*
 * Largefile definitions: how to handle files > 2GB
 *
 * Nowadays (in 2020), _FILE_OFFSET_BIT=64 by default (in most cases).
 * MinGW still sticks to _FILE_OFFSET_BIT=32 by default.
 * I leave it to the Makefile (or compiler settings) to decide whether or not to enable 64-bit file support.
 */
// If _FILE_OFFSET_BITS works correctly, following should be right:
#define off_t    off_t
#define jfopen   fopen
#define jfclose  fclose
#define jfseek   fseeko
#define jftell   ftello

// Indicate JDIFF that files may be larger that 2GB
#if _FILE_OFFSET_BIT == 64
#define JDIFF_LARGEFILE
#endif
#ifdef _LARGEFILE64_SOURCE
#define JDIFF_LARGEFILE
#endif

// Normal definition
#define PRIzd "zd"

// MINGW uses ms windows printf that does not recognise %zd
// unless -D_GNU_SOURCE has been specified
#ifndef _GNU_SOURCE
#ifdef __MINGW32__ // #if __MINGW_PRINTF_FORMAT == ms_printf doen't work
    // set coorect PRIzd
    #undef PRIzd
    #if _FILE_OFFSET_BITS == 64
        #define PRIzd "I64d"  // ms_printf doesn't know about %lld nor %zd
    #else
        #define PRIzd "ld"
    #endif // _FILE_OFFSET_BITS
#endif // __MINGW_PRINTF_FORMAT
#endif // _GNU_SOURCE

// One person had issues with MINGW32 and istreams/malloc.
// We're adding some definitions to suit everyone's needs.
// I guess than more recent versions of MINGW may not have these issues.
#define JDIFF_THROW_BAD_ALLOC
#ifndef __MINGW64__
#ifdef  __MINGW32__
    #define JDIFF_STDIO_ONLY        // don't use istreams
    #undef  JDIFF_THROW_BAD_ALLOC   // throw bad_alloc if alloc fails
#endif // __MINGW32__
#endif // __MINGW64__

#ifdef JDIFF_LARGEFILE
#if debug
#define P8zd    "%10" PRIzd     // in debug mode, we'll stay with 10 decimals for files > 2GB
#else
#define P8zd    "%12" PRIzd     // increase to 12 decimals for files > 2GB
#endif
#else
#define P8zd    "%8" PRIzd      // 8 decimals is ok for files < 2GB
#endif

/*
 * Global definitions
 */
#define uchar unsigned char             // always unsigned char
#define jchar unsigned char             // depending on architecture, may change to byte, char, int, ....
#define null  NULL

#ifdef _LARGESAMPLE
typedef unsigned long long int  hkey ;  // 64-bit hash keys
#define PRIhkey "llx"                   // format to print a hkey
#else
typedef unsigned long int hkey ;        // 32-bit hash keys
#define PRIhkey "lx"                    // format to print a hkey
#endif /* _LARGESAMPLE */
const int SMPSZE = (int) sizeof(hkey) * 8 ;                                         /**< Number of bit in a sample   */
const off_t MAX_OFF_T = (((off_t)-1) ^ (((off_t) 1) << (sizeof(off_t) * 8 - 1))) ;  /**< Largest positive offset     */

#define EOB (EOF - 1)                   /**< End-Of-Buffer constant              */
#define EXI_OK   0                      /**< OK Exit code                        */
#define EXI_EQL  0                      /**< OK Exit code, no differences found  */
#define EXI_DIF  1                      /**< OK Exit code, differences found     */
#define EXI_ARG  2                      /**< Error: not enough arguments         */
#define EXI_FRT  3                      /**< Error opening first file            */
#define EXI_SCD  4                      /**< Error opening second file           */
#define EXI_OUT  5                      /**< Error opening output file           */
#define EXI_SEK  6                      /**< Error seeking file                  */
#define EXI_LRG  7                      /**< Error on 64-bit number              */
#define EXI_RED  8                      /**< Error reading file                  */
#define EXI_WRI  9                      /**< Error writing file                  */
#define EXI_MEM  10                     /**< Error allocating memory             */
#define EXI_ERR  20                     /**< Spurious error occured              */

/**
 * Output routine constants
 */
#define ESC     0xA7    /**< 167 Escape       */
#define MOD     0xA6    /**< 166 Modify       */
#define INS     0xA5    /**< 165 Insert       */
#define DEL     0xA4    /**< 164 Delete       */
#define EQL     0xA3    /**< 163 Equal        */
#define BKT     0xA2    /**< 162 Backtrace    */

/**
* Some utilities
*/

namespace JojoDiff {

    /**
    * @brief Get highest lower prime.
    *
    * @param    aiNum   number to get a prime for
    * @return   > 0     largest prime lower than aiNum
    */
    int getLowerPrime(int aiNum) ;

} /* namespace jojodiff */

// Old stuff
//#if defined _FILE_OFFSET_BITS && _FILE_OFFSET_BITS == 64
//
//#else
//#ifdef _LARGEFILE64_SOURCE
//#define JDIFF_LARGEFILE
//#define off_t off64_t
//#ifdef __MINGW32__
//#define PRIzd "I64d"
//#else
//#define PRIzd "ld"
//#endif
//
//#define jfopen   fopen64
//#define jfclose  fclose
//#define jfseek   fseeko64
//#define jftell   ftello64
//
//#else
//
//#define off_t off_t
//#define PRIzd "ld"
//
//#define jfopen   fopen
//#define jfclose  fclose
//#define jfseek   fseeko
//#define jftell   ftello
//
//#endif
//#endif
#endif /* _JDEFS_H */
