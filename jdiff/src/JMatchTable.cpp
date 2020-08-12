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
#define EQLSZE 12
#define EQLMIN 8
#define EQLMAX 256      //@ make this a dynamic parameter

// Fuzzy factor: for differences smaller than this number of bytes, take the longest looking sequence
// Reason: control bytes consume byte to, so taking the longer one is better
#define FZY 0

// A run of EQLSZE may actually be longer, so give it a boost
#define EQLBST 0

// Inline functions
#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

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
* Therefore we first memorize a number of matching postions found with the hashtable,
* optimize them (look 1024 bytes back) and then select the first matching solution.
*
* ufMchIni      Initializes the matching table
* ufMchAdd      Adds a potential solution to the mach table
* ufMchFre      Checks if there is room for new solutions
* ufMchBst      Optimizes the matches and returns the best one
*
*******************************************************************************/

int JMatchTable::siHshRpr = 0;     /* Number of repaired hash hits (by comparing) */

/* Construct a matching table for specified hashtable, original and new files. */
JMatchTable::JMatchTable (
    JHashPos const * const apHsh,
    JFile  * const apFilOrg,
    JFile  * const apFilNew,
    const int  aiMchSze,
    const bool abCmpAll)
: mpHsh(apHsh), mpFilOrg(apFilOrg), mpFilNew(apFilNew), miMchSze(aiMchSze), mbCmpAll(abCmpAll)
{
    if (miMchSze < 13)
        miMchSze = 13;

    // allocate matching table
    msMch = (rMch *) malloc(sizeof(rMch) * miMchSze) ;
    #ifdef JDIFF_THROW_BAD_ALLOC
    if ( msMch == null ) {
        throw bad_alloc() ;
    }
    #endif

    // allocate and initialize the hashtable
    miMchPme = getLowerPrime(aiMchSze) * 2;
    mpMch = (tMch **) calloc(miMchPme, sizeof(tMch *));

    // initialize other values
    mpMchGld = null;
    mzGldDlt = 0 ;
    mpMchFre = null ;
    miMchFre = miMchSze;
}

/* Destructor */
JMatchTable::~JMatchTable() {
    free(msMch);
    free(mpMch);
}

/* -----------------------------------------------------------------------------
 * Auxiliary function: ufMchAdd
 * Add given match to the array of matches:
 * - add to gliding or colliding match if possible, or
 * - add at the end of the list, or
 * - override an old match otherwise
 * Returns
 *  -1   if a new entry could not be added (table full)
 *   0   if a new entry has been added and table is now full
 *   1   if a new entry has been added
 *   2   if an existing entry has been enlarged
 *   3   match was found to be invalid
 *   4   match is very good, no need to contionue much longer
 *   5   match is perfect, stop now
 * ---------------------------------------------------------------------------*/
