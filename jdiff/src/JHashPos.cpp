/*
 * JHashPos.cpp
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

#include <stdlib.h>
#include <new>
#include <string.h>
#include <limits.h>
using namespace std;

#include "JHashPos.h"


namespace JojoDiff {

const int COLLISION_THRESHOLD = 4 ; /* override when collision counter exceeds threshold  */
const int COLLISION_HIGH = 4 ;      /* rate at which high quality samples should override */
const int COLLISION_LOW = 1 ;       /* rate at which low quality samples should override  */

/* List of primes we select from when size is specified on commandline
const int giPme[24] = { 2147483647, 1073741789, 536870909,  268435399,
                         134217689,   67108859,  33554393,   16777213,
                           8388593,    4194301,   2097143,    1048573,
                            524287,     262139,    131071,      65521,
                             32749,      16381,      8191,       4093,
                              2039,       1021,       509,        251} ;*/

/**
* Check if number is a prime number.
* @param   number     Number to check
* @return  true = a prime, false = not a prime
*/
bool isPrime(int number){
    if(number < 2) return 0;
    if(number == 2) return 1;
    if(number % 2 == 0) return 0;
    for(int  i=3; number/i >= i; i+=2){
        if(number % i == 0 ) return false;
    }
    return true;
}

/**
  * Create a new hash-table with size (number of elements) not larger that the given size.
  *
  * Actual size will be based on the highest prime below the highest power of 2
  * lower or equal to the specified size, e.g. aiSze=8192 will create a hashtable
  * of 8191 elements.
  *
  * One element may be 4, 6 or 8 bytes.
  *
  * @param aiSze   size, in number of elements.
  */
JHashPos::JHashPos(int aiSze)
:  miHshColMax(COLLISION_THRESHOLD), miHshColCnt(COLLISION_THRESHOLD),
   miHshRlb(SMPSZE + SMPSZE / 2), miLodCnt(0), miHshHit(0)
{   /* get largest prime < aiSze */
    int liSzeIdx=aiSze / (sizeof(hkey)+sizeof(off_t));      // convert Mb to number of elements
    for (; ! isPrime(liSzeIdx); liSzeIdx--) ;               // find nearest lower prime

    /* allocate hashtable */
    miHshPme = liSzeIdx ;                                   // keep for refernce
    miHshSze = miHshPme * (sizeof(off_t) + sizeof(hkey));   // reconvert to bytes
    mzHshTblPos = (off_t *) calloc(miHshPme,                // allocate and initialize
                        sizeof(off_t) + sizeof(hkey)) ;
    mkHshTblHsh = (hkey *) &mzHshTblPos[miHshPme] ;         // set address of hashes

    #if debug
      if (JDebug::gbDbg[DBGHSH])
        fprintf(JDebug::stddbg, "Hash Ini sizeof=%2d+%2d=%2d, %d samples, %d bytes, address=%p-%p,%p-%p.\n",
            sizeof(hkey), sizeof(off_t), sizeof(hkey) + sizeof(off_t),
            miHshPme, miHshSze,
            mzHshTblPos, &mzHshTblPos[miHshPme], mkHshTblHsh, &mkHshTblHsh[miHshPme]) ;
    #endif
    #ifndef __MINGW32__
      if ( mzHshTblPos == null ) {
          throw bad_alloc() ;
      }
    #endif

    /* initialize hashtable */
    //@083 memset(mzHshTblPos, 0, miHshSze); // done by calloc
}

/*
 * Destructor
 */
JHashPos::~JHashPos() {
	free(mzHshTblPos);
	mzHshTblPos = null ;
	mkHshTblHsh = null ;
}

/**
 * Hashtable add
 * @param alCurHsh      Hash key to add
 * @param azPos         Position to add
 * @param aiEqlCnt      Quality of the sample
 */
void JHashPos::add (hkey akCurHsh, off_t azPos, int aiEqlCnt ){
    /* Every time the load factor increases by 1
     * - increase miHshColMax: the ratio at which we store values to achieve a uniform distribution of samples
     * - increase miHshRlb: the number of bytes to verify (reliability range) to be sure there is no match
     */
    if ( miLodCnt < miHshPme ) {
        miLodCnt ++ ;
    } else {
        miLodCnt = 0 ;
        miHshColMax += COLLISION_THRESHOLD ;
        miHshRlb += 4 ;  // try to keep a reliability of +/- 99%
    }

    /* Increase the collision strategy counter
     * - HIGH for "good" samples
     * - LOW  for low-quality samples
     */
    if (aiEqlCnt <= SMPSZE - 4)  {
        miHshColCnt+= COLLISION_HIGH ;
    } else {
        miHshColCnt+= COLLISION_LOW ;    // reduce overrides by low-quality samples
    }

    /* store key and value when the collision counter reaches the collision threshold */
    if (miHshColCnt >= miHshColMax){
        /* calculate the index in the hashtable for the given key */
        int liIdx = (akCurHsh % miHshPme) ;

        /* debug */
        #if debug
        if (JDebug::gbDbg[DBGHSH])
            fprintf(JDebug::stddbg, "Hash Add %8d " P8zd " %8"PRIhkey" %c\n",
                    liIdx, azPos, akCurHsh,
                    (mkHshTblHsh[liIdx] == 0)?'.':'!');
        #endif

        /* store */
        mkHshTblHsh[liIdx] = akCurHsh ;
        mzHshTblPos[liIdx] = azPos ;
        miHshColCnt = 0 ; // reset subsequent lost collisions counter
    }
} /* ufHshAdd */

