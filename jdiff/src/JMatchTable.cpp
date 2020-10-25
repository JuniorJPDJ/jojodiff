/*
 * JMatchTable.cpp
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
 */

#include "JMatchTable.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <new>
using namespace std;

#include "JDebug.h"

namespace JojoDiff {

// Continuous runs of 8 (> 7) equal bytes are worth the jump
// Extend to 12 to explore, so we can prefer longer runs
// These settings provide a good tradeoff between maximum equal bytes and minimum overhead bytes
#define EQLSZE 8
#define EQLMIN 4
#define EQLMAX 256

#define MAXDST 2 * 1024 * 1024  // Max compare distance | on HDD +/-40ms at 100Mb/s + 10ms seek time
#define MINDST 1024             // Min compare distance \ on SSD +/- 4ms at 1Gb/s   +  1ms seek time
#define MAXGLD 128              // Max distance for gliding matches

// Fuzzy factor: for differences smaller than this number of bytes, take the longest looking sequence
// Reason: control bytes consume byte to, so taking the longer one is better
#define FZY 0

// Inline functions
#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))
#define abs(a) ((a) < 0 ? (-a) : (a))

/*******************************************************************************
* Matching table functions
*
* The matching table contains a series of possibly matching regions between
* the two files.
*
* Because of the statistical nature of the hash table, we're not sure that the
* best solution will be found first. Consider, for example, that the samplerate
* is at 10% on files of about 10Mb and equal regions exist at positions 1000-2000
* and 1500-1500. Because 10% of 10Mb means one sample every 1024 bytes,
* it can happen that the 1000-2000 region is only discovered at positions 2000-3000
* (1000 bytes later). If the 1500-1500 region gets found first at say 1700-1700,
* and if we would not optimize the found solutions, then 500 equal bytes would
* get lost.
*
* Therefore we first memorize a number of matching positions from the hashtable,
* optimize them (look 1024 bytes back) and then select the first matching solution.
*
*******************************************************************************/

/* Construct a matching table for specified hashtable, original and new files. */
JMatchTable::JMatchTable (
    JHashPos const * const apHsh,
    JFile  * const apFilOrg,
    JFile  * const apFilNew,
    const int  aiMchSze,
    const bool abCmpAll,
    const int aiAhdMax)
: mpHsh(apHsh), miMchSze(aiMchSze < 13 ? 13 : aiMchSze), miMchFre(miMchSze)
, mpFilOrg(apFilOrg), mpFilNew(apFilNew), mbCmpAll(abCmpAll), miAhdMax(aiAhdMax)
{
    // allocate matching table
    msMch = (rMch *) malloc(sizeof(rMch) * miMchSze) ;
    #ifdef JDIFF_THROW_BAD_ALLOC
    if ( msMch == null ) {
        throw bad_alloc() ;
    }
    #endif

    // allocate and initialize the hashtable
    miMchPme = getLowerPrime(aiMchSze * 2);
    mpCol = (tMch **) calloc(miMchPme, sizeof(tMch *));
    mpGld = (tMch **) calloc(miMchPme, sizeof(tMch *));
    #ifdef JDIFF_THROW_BAD_ALLOC
      if ( mpCol == null || mpGld == null ) {
          throw bad_alloc() ;
      }
    #endif // JDIFF_THROW_BAD_ALLOC
}

/* Destructor */
JMatchTable::~JMatchTable() {
    free(msMch);
    free(mpCol);
    free(mpGld);
}

/**
 * Auxiliary function: add
 * Add given match to the array of matches:
 * - add to gliding or colliding match if possible, or
 * - add at the end of the list, or
 * - override an old match otherwise
 * Returns
 *   0   if a new entry could not be added (table full)
 *   1   if a new entry has been added and table is now full
 *   2   if a new entry has been added
 *   3   if an existing entry has been enlarged
 *   4   match was found to be invalid
 *   5   match is very good, no need to continue much longer
 *   6   match is perfect, stop now
 */
JMatchTable::eMatchReturn JMatchTable::add (
  off_t const &azFndOrgAdd,     /* match to add               */
  off_t const &azFndNewAdd,
  off_t const &azRedNew         /* current read position        */
){
    rMch * lpCur ;                     /**< current element */

    // Join colliding matches
    off_t const lzDlt = azFndOrgAdd - azFndNewAdd ;                     /**< delta key of match */
    int const liIdxDlt = abs(lzDlt) % miMchPme ;                        /**< lzDlt % miMchPme   */
    for (lpCur = mpCol[liIdxDlt] ; lpCur != null; lpCur=lpCur->ipCol){
        if (lpCur->izDlt == lzDlt){
            // remove from gliding matches
            if (lpCur->iiCnt == 1)
                delGld(lpCur) ;

            // add to colliding match
            lpCur->iiCnt ++ ;
            lpCur->izNew = azFndNewAdd ;

            break ;
        } /* if colliding */
    } /* for colliding */

    // Join gliding matches
    int liIdxGld ;                                                      /**< azOrg % miMchPme */
    if (lpCur == null){
        liIdxGld = azFndOrgAdd % miMchPme ;
        for (lpCur = mpGld[liIdxGld] ; lpCur != null; lpCur=lpCur->ipGld){
            if (lpCur->izOrg == azFndOrgAdd){
                // remove from colliding matches
                if (lpCur->iiCnt == 1)
                    delCol(lpCur) ;

                // add to gliding match
                lpCur->iiCnt ++ ;
                lpCur->izNew = azFndNewAdd ;

                // set gliding recurrence
                if (lpCur->iiGld == 0){
                    if (azFndNewAdd <= lpCur->izBeg + SMPSZE)
                        lpCur->iiGld = (azFndNewAdd - lpCur->izBeg) ;
                    else
                        lpCur->iiGld = SMPSZE ;
                }

                break ;
            } /* if gliding */
        } /* for gliding */
    } /* join gliding */

    // remove first renewed item from the oldlist
    if (lpCur != null && mpOld == lpCur){
        mpOld = mpOld->ipNxt ;  // remove from oldlist
        nextold(azRedNew) ;     // puts a reusable element in front of mpOld
        addNew(lpCur) ;         // add to the newlist
    }

    // allocate new element
    if (lpCur == null){
        // get free element
        if (miMchFre > 0){
            // take unused element
            miMchFre--;
            lpCur = &msMch[miMchFre];
        } else if (mpOld != null) {
            // sanity check
            #if debug
            if ((mpOld != null) &&
                (mpOld->iiCmp != -2) &&
                ((mpOld->iiCmp != 0 && mpOld->izNew >= azRedNew) ||
                 (mpOld->iiCmp > 0 && mpOld->izTst + mpOld->iiCmp > azRedNew)))
                    fprintf(JDebug::stddbg, "Mch Add (" P8zd ">" P8zd "<" P8zd") Reusing valid new element %d !\n",
                        mpOld->izOrg, mpOld->izDlt, mpOld->izNew, mpOld->iiCmp) ;
            #endif

            // reuse old element
            lpCur = mpOld ;
            mpOld = mpOld->ipNxt ;
            nextold(azRedNew) ;     // prepare next old element

            // remove old element from gliding/colliding lists
            //@if (lpCur->iiCmp != -3){
                if (lpCur->iiCnt == 1 || lpCur->iiGld == 0)
                    delCol(lpCur) ;
                if (lpCur->iiCnt == 1 || lpCur->iiGld != 0)
                    delGld(lpCur) ;
            //}

            // debug reporting
            #if debug
            if (JDebug::gbDbg[DBGMCH])
              fprintf(JDebug::stddbg,
                        "Del         [%2d:" P8zd ">" P8zd "<" P8zd "~" P8zd "#%4d+%4d] bse=%" PRIzd "\n",
                        lpCur->iiGld,
                        lpCur->izOrg, lpCur->izDlt, lpCur->izBeg, lpCur->izNew, lpCur->iiCnt, lpCur->iiCmp,
                        azRedNew) ;
            #endif
        } else {
            return Error ;  // should not occur
        }

        // fill out the form
        lpCur->izOrg = azFndOrgAdd ;
        lpCur->izNew = azFndNewAdd ;
        lpCur->izBeg = azFndNewAdd ;
        lpCur->izDlt = lzDlt ;
        lpCur->iiCnt = 1 ;
        lpCur->iiGld = 0 ;
        lpCur->iiCmp = 0 ;
        lpCur->izTst = -1 ;

        // add to colliding hashtable
        lpCur->ipCol = mpCol[liIdxDlt];
        mpCol[liIdxDlt] = lpCur ;

        // add to gliding hashtable
        lpCur->ipGld = mpGld[liIdxGld] ;
        mpGld[liIdxGld] = lpCur ;
    }

    // evaluate new (iiCnt==1) or skipped (iiCmp==-3) elements
    eMatchReturn liRet = Enlarged; /**< return code */
    if ((lpCur->iiCnt == 1 || lpCur->iiCmp == -3)){
        // reactivate skipped elements
        if (lpCur->iiCmp == -3)
            lpCur->iiCmp = 0;

        switch (liRet = isGoodOrBest(azRedNew, lpCur)){
        case Invalid:
            if (lpCur->izTst >= lpCur->izNew){
                // Invalids are marked -2 for reuse (if they were completely evaluated)
                miHshRpr++ ;
                lpCur->iiCmp = -2 ;  // mark as invalid for reuse

                // put new invalid elements in front of the new list to be reused
                if (lpCur->iiCnt == 1) {
                    if (mpNew == null)
                        mpLst = lpCur ;
                    lpCur->ipNxt = mpNew ;
                    mpNew = lpCur ;
                }

                break ;
            }
            // Invalids that were not fully evaluated are treaded like valids, so no break
        case Valid:
        case Good:
        case Best:
            // put new valid elements on the new elements list
            if (lpCur->iiCnt == 1)
                addNew(lpCur) ;
            break ;

        case Enlarged:
        case Error:
        case Full:
            // should not occur
            break ;
        } /* switch */

        // debug reporting
        #if debug
        if (JDebug::gbDbg[DBGMCH])
          fprintf(JDebug::stddbg,
                    "Add         [  :" P8zd ">" P8zd "<" P8zd "] bse=%" PRIzd " ret=%d\n",
                    azFndOrgAdd, lzDlt, azFndNewAdd, azRedNew, liRet) ;
        #endif
    } /* if enlarged else add */

    // Check if there's still room for new elements
    if (miMchFre == 0 && mpOld == null)
        return Full ;      // table is full
    else
        return liRet ;     // Good, bad or ugly :-)

} /* add() */

/**
* @brief Prepare next reusable old element
* @return true=found, false=notfound
*/
bool JMatchTable::nextold(off_t const azRedNew){
    rMch * lpCur ;  /* current element */

    // find first old item on old list
    while (mpOld != null)
        if ((mpOld->iiCmp <= -2))
            break ; // skipped or marked invalid elemnts
//        else if (mpOld->izNew < lzLmt && mpOld->izTst + mpOld->iiCmp < lzLmt)
//            break ; // valid but not useful anymore
//        else if (mpOld->iiCmp <= 0 && mpOld->izNew - miRlb < lzLmt)
//            break ; // reuse invalid items
        else if ((mpOld->izNew >= mzOld) ||
                    (mpOld->iiCmp > 0 && mpOld->izTst + mpOld->iiCmp >= mzOld) ||
                    (mpOld->iiCmp >= EQLMAX && mpOld->izTst + mpOld->iiCmp + miRlb > mzOld)) {
            // not an old item: remove from oldlist
            lpCur = mpOld ;
            mpOld = mpOld->ipNxt ;

            // add to newlist ;
            addNew(lpCur);
        } else {
            break ;
        }

    // reuse new invalid items (marked with iiCmp == -2)
    if (mpOld == null && mpNew != null){
        mpLst->ipNxt = null ;
        if (mpNew->iiCmp <= -2){
            // Remove from new list
            lpCur = mpNew ;
            mpNew = mpNew->ipNxt ;

            // Move to old list
            lpCur->ipNxt = mpOld ;
            mpOld = lpCur ;

        }
    }

    // debug-verify
    #if debug
    if (JDebug::gbDbg[DBGMCH]){
        if (mpOld != null){
            off_t lzChkNew = azRedNew ;
            off_t lzChkOrg = (mpOld->iiGld > 0) ? mpOld->izOrg : lzChkNew + mpOld->izDlt ;
            int liCmp = check(lzChkOrg, lzChkNew, 32) ;
            if (liCmp > 0){
                fprintf(JDebug::stddbg, "Mch Nxt Err @%" PRIzd
                        " for %" PRIzd ">%" PRIzd "<%" PRIzd "~%" PRIzd "/%d"
                        " cnt=%d cmp@%" PRIzd "=%d chk=%d is not invalid !\n",
                        azRedNew, lzChkOrg, mpOld->izDlt, lzChkNew, mpOld->izBeg, mpOld->iiGld,
                        mpOld->iiCnt, mpOld->izTst, mpOld->iiCmp, liCmp) ;
                liCmp = 0; // for breakpoint
            }
        }
    }
    #endif

    return mpOld != null ;
}

/**
* @brief Add element to new list
*/
void JMatchTable::addNew(rMch *lpCur){
    if (mpNew == null)
        mpNew = lpCur ;
    else
        mpLst->ipNxt = lpCur ;
    mpLst = lpCur ;     // attention: mpLst->ipNxt is a dangling pointer (saves one assignment) !!!
}

/**
 * @brief Cleanup, check free space and fastcheck best match.
 *
 * @param       azRedNew    Current reading position
 * @param       azBseNew    Cleanup all mathes before this position
 *
 * @return Full, GoodMatch or Added
 * ---------------------------------------------------------------------------*/
JMatchTable::eMatchReturn JMatchTable::cleanup ( off_t const azBseOrg, off_t const azRedNew, int const liBck ){
    rMch *lpCur ;               /**< Current element from matchtable    */

    // get actual reliability distance
    miRlb = mpHsh->get_reliability();

    // join old and new lists
    if (mpNew != null){
        mpLst->ipNxt = mpOld ;
        mpOld = mpNew ;
    }
    mpNew = null ;
    mpLst = null ;

    // sanity checks
    #if debug
    int liOld = 0 ;
    int liNew = 0 ;
    if (JDebug::gbDbg[DBGMCH]){
        for (lpCur = mpNew; lpCur != null; lpCur = lpCur->ipNxt) liNew++ ;
        for (lpCur = mpOld; lpCur != null; lpCur = lpCur->ipNxt) liOld++ ;
        if (miMchFre + liOld + liNew != miMchSze)
            fprintf(JDebug::stddbg, "Mch Cln Wrong table size %d+%d+%d != %d !\n", liNew, liOld, miMchFre, miMchSze) ;
    }
    #endif

    // evaluate existing entries
    mpBst = null ;  // reset best pointer
    for (lpCur = mpOld ; lpCur != null; lpCur = lpCur->ipNxt){
//        if ((lpCur->iiCmp <= 0 && lpCur->izNew + miRlb < azRedNew) ||
//            (lpCur->iiCmp > 0 && lpCur->izTst + lpCur->iiCmp + 2 * miRlb < azRedNew) ||
//            (lpCur->iiCmp >= EQLMAX && lpCur->izTst + lpCur->iiCmp + 3 * miRlb < azRedNew)) {
//            // Very old items are skipped
//            lpCur->iiCmp = -3 ;
//        } else {
        if ((lpCur->izNew + miAhdMax > azRedNew) ||
            (lpCur->iiCmp > 0 && lpCur->izTst + lpCur->iiCmp >= azRedNew + EQLMIN) ||
            (lpCur->iiCmp >= EQLMAX && lpCur->izTst + lpCur->iiCmp >= azRedNew)){
            isGoodOrBest(azRedNew, lpCur) ;
        } else {
            // Mark very old elements as skipped
            lpCur->iiCmp = -3 ;
        }
    }

    // invalid items are not always old, so we still need to check
    nextold(azRedNew) ;

    // redo sanity checks
    #if debug
    if (JDebug::gbDbg[DBGMCH]){
        liOld = 0 ;
        liNew = 0 ;
        for (lpCur = mpNew; (lpCur != null && lpCur != mpLst->ipNxt); lpCur = lpCur->ipNxt) liNew++ ;
        for (lpCur = mpOld; lpCur != null; lpCur = lpCur->ipNxt) liOld++ ;
        if (miMchFre + liNew + liOld != miMchSze)
            fprintf(JDebug::stddbg, "Mch Cln Wrong table size %d+%d+%d != %d !\n",
                    liNew, liOld, miMchFre, miMchSze) ;
    }
    #endif

    // issue return value
    if (mpOld == null && miMchFre == 0)
        return Full ;
    else if (mpBst != null && mzBstNew == azRedNew) {
        if (miBstCmp >= EQLMAX)
            return Best ;
        else if (miBstCmp >= EQLSZE)
            return Good ;
        else
            return Valid ;
    } else
        return Valid ;
} /* cleanup() */

/**
 * Get the best match from the array of matches
 */
bool JMatchTable::getbest (
  off_t const &azRedOrg,    // current read position on original file
  off_t const &azRedNew,    // current read position on new file
  off_t &azBstOrg,          // best position found on original file
  off_t &azBstNew           // best position found on new file
) {
    // get best match
    if (mpBst != null){
        azBstOrg = mzBstOrg ;
        azBstNew = mzBstNew ;
    }

    #if debug
    if (JDebug::gbDbg[DBGMCH]){
        if (mpBst == null){
            fprintf(JDebug::stddbg, "Match Failure at %" PRIzd "\n", azRedNew) ;
        } else if ((azRedNew != azBstNew)){
            fprintf(JDebug::stddbg, "Suboptimal Match at %" PRIzd ": from %" PRIzd ", length %d\n",
                    azRedNew, azBstNew, mpBst->iiCmp);
        } else if ((mpBst->iiCmp < EQLSZE)) {
            fprintf(JDebug::stddbg, "Short Match at %" PRIzd ": from %" PRIzd ", length %d\n",
                    azRedNew, azBstNew, mpBst->iiCmp);
        }
    }
    #endif

    return mpBst != null ;
} /* getbest() */

/**
* @brief Evaluate a match
*/
JMatchTable::eMatchReturn JMatchTable::isGoodOrBest(
    off_t const azRedNew,       /**< Current read position */
    rMch *lpCur                 /**< Element to evaluate */
){
    int  liCurCmp=0 ;       /**< current match compare state                  */
    int  liCurCnt=-1 ;      /**< current match confirmation count             */
    bool lbGld ;            /**< gliding match under investigation              */

    off_t lzTstNew ;        /**< test/found position in new file */
    off_t lzTstOrg ;        /**< test/found position in old file */
    off_t lzDst ;           /**< distance: number of bytes to compare before failing */

    /* check if the match yields a solution on this position */
    lzTstNew = azRedNew ; // start test at current read position

    /* calculate the test position on the original file by applying izDlt */
    lbGld = calcPosOrg(lpCur, lzTstOrg, lzTstNew);

    /* reuse earlier compare result */
    lzDst = -1 ;
    if (lzTstNew <= lpCur->izTst) {
        // The test position is still before the previous test result,
        // so reuse the previous test result.
        liCurCmp = lpCur->iiCmp ;
        if (liCurCmp <= -2)
            liCurCmp=0;
        if (lbGld){
            lzTstNew = lpCur->izTst ;
            lzTstOrg = lpCur->izOrg ;
        } else {
            lzTstOrg = lzTstOrg + (lpCur->izTst - lzTstNew) ;
            lzTstNew = lpCur->izTst ;
            lzTstOrg = lpCur->izTst + lpCur->izDlt ;
        }
    } else if ((! lbGld) && lpCur->iiCmp > 0 && lpCur->iiCmp - (lzTstNew - lpCur->izTst) > EQLMIN) {
        // The new test position is within the previous test result.
        // Report the remaining length
        liCurCmp = lpCur->iiCmp - (lzTstNew - lpCur->izTst) ;
        if (liCurCmp <= -2)
            liCurCmp=0;
    } else {
        // The previous test result cannot be reused: check (again)
        // determine number of bytes to check
        lzDst = lpCur->izBeg - lzTstNew ;       // at least till the first match
        //if (mpBst != null && lzDst > mzBstNew - lzTstNew + FZY)
        //    lzDst = mzBstNew - lzTstNew + FZY ; // no need to be better than best
        if (lzDst < MINDST)
            lzDst = MINDST ;
        else if (lzDst > MAXDST)
            lzDst = MAXDST ;

        // check
        liCurCmp = check(lzTstOrg, lzTstNew, lzDst, lpCur->iiGld,
                         mbCmpAll ? JFile::HardAhead : JFile::SoftAhead) ;

        // store result
        lpCur->izTst = lzTstNew ;
        if (lpCur->iiCmp == -2 && liCurCmp <= 0)
            #if debug
            lpCur->iiCmp = -2 // for breakpoint
            #endif // debug
            ; // don't erase an invalid marker
        else {
            lpCur->iiCmp = liCurCmp ;
        }
    }

    // Debug doublecheck
    #if debug
    if (JDebug::gbDbg[DBGMCH]){
        if (lzDst == -1){
            off_t lzChkOrg = lzTstOrg ;
            off_t lzChkNew = lzTstNew ;
            int liChkCmp = check(lzChkOrg, lzChkNew, 0, lpCur->iiGld,
                             mbCmpAll ? JFile::HardAhead : JFile::SoftAhead) ;
            if (liChkCmp == 0 && liCurCmp == 0) ; // ok
            else if ((liChkCmp != liCurCmp) ||
                    (lzChkOrg != lzTstOrg) ||
                    (lzChkNew != lzTstNew))
                fprintf(JDebug::stddbg, "Mch Chk Err :%" PRIzd "=%" PRIzd "+%d "
                        "Chk: %" PRIzd "=%" PRIzd "+%d!\n",
                        lzTstOrg, lzTstNew, liCurCmp,
                        lzChkOrg, lzChkNew, liChkCmp) ;
        }
    }
    #endif // debug

    // If iiCmp==EQLMAX, the test result probably extends till izNew
    if (lpCur->iiCmp == EQLMAX && lpCur->izNew > lzTstNew + liCurCmp) {
        liCurCmp += lpCur->izNew - lzTstNew ;
    }

    /* Handle EOB: estimate liCurCmp from hash counts */
    //@ TODO:
    //@ - for EOB: no solution before EOB !
    if (liCurCmp < 0){
        // EOB was reached, so rely on info from the hashtable: iiCnt, izBeg and izNew
        if (liCurCnt < 0)
            liCurCnt = (lpCur->iiGld > 0) ? lpCur->iiCnt / 2 : lpCur->iiCnt ;

        if (lzTstNew <= lpCur->izBeg) {
            // We're still before the first detected match,
            // so a potential solution probably starts at given match
            lzTstNew = lpCur->izBeg ;
            liCurCmp = liCurCnt ;
        } else if (lzTstNew <= lpCur->izNew) {
            // We're in between the first and last detected match:
            // Prorate liCurCmp, but never lower than 1
            liCurCmp = liCurCnt * (lpCur->izNew - lzTstNew)
                          / (lpCur->izNew - lpCur->izBeg) ;
            if (liCurCmp <= 1)
                liCurCmp = 1 ;
        }
        if (liCurCmp > 0){
            // reduce hashtable match, real compares are better
            //if (liCurCmp > 2)
            liCurCmp = 1 + liCurCmp / 2 ;

            // calculate corresponding lzPosOrg
            calcPosOrg(lpCur, lzTstOrg, lzTstNew) ;
        } else {
            liCurCmp = -1;
        }
    }

    /* evaluate: keep the best solution */
    if (liCurCmp > 0){
        if (mpBst == NULL)
            // first one, take it
            mpBst=lpCur ;
        else if (liCurCmp < 2 && miBstCmp > 4)
            // avoid using low-quality matches (liCurCmp < 2 == low quality)
            ; // do nothing //@liCurCnt = -1 ;  // breakpoint
        else if (miBstCmp < 2 && liCurCmp > 4)
            // avoid using low-quality matches (liBstCmp < 2 == low quality)
            mpBst=lpCur ;
        else if (lzTstNew + FZY < mzBstNew)
            // new one is clearly better (nearer)
            mpBst=lpCur ;
        else if (lzTstNew <= mzBstNew + FZY) {
            // maybe better (nearer): check in more detail
            if (lzTstNew - liCurCmp < mzBstNew - miBstCmp) {
                // new one is longer
                mpBst=lpCur ;
            } else if (lzTstNew - liCurCmp == mzBstNew - miBstCmp) {
                // If all else is equal, then rely on the hash counter
                if (liCurCnt < 0)
                    liCurCnt = (lpCur->iiGld > 0) ? lpCur->iiCnt / 2 : lpCur->iiCnt ;
                int liBstCnt = (mpBst->iiGld > 0) ? mpBst->iiCnt / 2 : mpBst->iiCnt ;
                if (liCurCnt > liBstCnt)
                    // higher hash-match counter = probably longer
                    mpBst=lpCur ;
            }
        }

        if (mpBst==lpCur){
            mzBstNew = lzTstNew ;
            mzBstOrg = lzTstOrg ;
            miBstCmp = liCurCmp ;

            // Determine the limit for being old:
            // - current mpBst runs till izTst + iiCmp, so all matches before this point are useless
            // - except if a new mpBst is found that is earlier but shorter
            // - therefore, miRlb is used as safety range
            mzOld = mpBst->izTst + mpBst->iiCmp - miRlb ;
            if (mzOld < azRedNew)
                mzOld = azRedNew ;
        }
    } /* if liCurCmp > 0 */

    // debug feedback
    #if debug
    if (JDebug::gbDbg[DBGMCH]){
        fprintf(JDebug::stddbg,
            "%s %5d %c [%2d:" P8zd ">" P8zd "<" P8zd "~" P8zd "#%4d:" P8zd "+%4d] "
            "bse=%" PRIzd " dst=%" PRIzd " fnd=%" PRIzd "=%" PRIzd "(%d)\n",
            (liCurCmp != 0) ? "Val" : (lpCur->izNew < azRedNew) ? "Old" : "Inv",
            liCurCmp, (mpBst == lpCur)?'*':' ',
            lpCur->iiGld,
            lpCur->izOrg, lpCur->izDlt, lpCur->izBeg, lpCur->izNew,
            lpCur->iiCnt, lpCur->izTst, lpCur->iiCmp,
            azRedNew, lzDst, lzTstOrg, lzTstNew, lzTstNew - azRedNew) ;

        // Measure old distance
        static long llOldMax = 0;
        if (liCurCmp > 0){
            if (azRedNew - lpCur->izNew > llOldMax){
                llOldMax = azRedNew - lpCur->izNew ;
                fprintf(JDebug::stddbg, "Mch Old Max Distance = %ld\n", llOldMax) ;
            }
        }
    }
    #endif

    if (liCurCmp == 0)
        return Invalid ;
    else if (lzTstNew != azRedNew)
        return Valid ;
    else if (liCurCmp >= EQLMAX)
        return Best ;
    else if (liCurCmp >= EQLSZE)
        return Good ;
    else
        return Valid ;
} /* isGoodOrBest */

/**
* @brief   Calculate position on original file corresponding to given new file position.
*
* @param   rMch     *apCur    Match (in)
* @param   off_t    azTstOrg  Position on org file (out only)
* @param   off_t    azTstNew  Position on new file (in/out, adapted if azTstOrg would become negative)
*/
bool JMatchTable::calcPosOrg(rMch * const lpCur, off_t &azTstOrg, off_t &azTstNew) const {
    /* calculate the test position on the original file by applying izDlt */
    if ((lpCur->iiGld > 0) && (azTstNew >= lpCur->izBeg)) {
        // we're within a gliding match
        azTstOrg = lpCur->izOrg ;
        return true ;
    } else {
        // we're before or after a gliding match
        // or on a colliding match
        if (azTstNew + lpCur->izDlt >= 0) {
            azTstOrg = azTstNew + lpCur->izDlt;
        } else {
            // azTstOrg would become negative, so advance azTstNew till azTstOrg == 0
            //   azTstOrg = azTstNew + lpCur->izDlt;
            //   azTstNew = azTstNew - azTstOrg ;
            //   azTstOrg = 0 ;
            // or shorter:
            azTstNew = - lpCur->izDlt ;
            azTstOrg = 0;
        }
        return false ;
    }
}

/**
 * @brief Verify and optimize matches:
 * Searches at given positions for a run of minimum EQLMIN equal bytes.
 * Searching continues for the full length unless soft-reading is specified
 * and the end-of-buffer is reached.
 *
 * @param   &rzPosOrg   in/out  position on first file
 * @param   &rzPosNew   in/out  position on second file
 * @param   aiLen       in      number of bytes to compare
 * @param   abGld       in      check gliding match ?
 * @param   aiSft       in      1=hard read, 2=soft read
 *
 * @return  0     : no equal bytes found
 * @return  -1    : EOB reached, no equal bytes found
 * @return  >= 4  : number of equal bytes found
 */
int JMatchTable::check (
    off_t &azPosOrg, off_t &azPosNew,
    int aiLen, int aiGld, const JFile::eAhead aiSft
) const {
    int lcOrg=0 ;   /**< Byte from source file */
    int lcNew=0 ;   /**< Byte from destination file */
    int liEql=0 ;   /**< Equal bytes counter */

    #if debug
    if (JDebug::gbDbg[DBGCMP])
        fprintf(JDebug::stddbg, "Cmp %s (" P8zd "," P8zd ",%4d,%d): ",
                (aiGld != 0) ? "Gld" : "Col",
                azPosOrg, azPosNew, aiLen, aiSft) ;
    #endif

    /* Compare bytes */
    for ( ; liEql < EQLMAX; aiLen--)
    {
        if ((lcOrg = mpFilOrg->get(azPosOrg, aiSft)) < 0){
            break;
        } else if ((lcNew = mpFilNew->get(azPosNew, aiSft)) < 0) {
            break;
        } else if (lcOrg == lcNew) {
            azPosOrg ++ ;
            azPosNew ++ ;
            liEql ++ ;
        } else if (liEql >= EQLSZE) {
            break ;
        } else if (aiLen <= 0) {
            break ;
        } else {
            azPosNew ++ ;
            if (aiGld != 0)
                azPosOrg -= liEql ;
            else
                azPosOrg ++ ;
            liEql = 0;
        }
    }

    #if debug
        if (JDebug::gbDbg[DBGCMP])
            fprintf( JDebug::stddbg, "" P8zd " " P8zd " %2d %s (%c)%02x == (%c)%02x\n",
                     azPosOrg - liEql, azPosNew - liEql, liEql,
                     (liEql>=EQLMIN)?"OK!":(lcOrg==EOB || lcNew==EOB)?"EOB":"NOK",
                     (lcOrg>=32 && lcOrg <= 127)?lcOrg:' ',(uchar)lcOrg,
                     (lcNew>=32 && lcNew <= 127)?lcNew:' ',(uchar)lcNew);
    #endif

    if (liEql > EQLMIN){
        azPosOrg -= liEql ;
        azPosNew -= liEql ;
        return liEql ;
    } else if (lcOrg == EOB || lcNew == EOB){
        // EOB reached
        return -1  ;
    } else {
        // No equal bytes found
        return 0 ;
    }
} /* check() */

/**
* @brief    Delete element from gliding hashtable
*/
void JMatchTable::delGld(rMch * const apGld) {
    int const liIdx = apGld->izOrg % miMchPme ;
    rMch * lpGld = mpGld[liIdx] ;
    if (lpGld == apGld)
        mpGld[liIdx] = apGld->ipGld ;
    else
        while (lpGld != null){
            if (lpGld->ipGld == apGld){
                lpGld->ipGld = apGld->ipGld ;
                break ;
            } else {
                lpGld=lpGld->ipGld;
            }
        }
}

/**
* @brief    Delete element from colliding hashtable
*/
void JMatchTable::delCol(rMch * const apCol ) {
    int const liIdx = abs(apCol->izDlt) % miMchPme ;
    rMch * lpCol = mpCol[liIdx] ;
    if (lpCol == apCol)
        mpCol[liIdx] = apCol->ipCol ;
    else
        while (lpCol != null){
            if (lpCol->ipCol == apCol){
                lpCol->ipCol = apCol->ipCol ;
                break ;
            } else {
                lpCol=lpCol->ipCol;
            }
        }
}


/**
* @brief Get number of hash repairs (matches repaired by comparing).
*/
int JMatchTable::getHshRpr ( ) { return miHshRpr ; }

} /* namespace JojoDiff */

/*==============================================
August 2020 v083g test with bkocomu.0000 - 0009
EQLSZE / EQLMIN  data+overhead=total  with -r option
     8 / 3     37591 + 51558 = 89149  23559 + 54467 = 78026
     8 / 4     30607 + 71764 =102371  26047 + 50742
     8 / 7     37741 + 48938 = 86679
    12 / 6     37077 + 49615 = 86692  30995 + 45120
    12 / 7     37743 + 48645 = 86388  32549 + 44060 = 76609
--> 12 / 8     38338 + 47544 = 85882  33102 + 43640 = 76742

FY (Fuzyness) with 12/7
 1  37804 + 48376 = 86180
 2  37954 + 47967 = 85921  <--
 3  38307 + 47649 = 85956
 4  38467 + 47547 = 86014

FY=2 EQL/MIN=8/12 : 38518 + 47049 = 85567
===============================================*/