int JMatchTable::add (
  off_t const &azFndOrgAdd,     /* match to add               */
  off_t const &azFndNewAdd,
  off_t const &azRedNew         /* current read position        */
){
    off_t lzDlt ;            /* delta key of match */
    rMch *lpCur ;            /* current item */
    int liIdx ;              /* lzDlt % miMchMax */

    lzDlt = azFndOrgAdd - azFndNewAdd ;

    // Check for a gliding match
    if (mpMchGld != null){
        // A gliding match occurs when a same source sample matches subsequent destination samples
        // Typically a series of zero's or same-value bytes.
        // Only retain the first such match, to avoid to clutter up the match table with low quality matches
        if ((lzDlt == mzGldDlt) || (lzDlt < mzGldDlt && mpMchGld->izOrg == azFndOrgAdd)) {
            if (lzDlt == mzGldDlt)
                mzGldDlt--;
            else
                mzGldDlt = lzDlt-1 ;

            mpMchGld->iiTyp = -1 ;
            mpMchGld->iiCnt ++ ;
            mpMchGld->izNew = azFndNewAdd ;
            return 2 ;
        }
    }

    // add or override colliding match
    liIdx = (lzDlt >= 0 ? lzDlt : - lzDlt) % miMchPme ;
    for (lpCur = mpMch[liIdx] ; lpCur != null; lpCur = lpCur->ipNxt){
        if (lpCur->izDlt == lzDlt){
            // add to colliding match
            lpCur->iiCnt ++ ;
            lpCur->iiTyp = 1 ;
            lpCur->izNew = azFndNewAdd ;
            lpCur->izOrg = azFndOrgAdd ;

            // colliding cannot be gliding
            if (mpMchGld == lpCur)
                mpMchGld = null ;

            return 2 ;
        }
    } /* for */

    // create new match
    if (miMchFre > 0 || mpMchFre != null){
        // soft-verify the new match
        off_t lzTstNew = azRedNew ;                // start test at current read position
        int liDst = azFndNewAdd - lzTstNew + 1 ;   // number of bytes to check before failing
        if (liDst < SMPSZE) liDst = SMPSZE ;       // verify at least SMPSZE bytes

        /* calculate the test position on the original file by applying izDlt */
        off_t lzTstOrg =  lzTstNew + lzDlt ;
        if (lzTstOrg < 0) {
            lzTstNew -= lzTstOrg ;
            lzTstOrg = 0 ;
        }

        /* check */
        int liCurCmp = check(lzTstOrg, lzTstNew, liDst, mbCmpAll ? JFile::HardAhead : JFile::SoftAhead) ;
        if (liCurCmp == 0){
            // check failed, don't add to the table
            if (azFndNewAdd >= azRedNew) siHshRpr++ ;
            return 3 ;
        }

        // remove from free-list
        if (mpMchFre != null){
            lpCur = mpMchFre ;
            mpMchFre = mpMchFre->ipNxt ;
        } else {
            miMchFre--;
            lpCur = &msMch[miMchFre];
        }

        // add to hashtable
        lpCur->ipNxt = mpMch[liIdx];
        mpMch[liIdx] = lpCur ;

        // potential gliding match
        mpMchGld = lpCur ;
        mzGldDlt = lzDlt - 1 ;

        // fill out the form
        lpCur->izOrg = azFndOrgAdd ;
        lpCur->izNew = azFndNewAdd ;
        lpCur->izBeg = azFndNewAdd ;
        lpCur->izDlt = lzDlt ;
        lpCur->iiCnt = 1 ;
        lpCur->iiTyp = 0 ;
        if (liCurCmp == 0){
            lpCur->izTst = -1 ;
            lpCur->iiCmp = 0 ;
        } else {
            /* store check result for later */
            lpCur->izTst = lzTstNew ;
            lpCur->iiCmp = liCurCmp ;

            /* if this is a very good match, then reduce lookahead...*/
            if ((lzTstNew == azRedNew)){
                if (liCurCmp >= EQLMAX) {
                    mpMchBst = lpCur ;
                    return 5;
                } else if (liCurCmp >= EQLSZE) {
                    //mpMchBst = lpCur ;
                    return 4;
                }
            }
        }

        #if debug
        if (JDebug::gbDbg[DBGMCH])
          fprintf(JDebug::stddbg, "Mch Add (" P8zd "," P8zd ")  Bse (%" PRIzd ")\n",
                  azFndOrgAdd, azFndNewAdd, azRedNew) ;
        #endif

        return (mpMchFre != null || miMchFre > 0) ; // still place or not ?
    } else {
        #if debug
        if (JDebug::gbDbg[DBGMCH]) fprintf(JDebug::stddbg, "Mch (" P8zd ", " P8zd ") Ful\n",
                  azFndOrgAdd, azFndNewAdd) ;
        #endif

        return -1 ; // not added
    }
} /* add() */

/**
 * @brief Cleanup, check free space and fastcheck best match.
 *
 * @param       azRedNew    Current reading position
 * @param       azBseNew    Cleanup all mathes before this position
 *
 * @return < 0  Negated number valid matches: one of the matches meets azBseNew
 *         > 0  Number of valid matches
 * ---------------------------------------------------------------------------*/