/**
 * Hasttable lookup
 * @param alCurHsh  in:  hash key to lookup
 * @param lzPos     out: position found
 * @return true=found, false=notfound
 */
bool JHashPos::get (const hkey akCurHsh, off_t &azPos)
{ int   liIdx ;

  /* calculate key and the corresponding entries' address */
  liIdx    = (akCurHsh % miHshPme) ;

  /* lookup value into hashtable for new file */
  if (mkHshTblHsh[liIdx] == akCurHsh)  {
    miHshHit++;
    azPos = mzHshTblPos[liIdx];
    return true ;
  }
  return false ;
}

/**
 * Print hashtable content (for debugging or auditing)
 */
void JHashPos::print(){
    int liHshIdx;

    for (liHshIdx = 0; liHshIdx < miHshPme; liHshIdx ++)  {
        if (mzHshTblPos[liHshIdx] != 0) {
            fprintf(JDebug::stddbg, "Hash Pnt %12d " P8zd "-%08" PRIhkey "x\n", liHshIdx,
                    mzHshTblPos[liHshIdx], mkHshTblHsh[liHshIdx]) ;
        }
    }
}

/**
 * Print hashtable distribution  (for debugging or auditing)
 * @param azMax     Largest possible position to find
 * @param aiBck     Number of buckets
 */
void JHashPos::dist(off_t azMax, int aiBck){
    int liHshIdx;
    int liHshDiv;   // Number of positions by bucket
    int *liBckCnt;  // Number of elements by bucket
    int liIdx;

    int liSum = 0 ;
    int liMin = INT_MAX ;
    int liMax = 0;

    fprintf(JDebug::stddbg, "Hash Dist Overload    = %d\n", miHshColMax / 3);
    fprintf(JDebug::stddbg, "Hash Dist Reliability = %d\n", miHshRlb);

    liBckCnt = (int *) malloc(aiBck * sizeof(int));
    if (liBckCnt != null){
    	/* Init mem */
    	memset(liBckCnt, 0, aiBck * sizeof(int));

    	/* Fill the buckets */
    	liHshDiv = (azMax / aiBck) ;
        for (liHshIdx = 0; liHshIdx < miHshPme; liHshIdx ++)  {
            if (mzHshTblPos[liHshIdx] > 0 && mzHshTblPos[liHshIdx] <= azMax) {
            	liIdx =mzHshTblPos[liHshIdx] / liHshDiv ;
            	if (liIdx >= aiBck) {
            		liIdx = 0 ;
            	} else {
            		liBckCnt[liIdx] ++ ;
            	}
            }
        }

        /* Printout */
        for (liIdx = 0; liIdx < aiBck; liIdx ++)  {
        	liSum += liBckCnt[liIdx] ;
        	if (liBckCnt[liIdx] < liMin) liMin = liBckCnt[liIdx] ;
        	if (liBckCnt[liIdx] > liMax) liMax = liBckCnt[liIdx] ;

        	fprintf(JDebug::stddbg, "Hash Dist %8d Pos=" P8zd ":" P8zd " Cnt=%8d Rlb=%d\n",
        			liIdx, (off_t) liIdx * liHshDiv, (off_t) (liIdx + 1) * liHshDiv, liBckCnt[liIdx],
        			(liBckCnt[liIdx]==0)?-1:liHshDiv / liBckCnt[liIdx]) ;
        }
        fprintf(JDebug::stddbg, "Hash Dist Avg/Min/Max/%% = %d/%d/%d/%d\n", liSum / aiBck, liMin, liMax, 100 - (liMin * 100 / liMax));
        fprintf(JDebug::stddbg, "Hash Dist Load           = %d/%d=%d\n", liSum, miHshPme, liSum * 100 / miHshPme);
    }
} /* JHasPos::dist */

} /* namespace jojodiff */