int JMatchTable::cleanup ( off_t const azRedNew, int const liBck ){
    rMch *lpCur ;
    rMch *lpPrv ;
    int  liFnd=0 ;      /* number of valid matches left */
    bool lbBst=false;   /* a best match is available    */

    mpMchBst = null ; // clear best match
    for (int liIdx = 0; liIdx < miMchPme; liIdx ++) {
        lpPrv = null ;
        lpCur=mpMch[liIdx];
        while(lpCur != null)  {
            // if bad or old
            if ((lpCur->izNew < azRedNew - liBck) || (lpCur->iiCnt < 2 && lpCur->iiCmp<=0)) {
                // remove from list
                if (lpPrv == null)
                    mpMch[liIdx] = lpCur->ipNxt ;
                else
                    lpPrv->ipNxt = lpCur->ipNxt ;

                // add to free-list
                lpCur->ipNxt = mpMchFre ;
                mpMchFre = lpCur ;

                // next after remove
                if (lpPrv == null){
                    lpCur = mpMch[liIdx] ;
                } else {
                    lpCur = lpPrv->ipNxt ;
                }
            } else {
                liFnd ++ ;
                if ((lpCur->iiCmp >= EQLSZE)
                    && (lpCur->izTst <= azRedNew)
                    && (lpCur->izTst + lpCur->iiCmp > azRedNew)){
                    // There's a good compared match valid on position azRedNew, so
                    lbBst=true;  // no reason to search a lot
                } else if ((lpCur->iiCmp < 0)
                    && (lpCur->izTst < 0)
                    && (lpCur->iiCnt > 12)
                    && (lpCur->izBeg <= azRedNew)
                    && (lpCur->izNew >= azRedNew))
                {
                    // There's a good confirmed match on position azRedNew, so
                    lbBst=true ; // no reason to search a lot
                }
                lpPrv = lpCur ;
                lpCur = lpCur->ipNxt ;
            }
        }
    }

    return lbBst ? -liFnd : liFnd ;
} /* cleanup() */

/**
* @brief   Calculate position on original file corresponding to given new file position.
*
* @param   rMch     *apCur    Match (in)
* @param   off_t    azTstOrg  Position on org file (out only)
* @param   off_t    azTstNew  Position on new file (in/out, adapted if azTstOrg would become negative)
*/
void JMatchTable::calcPosOrg(rMch *apCur, off_t &azTstOrg, off_t &azTstNew) const {
    /* calculate the test position on the original file by applying izDlt */
    if ((apCur->iiTyp < 0) && (azTstNew >= apCur->izBeg)){
        // we're within a gliding match
        azTstOrg = apCur->izOrg ;
    } else {
        // we're before a gliding match
        // or on a colliding match
        if (azTstNew + apCur->izDlt >= 0) {
            azTstOrg = azTstNew + apCur->izDlt;
        } else {
            azTstNew -= azTstOrg ;
            azTstOrg = 0 ;
        }
    }
}

/* -----------------------------------------------------------------------------
 * Get the best match from the array of matches
 * ---------------------------------------------------------------------------*/
bool JMatchTable::getbest (
  off_t const &azRedOrg,    // current read position on original file
  off_t const &azRedNew,    // current read position on new file
  off_t &azBstOrg,          // best position found on original file
  off_t &azBstNew           // best position found on new file
) {
    int liIdx ;             // index in mpMch
    int liDst ;             // distance: number of bytes to compare before failing

    rMch *lpCur ;           /* current match under investigation            */
    rMch *lpPrv ;           /* previous match under investigation           */
    int liCurCnt ;          /* current match confirmation count             */
    int liCurCmp ;          /* current match compare state                  */

    rMch *lpBst=null ;      /* best match.                                  */
    int liBstCnt=0 ;        /* best match count                             */
    int liBstCmp=0 ;        /* best match compare state                     */

    off_t lzTstNew ;        // test/found position in new file
    off_t lzTstOrg ;        // test/found position in old file

    //@int liRlb = mpHsh->get_reliability() ;  // current unreliability range

    #ifdef debug
    int liRlbMax=0;     // measured reliability distance
    int liRlbCur;
    int liRlbCnt=0;
    int liRlbSum=0;
    #endif // debug

    // add function has set a candidate, take this one first
    if (mpMchBst != null) {
        lpBst=mpMchBst ;
        azBstNew = lpBst->izTst;
        azBstOrg = lpBst->izTst + lpBst->izDlt ;
        liBstCnt = lpBst->iiCnt;
        liBstCmp = lpBst->iiCmp;
    } else

    /* loop on the table of matches */
    for (liIdx = 0; liIdx < miMchPme; liIdx ++) {
        for (lpPrv=null, lpCur = mpMch[liIdx]; lpCur != null; ) {
//@            if (lpCur==lpBst){
//                lpPrv=lpCur;
//                lpCur=lpCur->ipNxt;
//                continue ;
//            }
            liCurCnt=-1;

            /* check if the match yields a solution on this position */
            lzTstNew = azRedNew ;                              // start test at current read position

            /* calculate the test position on the original file by applying izDlt */
            calcPosOrg(lpCur, lzTstOrg, lzTstNew);

            /* number of bytes to check before failing */
            if (lpCur->izBeg - lzTstNew + 1 > EQLMAX + SMPSZE)
                liDst = lpCur->izBeg - lzTstNew + 1 ;
            else
                liDst = EQLMAX + SMPSZE ;

            /* if the current best solution is already optimal, then no need to try to be better */
            if (lpBst != null && liBstCmp == EQLMAX && liDst > azBstNew - azRedNew + liBstCmp) {
                liDst = azBstNew - azRedNew + liBstCmp ;
            }

            /* reuse earlier compare result ? */
            if (lzTstNew <= lpCur->izTst) {
                // The new test position is still before the found equal region.
                // So the found equal region is still perfectly valid, no need to check again.
                liCurCmp = lpCur->iiCmp ;
                lzTstOrg = lzTstOrg + (lpCur->izTst - lzTstNew) ;
                lzTstNew = lpCur->izTst ;
            } else if (lzTstNew <= lpCur->izTst + lpCur->iiCmp){
                // The new test position is within the found equal region.
                // If we keep the equal region, it should be shortened, so see is it can be reused
                if (lpCur->iiCmp < EQLMAX) {
                  // The found region < EQLMAX, so in ends after iiCmp.
                  // We only have to shorten iiCmp to correspond with lzTstNew;
                  liCurCmp = lpCur->iiCmp - (lzTstNew - lpCur->izTst) ;
                } else {
                  // We could shorten, but maybe the region extends beyond EQLMAX
                  // If we shorten, we could loose this potentially important region.
                  // So it's better to check agan.
                  // In principle, we could reuse the shortened part of the previous check,
                  // but that's only a small performance optimization, i guess.
                  liCurCmp = 0 ;
                }
            } else {
                // The new test position is beyond the earlioer found equal region, so
                // it can't be reused: check again.
                liCurCmp = 0 ;
            }
            if (liCurCmp == 0) {
                /* compare */
                liCurCmp = check(lzTstOrg, lzTstNew, liDst, mbCmpAll ? JFile::HardAhead : JFile::SoftAhead) ;

                /* store result for later */
                if (liCurCmp>0){
                    lpCur->izTst = lzTstNew ;
                    lpCur->iiCmp = liCurCmp ;
                } else if (lpCur->izNew >= azRedNew) {
                    siHshRpr++ ;
                }
            }

            #ifdef debug
            if (liCurCmp > 0){
                /* measure reliability distance */
                //@liRlbCur = lpCur->izBeg - lzTstNew ;
                liRlbCur = (lpCur->izNew - lpCur->izBeg) / lpCur->iiCnt ;
                if (liRlbCur > 0){
                    liRlbSum+=liRlbCur;
                    liRlbCnt++;
                }
                if (liRlbCur > liRlbMax){
                    liRlbMax = liRlbCur ;
                }
            }
            #endif // debug

            /* Handle EOB */
            if (liCurCmp < 0){
                // EOB was reached, so rely on info from the hashtable: iiCnt, izBeg and izNew
                if (liCurCnt < 0)
                    liCurCnt = (lpCur->iiTyp < 0) ? 0 : lpCur->iiCnt ;

                if (liCurCnt < 2){
                    //  a non-confirmed match is too risky
                } else if (lzTstNew <= lpCur->izBeg) {
                    // We're still before the first detected match,
                    // so a potential solution probably starts at given match
                    lzTstNew = lpCur->izBeg ;
                    liCurCmp = liCurCnt ;
                } else if (lzTstNew <= lpCur->izNew) {
                    // We're in between the first and last detected match,
                    // prorate liCurCmp
                    liCurCmp = liCurCnt * (lzTstNew - lpCur->izBeg)
                                  / (lpCur->izNew - lpCur->izBeg) ;
                }
                if (liCurCmp > 0){
                    // reduce hashtable match, real compares are better
                    liCurCmp /= 2 ;

                    // calculate corresponding lzPosOrg
                    calcPosOrg(lpCur, lzTstOrg, lzTstNew) ;
                } else {
                  liCurCmp = -1;
                }
            }

            /* evaluate: keep the best solution */
            if (liCurCmp > 0){
                if (lpBst == NULL)
                    // first one, take it
                    lpBst=lpCur ;
                else if (lzTstNew + FZY < azBstNew)
                    // new one is clearly better (nearer)
                    lpBst=lpCur ;
                else if (lzTstNew <= azBstNew + FZY) {
                    // maybe better (nearer): check in more detail
                    if (lzTstNew - liCurCmp < azBstNew - liBstCmp) {
                        // new one is longer
                        lpBst=lpCur ;
                    } else if (lzTstNew - liCurCmp == azBstNew - liBstCmp) {
                        // If all else is equal, then rely on the hash counter
                        if (liCurCnt < 0)
                            liCurCnt = (lpCur->iiTyp < 0) ? 0 : lpCur->iiCnt ;
                        if (liCurCnt > liBstCnt)
                            // higher hash-match counter = probably longer
                            lpBst=lpCur ;
                    }
                }

                if (lpBst==lpCur){
                    azBstNew = lzTstNew ;
                    azBstOrg = lzTstOrg ;
                    liBstCnt = liCurCnt ;
                    liBstCmp = liCurCmp ;
                    if (liCurCnt < 0)
                        liBstCnt = (lpCur->iiTyp < 0) ? 0 : lpCur->iiCnt ;
                    else
                        liBstCnt = liCurCnt ;
                }

                /* show table */
                #if debug
                if (JDebug::gbDbg[DBGMCH])
                    fprintf(JDebug::stddbg,
                        "Sol %5d %c [%c" P8zd "<" P8zd ">" P8zd ":" P8zd ":%4d]" P8zd ":%d\n",
                        liCurCmp,
                        (lpBst == lpCur)?'*':' ',
                        (lpCur->iiTyp<0)?'G': (lpCur->iiTyp>0)?'C': ' ',
                        lpCur->izOrg, lpCur->izDlt, lpCur->izNew, lpCur->izBeg, lpCur->iiCnt,
                        lzTstNew, liDst) ;
                #endif

                // normal next
                lpPrv = lpCur ;
                lpCur = lpCur->ipNxt ;

            } else {
                /* show table */
                #if debug
                if (JDebug::gbDbg[DBGMCH])
                    fprintf(JDebug::stddbg,
                        "%s %5d %c [%c" P8zd "<" P8zd ">" P8zd ":" P8zd ":%4d]" P8zd ":%d\n",
                        (lpCur->iiCnt < 2) ? "Cln" : "Mch",
                        liCurCmp, ' ',
                        (lpCur->iiTyp<0)?'G': (lpCur->iiTyp>0)?'C': ' ',
                        lpCur->izOrg, lpCur->izDlt, lpCur->izNew, lpCur->izBeg, lpCur->iiCnt,
                        lzTstNew, liDst) ;
                #endif

                /* useless ? : clean it up to make room for other (better?) matches */
                if ((lpCur->iiCnt < 2) && lpCur->izNew < azRedNew) {
                    // remove from list
                    if (lpPrv == null)
                        mpMch[liIdx] = lpCur->ipNxt ;
                    else
                        lpPrv->ipNxt = lpCur->ipNxt ;

                    // add to free-list
                    lpCur->ipNxt = mpMchFre ;
                    mpMchFre = lpCur ;

                    // next after remove
                    if (lpPrv == null){
                        lpCur = mpMch[liIdx] ;
                    } else {
                        lpCur = lpPrv->ipNxt ;
                    }
                } else {
                    // normal next
                    lpPrv = lpCur ;
                    lpCur = lpCur->ipNxt ;
                }
            } /* if else liCurCmp > 0 */
        } /* for lpCur */
    } /* for liIdx */

    #if debug
    if (JDebug::gbDbg[DBGMCH]){
        if (lpBst == null){
            fprintf(JDebug::stddbg, "Match Failure at %" PRIzd "\n", azRedNew) ;
        } else if ((azRedNew != azBstNew)){
            fprintf(JDebug::stddbg, "Suboptimal Match at %" PRIzd ": %" PRIzd "= %d\n",
                    azRedNew, azBstNew, liBstCmp);
        } else if ((liBstCmp < EQLSZE)) {
            fprintf(JDebug::stddbg, "Short Match at %" PRIzd ": %" PRIzd "= %d\n",
                    azRedNew, azBstNew, liBstCmp);
        }

        fprintf(JDebug::stddbg, "Rlb official=%d, measured max=%d, avg=%d\n",
            mpHsh->get_reliability(), liRlbMax, liRlbCnt>0?liRlbSum/liRlbCnt:-1);
    }
    #endif

    // Create a synthetic jump ahead if no match has been found
    if (lpBst == null) {
        azBstNew = azRedNew ;
        azBstOrg = azRedOrg ;
    }

    return (mpMchFre != null || miMchFre > 0) ; // still place or not ?
} /* getbest() */


/* -----------------------------------------------------------------------------
 * check(): verify and optimize matches
 *
 * Searches at given positions for a run of 24 equal bytes.
 * Searching continues for the given length unless soft-reading is specified
 * and the end-of-buffer is reached.
 *
 * Arguments:     &rzPosOrg    in/out  position on first file
 *                &rzPosNew    in/out  position on second file
 *                 aiLen       in      number of bytes to compare
 *                 aiSft       in      1=hard read, 2=soft read
 *
 * Return value:   0 = no run of equal byes found
 *               > 0 = number of continuous equal bytes found
 *               < EQLSZE: aiLen or EOB or EOF reached
 *               = EQLSZE: EQLSZE found
 * ---------------------------------------------------------------------------*/
/**
 * Verify and optimize matches:
 * Searches at given positions for a run of minimum EQLMIN equal bytes.
 * Searching continues for the full length unless soft-reading is specified
 * and the end-of-buffer is reached.
 *
 * @param   &rzPosOrg    in/out  position on first file
 * @param   &rzPosNew    in/out  position on second file
 * @param   aiLen       in      number of bytes to compare
 * @param   aiSft       in      1=hard read, 2=soft read
 *
 * @return  0   : no run of equal byes found
 * @return  > 0 : number of continuous equal bytes found
 * @return  < 0 : EOB reached at - aiLen
 * ---------------------------------------------------------------------------*/
int JMatchTable::check (
    off_t &azPosOrg, off_t &azPosNew,
    int aiLen, const JFile::eAhead aiSft
) const {
    int lcOrg=0 ;
    int lcNew=0 ;
    int liEql=0 ;
    //int liMaxEql=0;
    //int liMaxPos=0;
    //int liLen ;

    #if debug
    if (JDebug::gbDbg[DBGCMP])
        fprintf( JDebug::stddbg, "Fnd (" P8zd "," P8zd ",%4d,%d): ",
                 azPosOrg, azPosNew, aiLen, aiSft) ;
    #endif

    /* Compare bytes */
    for ( ; liEql < EQLMAX; aiLen--)
    {
        if ((lcOrg = mpFilOrg->get(azPosOrg ++, aiSft)) < 0){
            azPosOrg--;
            break;
        }
        else if ((lcNew = mpFilNew->get(azPosNew ++, JFile::SoftAhead)) < 0){
            azPosNew--;
            azPosOrg--;
            break;
        }
        else if (lcOrg == lcNew)
            liEql ++ ;
        else if (liEql > EQLMIN) {
            azPosOrg -- ;
            azPosNew -- ;
            break ;
        } else {
            liEql = 0;
            if (aiLen <= 0)
                break ;
        }
    }

    #if debug
        if (JDebug::gbDbg[DBGCMP])
            fprintf( JDebug::stddbg, "" P8zd " " P8zd " %2d %s (%c)%02x == (%c)%02x\n",
                     azPosOrg - liEql, azPosNew - liEql, liEql,
                     (liEql>EQLMIN)?"OK!":(lcOrg==EOB || lcNew==EOB)?"EOB":"NOK",
                     (lcOrg>=32 && lcOrg <= 127)?lcOrg:' ',(uchar)lcOrg,
                     (lcNew>=32 && lcNew <= 127)?lcNew:' ',(uchar)lcNew);
    #endif

    if (liEql > EQLMIN){
        azPosOrg -= liEql ;
        azPosNew -= liEql ;
        #if EQLBST > 0
        if (liEql >= EQLMAX)
            return  EQLSZE + EQLBST ;
        else if (lcOrg == EOB || lcNew == EOB)
            return  EQLSZE + EQLBST / 2 ;
        else
        #endif // EQLBST
            return  liEql ;
    } else if (lcOrg == EOB || lcNew == EOB){
        return -aiLen  ; // EOB reached
    } else {
        // No equal bytes found
        return 0 ;
    }
} /* check() */

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

