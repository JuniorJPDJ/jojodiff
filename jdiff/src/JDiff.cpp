/*******************************************************************************
 * JDiff.cpp
 *
 * Jojo's diff on binary files: main class.
 *
 * Copyright (C) 2002-2020 Joris Heirbaut
 *
 * This file is part of JojoDiff.
 *
 * This program is free software: you can redistribute it and/or modify
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
 * ----------------------------------------------------------------------------
 *
 * Class JDiff takes two JFiles as input and outputs to a JOut instance.
 *
 * If you want to reuse JojoDiff, you need following files:
 * - JDiff.h/cpp        The main JojoDiff class
 * - JHashPos.h/cpp     The hash table collection of (sample-key, position)
 * - JMatchTable.h/cpp  The matching table logic
 * - JDefs.h            Global definitions
 * - JDebug.h/cpp       Debugging definitions
 * - JOut.h             Abstract output class
 * - JFile.h            Abstract input class
 * + at least one descendant of JOut and JFile (may be of your own).
 *
 * Method jdiff performs the actual diffing:
 * 1) first, we create a hashtable collection of (hkey, position) pairs, where
 *      hkey = hash key of a 32 byte sample in the left input file
 *      position = position of this sample in the file
 * 2) compares both files byte by byte
 * 3) when a difference is found, looks ahead using ufFndAhd to find the nearest
 *    equal region between both files
 * 4) output skip/delete/backtrace instructions to reach the found regions
 * 5) repeat steps 2-4 each time the end of an equal region is reached.
 *
 * Method ufFndAhd looks ahead on the input files to find the nearest equals regions:
 * - for every 32-byte sample in the right file, look in the hastable whether a
 *   similar sample exists in the left file.
 * - creates a table of matching regions using this catalog
 * - returns the nearest match from the table of matches
 *
 * There are two reasons for creating a matching table on top of the hashtable:
 * - The hashtable may only contain a (small) percentage of all samples, hence
 *   the nearest equal region is not always detected first.
 * - Samples are considered equal when their hash-keys are equal (in fact there's only
 *   1/256 chance that samples are really equal of their hash-keys match).
 *
 * Method ufFndAhdGet gets one byte of a file and counts the number of subsequent
 * equal bytes (because samples containing too many equal bytes are usually bad to
 * compare with between files).
 *
 * Method ufFndhdScn scans the left file and creates the hash table.
 *
 *******************************************************************************/
#include "JDefs.h"
#include "JDiff.h"
#include <limits.h>

#ifdef _FILE_OFFSET_BITS
#pragma message "INFO: FILE OFFSET BITS = " XSTR(_FILE_OFFSET_BITS)
#else
#pragma message "INFO: FILE OFFSET BITS NOT SET !"
#endif
#ifdef _LARGEFILE64_SOURCE
#pragma message "INFO: _LARGEFILE64_SOURCE set"
#endif
#ifdef __MINGW32__
#pragma message "INFO: __MINGW32__ set"
#endif
#ifdef __MINGW64__
#pragma message "INFO: __MINGW64__ set"
#endif
#if debug
#pragma message "INFO: debug activated"
#else
#pragma message "INFO: debug not activated"
#endif
#pragma message "INFO: PRIzd = " XSTR(PRIzd)

#define PGSMRK 0x100000    /**< Progress mark: show progress in Mb (1024 * 1024 or 0x400 x 0x400)  */
#define PGSMSK 0x1ffffff   /**< Progress mask: show progress every 32Mb when (lzPos & PGSMSG == 0) */

namespace JojoDiff {

/*
 * Constructor
 */
JDiff::JDiff(
    JFile * const apFilOrg,     /* Original file */
    JFile * const apFilNew,     /* New file */
    JOut  * const apOut,        /* Output patch file */
    const int aiHshSze,         /* Hashtable size in MB */
    const int aiVerbse,         /* Verbosity level: between 0 and 3 */
    const int abSrcBkt,         /* Allow backtracking on original file (default: yes) */
    const int aiSrcScn,         /* Scan source-file: 0=no, 1=yes, 2=done */
    const int aiMchMax,         /* Maximum matches to search for */
    const int aiMchMin,         /* Minimum matches to search for */
    const int aiAhdMax,         /* Lookahead maximum (in bytes) */
    const bool abCmpAll         /* Compare all matches ? */
) : mpFilOrg(apFilOrg), mpFilNew(apFilNew), mpOut(apOut),
    miVerbse(aiVerbse), mbSrcBkt(abSrcBkt),
    miMchMax(aiMchMax),
    miMchMin(aiMchMin > miMchMax ? miMchMax - 1 : aiMchMin),
    miAhdMax(aiAhdMax<1024?1024:aiAhdMax),
    mbCmpAll(abCmpAll), miSrcScn(aiSrcScn),
    mzAhdOrg(0), mzAhdNew(0), mlHshOrg(0), mlHshNew(0), giHshErr(0)
{
	gpHsh = new JHashPos(aiHshSze) ;
	gpMch = new JMatchTable(gpHsh, mpFilOrg, mpFilNew, aiMchMax, abCmpAll);
	miValOrg = 0 ; //@ to remove !
}

/*
 * Destructor
 */
JDiff::~JDiff() {
	delete gpHsh ;
	delete gpMch ;
}

/**
* @brief Difference function
*
* Takes two files as arguments and writes out the differences
*
* Principle:
*   Take one byte from each file and compare. If they are equal, then continue.
*   If they are different, start lookahead to find next equal blocks within file.
*   If equal blocks are found,
*   - first insert or delete the specified number of bytes,
*   - then continue reading on both files until equal blocks are reached,
*
* @return 0    all ok
* @return < 0  error: see EXIT-codes
*/
int JDiff::jdiff()
{
    int lcOrg;              /**< byte from original file */
    int lcNew;              /**< byte from new file */
    off_t lzPosOrg = 0 ;    /**< position in original file */
    off_t lzPosNew = 0 ;    /**< position in new file */

    bool  lbEql = false;    /**< accumulate equal bytes? */
    off_t lzEql = 0;        /**< accumulated equal bytes */

    int liFnd = 0;          /**< offsets are pointing to a valid solution (= equal regions) ?   */
    off_t lzAhd=0;          /**< number of bytes to advance on both files to reach the solution */
    off_t lzSkpOrg=0;       /**< number of bytes to skip on original file to reach the solution */
    off_t lzSkpNew=0;       /**< number of bytes to skip on new      file to reach the solution */
    off_t lzLap=0;          /**< lap for reducing number of progress messages for -vv           */

    #if debug
    int liErr=0 ;		    /**< check for misses : 0=not checking, 1=checking, 2=miss detected */
    #endif

    if (miVerbse > 0) {
      fprintf(JDebug::stddbg, "Comparing : ...           ");
    }

    /* Take one byte from each file ... */
    lcOrg = mpFilOrg->get(lzPosOrg, 0);
    lcNew = mpFilNew->get(lzPosNew, 0);
    while (lcNew >= 0) {
        #if debug
        if (JDebug::gbDbg[DBGPRG])
            fprintf(JDebug::stddbg, "Input "P8zd"->%2x "P8zd"->%2x.\n",
                    lzPosOrg - 1, lcOrg, lzPosNew - 1, lcNew)  ;
        #endif

        if (lcOrg == lcNew){
            /* Output or count equals */
            if (lbEql){
                lzEql ++ ;
            } else {
                // the first four bytes may be kept in reserve, then switch to counting asap
                lbEql = mpOut->put(EQL, 1, lcOrg, lcNew, lzPosOrg, lzPosNew);
            }

            /* Take next byte from each file ... */
            lcOrg = mpFilOrg->get(++ lzPosOrg, 0) ;
            lcNew = mpFilNew->get(++ lzPosNew, 0) ;

            /* decrease ahead counter */
            lzAhd -- ;

            /* show progress */
            if ((miVerbse > 1) && (lzLap <= lzPosNew)){
              fprintf(JDebug::stddbg, "\rComparing : %12" PRIzd "Mb", lzPosNew / PGSMRK);
              lzLap=lzPosNew+PGSMRK;
            }
        } else if (lzAhd > 0) {
            /* Output accumulated equals */
            ufPutEql(lzPosOrg, lzPosNew, lzEql, lbEql);

            /* Output difference */
            if (lcOrg < 0) {
                mpOut->put(INS, 1, lcOrg, lcNew, lzPosOrg, lzPosNew);

                /* Take next byte from each file ... */
                lcNew = mpFilNew->get(++ lzPosNew, 0) ;
            } else {
                mpOut->put(MOD, 1, lcOrg, lcNew, lzPosOrg, lzPosNew);

                /* Take next byte from each file ... */
                lcOrg = mpFilOrg->get(++ lzPosOrg, 0) ;
                lcNew = mpFilNew->get(++ lzPosNew, 0) ;
            }

            /* decrease ahead counter */
            lzAhd-- ;

        } else if ((liFnd == 1) && (lzAhd == 0)) {
            // Oops: the "found" solution did not point to an equal region
            // This may happen, especially for non-compared solutions,
            // but we should hope this does not happen too much
            liFnd = 0 ;

            // Report the miss to our user
            giHshErr++ ;
            if (miVerbse>2){
              fprintf(JDebug::stddbg, "Search failure at positions %" PRIzd "/%" PRIzd "!\n", lzPosOrg, lzPosNew);
            }

            lzAhd=SMPSZE / 2; /* Advance at least half-a samplesize bock */

        } else {
            // Look for a new solution
            #if debug
            /* Flush output buffer in debug */
            if (JDebug::gbDbg[DBGAHD] || JDebug::gbDbg[DBGMCH]){
              ufPutEql(lzPosOrg, lzPosNew, lzEql, lbEql);
              mpOut->put(ESC, 0, 0, 0, lzPosOrg, lzPosNew);
            }
            #endif

            /* Find a new equals-reqion */
            liFnd = ufFndAhd(lzPosOrg, lzPosNew, lzSkpOrg, lzSkpNew, lzAhd) ;
            if (liFnd < 0)
                return liFnd;
            #if debug
              if (JDebug::gbDbg[DBGAHD])
                fprintf(JDebug::stddbg, "Findahead on %" PRIzd " %" PRIzd " skip %" PRIzd " %" PRIzd " ahead %" PRIzd "\n",
                        lzPosOrg, lzPosNew, lzSkpOrg, lzSkpNew, lzAhd)  ;
              if (JDebug::gbDbg[DBGPRG])
                fprintf(JDebug::stddbg, "Current position in new file= %" PRIzd "\n", lzPosNew) ;
            #endif

            /* show progress */
            if ((miVerbse > 1) && (lzLap <= lzPosNew)) {
              fprintf(JDebug::stddbg, "\rComparing : %12" PRIzd "Mb", lzPosNew / PGSMRK);
              lzLap=lzPosNew+PGSMRK;
            }

            /* Output accumulated equals */
            ufPutEql(lzPosOrg, lzPosNew, lzEql, lbEql);

            /* Execute offsets */
            if (lzSkpOrg > 0) {
              mpOut->put(DEL, lzSkpOrg, 0, 0, lzPosOrg, lzPosNew) ;
              lzPosOrg += lzSkpOrg ;
              lcOrg = mpFilOrg->get(lzPosOrg, 0);
            } else if (lzSkpOrg < 0) {
              mpOut->put(BKT, - lzSkpOrg, 0, 0, lzPosOrg, lzPosNew) ;
              lzPosOrg += lzSkpOrg ;
              lcOrg = mpFilOrg->get(lzPosOrg, 0);
            }
            if (lzSkpNew > 0) {
              while (lzSkpNew > 0 && lcNew > EOF) {
                mpOut->put(INS, 1, 0, lcNew, lzPosOrg, lzPosNew);
                lzSkpNew-- ;
                lcNew = mpFilNew->get(++ lzPosNew, 0);
              }
            }
        } /* if lcOrg == lcNew */
    } /* while lcNew >= 0 */

    /* Flush output buffer */
    ufPutEql(lzPosOrg, lzPosNew, lzEql, lbEql);
    mpOut->put(ESC, 0, 0, 0, lzPosOrg, lzPosNew);

    /* Show progress */
    if (miVerbse > 0) {
      fprintf(JDebug::stddbg, "\rComparing : %12" PRIzd "Mb", (lzPosNew + PGSMRK / 2) / PGSMRK);
    }

    /* Show final hashtable distribution in case of incremental source scanning (miSrcScn==0) */
    if (miVerbse>2 && miSrcScn == 0){
          gpHsh->dist(lzPosOrg, 10);
    }

    /* Return code */
    if (lcNew < EOB || lcOrg < EOB){
        return (lcNew < lcOrg)?lcNew:lcOrg;
    }
    return 0;
} /* jdiff */

/**
 * @brief Flush pending EQL's
 */
void JDiff::ufPutEql(const off_t &lzPosOrg, const off_t &lzPosNew, off_t &lzEql, bool &lbEql) const {
    /* Output accumulated equals */
    if (lzEql > 0){
        mpOut->put(EQL, lzEql, 0, 0, lzPosOrg - lzEql, lzPosNew - lzEql);
        lzEql = 0;
    }
    lbEql=false;
} /* ufPutEql */

/**
 * @brief Find Ahead function
 *        Read ahead on both files until an equal series of 32 bytes is found.
 *        Then calculate the deplacement vector between two files:
 *          - positive if characters need to be inserted in the original file,
 *          - negative if characters need to be removed from the original file.
 * @param azRedOrg  read position in left file
 * @param azRedNew  read position in right file
 * @param azSkpOrg  out: number of bytes to skip (delete) in left file
 * @param azSkpNew  out: number of bytes to skip (insert) in right file
 * @param azAhd     out: number of bytes to skip on bith files before similarity is reached
 * @return 0    no solution found
 * @return 1    solution found
 * @return < 0  error: see EXIT-codes
 */
int JDiff::ufFndAhd (
  off_t const &azRedOrg,
  off_t const &azRedNew,
  off_t &azSkpOrg,
  off_t &azSkpNew,
  off_t &azAhd
) {
    off_t lzFndOrg=0;   /**< Found position within original file                 */
    off_t lzFndNew=0;   /**< Found position within new file                      */
    off_t lzBseOrg;     /**< Base position on original file                      */
    off_t lzLap=0;      /**< Stop-lap for progress counter                       */

    int liMax;          /**< Max number of bytes to look ahead              */
    int liBck;          /**< Number of bytes to look back                   */
    int liIdx;          /**< Index for initializing                         */
    int liRlb;          /**< Reliability range for current hashtable        */

    int liFnd;          /**< Number of matches found                        */
    int liSftOrg;       /**< 1 = hard look-ahead, 2 = soft look-ahead       */
    int liSftNew;       /**< 1 = hard look-ahead, 2 = soft look-ahead       */

    static bool lbFre=true;     /**< false = match table is full, cleanup needed   */

    /* Set Lap for progress counter */
    if (miVerbse > 1) lzLap = azRedNew + PGSMRK ;

    /* Switch to soft reading when the minimum number of matches is obtained */
    liSftOrg = ((miMchMin == 0) ? 2 : 1) ;
    liSftNew = ((miMchMin == 0) ? 2 : 1) ;

    /* Prescan the source file to build the hashtable */
    switch (miSrcScn) {
    case 1: {
            // do a full prescan
            int liRet = ufFndAhdScn() ;
            if (liRet < 0)
                return liRet ;
            miSrcScn = 2 ;
        }
        break ;

    case 0:
        // incremental sourcefile scanning, soft-reading part
        // check if mzAhdOrg is (again) in the buffer
        if (mzAhdOrg > 0 && miValOrg == EOB){
            // reread the EOB-position to check if EOB is gone
            miValOrg = mpFilOrg->get(mzAhdOrg, liSftOrg) ;

            // check if the source position has jumped forward
            if (miValOrg == EOB && azRedOrg > mzAhdOrg){
                mzAhdOrg=0 ; // re-initialize and restart from zero
            }
        }

        // (re-)initialize the hash function
        if (mzAhdOrg==0){
            // scan the part of the source file available within the buffer
            mzAhdOrg = mpFilOrg->getBufPos() - 1; // -1 for ++mzAhdOrg
            if (mzAhdOrg < -1 )
                mzAhdOrg = -1 ; // for non-buffering sources

            // initialize the hash function
            mlHshOrg = 0 ;
            miEqlOrg = 0 ;
            miPrvOrg = EOF ;
            if (mzAhdOrg == -1 )
                liMax = SMPSZE - 1;         // to initialize mkHsh (miEql=0 is correct)
            else
                liMax = SMPSZE * 2 - 1;     // to initialize miEql and mkHsh
            for (liIdx = 0; liIdx < liMax; liIdx++) {
                miValOrg = mpFilOrg->get(++ mzAhdOrg, liSftOrg) ;
                if (miValOrg <= EOF)
                    break ;
                mlHshOrg = gpHsh->hash(mlHshOrg, miPrvOrg, miValOrg, miEqlOrg) ;

                // The following line needs some explication.
                // We want to terminate the initialization ASAP, every byte counts in -ff mode
                // To simplify, consider SMPSZE == 8, so we need 7 valid miEql's to initialize
                // Take for example an initialization starting at position 4 (hex data)
                //    mzAhd :   4 5 6 7 8 9 A B C D E F ...
                //    miVal :   0 0 0 0 7 6 5 4 3 2 1 0 4 9 7 4  ...
                //    miPrv : EOF 0 0 0 0 7 6 5 4 3 2 1 0 4 9 7 4 ...
                //    miEql :   0 1 2 3 0 0 0 0 0 0 0 ...
                //    liIdx :   0 1 2 3 4 5 6 7 8 9 A B C ...
                //    init            +-----------+
                // We don't know if position 3 is 0 or not, so we don't know what value miEql
                // at position 4 should have. So the first four bytes cannot be used,
                // because miEql may not be correct.
                // As soon as miEql is reset to 0 by miPrv != miVal, miEql becomes correct
                // and initialization will be correct after 7 more bytes (position D in the example)
                // Reset can be detected by miEql != liIdx. Hence, when miEql != liIdx,
                // we can reduce liMax to liIdx + 7 (or better SMPSZE - 1) and add more bytes
                // to the index hashtable.
                if (miEqlOrg != liIdx && liMax > liIdx + (SMPSZE - 1))
                    liMax = liIdx + (SMPSZE - 1) ;
            }
        }

        // scan the sourcefile till the buffer is exhausted
        for (liMax=miAhdMax; liMax > 0 ; liMax --) {
            miValOrg = mpFilOrg->get(++ mzAhdOrg, liSftOrg) ;
            if (miValOrg <= EOF)
                break ;
            mlHshOrg = gpHsh->hash(mlHshOrg, miPrvOrg, miValOrg, miEqlOrg) ;
            gpHsh->add(mlHshOrg, mzAhdOrg, miEqlOrg) ;
        }
        break ;
    } /* scan source file - build hashtable */

    /*
    * How many bytes to look ahead (search) ?
    * As far as possible, but going too far makes no sense.
    * In theory, if we can't find a solution within the (un)reliability range,
    * then there is no solution here !
    * In practice: it does no harm (no speed penalty) to use the buffer
    * and already jump to a far-away solution.
    * Moreover, the unreliability range is only an estimate of the average number
    * of bytes needed to find a solution (due to the index hashtable only covering
    * a small percentage of samples). So using the whole buffer may solve a situation
    * where a solution needs more bytes to be found than indicated by the reliability
    * range. Once a minimum number of potential solutions is found, the lookahead
    * may again be reduced to the reliability range (se below).
    */
    liRlb = gpHsh->get_reliability() ;
    if (liRlb < miAhdMax - 2 * SMPSZE){
        // search for the specified lookahead / buffersize
        liMax = miAhdMax - 2 * SMPSZE ;
    } else {
        // search at least the reliability distance
        liMax = liRlb  ;
    }

    /*
    * How many bytes to look back ?
    * In theory: none, it makes no sense to look back
    * In practice:
    * - looking back avoids the need to reinitialize the hash function
    * - re-initialization of the hash function can take up to SMPSZE * 2 bytes
    * - looking back allows to keep the existing match-table up-to-date
    * Therefore, we allow for some look back.
    */
    liBck = (azRedNew - mzAhdNew);
    if (liBck < 0)
        // mzAhdNew is stil ahead of azRedNew from a previous lookahead
        // continue where the previous left off
        liBck= 0 ;
    else if (liBck > liRlb + 2 * SMPSZE - 1)
        // Go back for the reliability range + 2 * SMPSZE to anticipate a reinitialization
        liBck=liRlb + 2 * SMPSZE - 1;


    /* Cleanup the old matches */
    liFnd = gpMch->cleanup(azRedNew, liBck + liRlb);
    if (liFnd < 0){
        // a best match is already available !
        if (liMax > liRlb)
            liMax=liRlb ;   // reduce search (but not to zero)
        liFnd=-liFnd;
    }
    lbFre=(miMchMax-liFnd > 0);

    /* If there's room to work */
    if (lbFre > 0) {
        // Set lookahead base position
        mpFilNew->set_lookahead_base(azRedNew);

        // Switch to soft reading if the minimum number of matches is obtained
        if (liFnd >= miMchMin){
            liSftOrg = 1 ; //@ 083s
            liSftNew = 2 ; //@ 083s
        }

        /*
        * Re-Initialize hash function (read 31 or 63 bytes) if
        * - ahead position has been reset, or
        * - read position has jumped over the ahead position
        */
        // recover from an EOB
        if (miValNew == EOB){
            // reread the EOB, unless a reset will be done anyway
            if (mzAhdNew + liBck < azRedNew)
                ; // reset lookahead will be done anyway, don't try to re-read
            else {
                // re-read, maybe data is now available
                miValNew = mpFilNew->get(mzAhdNew, liSftNew) ;

                // force a reset if re-read failed
                if (miValNew == EOB)
                    mzAhdNew = 0;
            }
        }
        if (mzAhdNew == 0 || mzAhdNew + liBck < azRedNew)  {
            // Don't go back more than the buffer allows (to avoid EOB)
            mzAhdNew = mpFilNew->getBufPos() ;

            // Set looking back position, but never before the buffer
            if (azRedNew > mzAhdNew + liBck){
                mzAhdNew = azRedNew - liBck;
                if (mzAhdNew < 0)
                    mzAhdNew = 0;
            }

            // Initialize hash: at the start of the file (mzAhdNew == 0),
            // SMPSZE suffices to initialize, but within the file (mzAhdNew > 0),
            // in a worst case, we first need SMPSZE to initialize miEqlNew and
            // then another SMPSZE to initialize the hash
            if (mzAhdNew == 0)
                liBck = SMPSZE - 1 ;        // to initialize mkHsh (miEql=0 is correct)
            else
                liBck = SMPSZE * 2 - 1 ;    // to initialize mkHsh and miEql
            mzAhdNew -- ; // switch to pre-increments
            mlHshNew = 0;
            miEqlNew = 0;
            miPrvNew = EOF;
            for (liIdx = 0 ; liIdx < liBck; liIdx++) {
                miValNew = mpFilNew->get(++ mzAhdNew, liSftNew) ;
                if (miValNew <= EOF)
                    break ;
                mlHshNew = gpHsh->hash(mlHshNew, miPrvNew, miValNew, miEqlNew) ;

                // See explication above at initialization of mlHshOrg
                if (liIdx != miEqlNew && liBck > liIdx + (SMPSZE - 1))
                    liBck = liIdx + (SMPSZE - 1) ;
            }
        }

        /* Add the resulting lookback to liMax */
        if (mzAhdNew < azRedNew)
            liMax += (azRedNew - mzAhdNew) ;

        /* Do not backtrace before lzBseOrg */
        lzBseOrg = (mbSrcBkt?0:mpFilOrg->getBufPos()) ;

        /*
        * Build the table of matches
        */
        while ((liMax > 0)) {
            /* hash the new value */
            miValNew = mpFilNew->get(++ mzAhdNew, liSftNew) ;
            if (miValNew <= EOF)
                break ;
            mlHshNew = gpHsh->hash(mlHshNew, miPrvNew, miValNew, miEqlNew) ;
            liMax --;

            /* lookup the new value in the hashtable and add it to the table of matches...*/
            if (gpHsh->get(mlHshNew, lzFndOrg)) {
                /* ...unless it's not usable because we've been instructed not to backtrack on source file */
                if (lzFndOrg > lzBseOrg) {
                    /* it's usable: add to the table of matches */
                    liIdx=gpMch->add(lzFndOrg, mzAhdNew, azRedNew);
                    if (liIdx < 0){
                        // panic: table is full unexpectedly, try to cleanup and add again
                        // normally we should never pass here, kept for just-in-case
                        liFnd = gpMch->cleanup(azRedNew, 0);
                        if (liFnd < 0){
                            liFnd=-liFnd;
                        }
                        lbFre = (miMchMax-liFnd > 0);
                        liIdx = gpMch->add(lzFndOrg, mzAhdNew, azRedNew);
                        if (liIdx < 0) {
                            // really full: this should not happen, write out an error
                            if (miVerbse > 0){
                                fprintf(JDebug::stddbg, "Match table overflow @ %" PRIzd "\n",
                                  mzAhdNew) ;
                            }
                            break;
                        }
                        if (mzAhdNew > azRedNew) liFnd --;  // will be reincremented below
                    } else if (liIdx == 0){
                        // solution added, but table is now full
                        // try to cleanup
                        liFnd = gpMch->cleanup(azRedNew,0);
                        if (liFnd < 0){
                            liFnd=-liFnd;
                        }
                        lbFre=(miMchMax-liFnd > 0);
                        if (! lbFre)
                            break ;
                        if (mzAhdNew > azRedNew) liFnd --;  // will be reincremented below
                    } else if (liIdx == 4) {
                        // This seems to be a very good solution.
                        // However, due to the unreliable nature of the checksums and the hash-table,
                        // the first good solution is not always the best one,
                        // but a better one should be found within the reliability range.
                        //
                        // Why ? Because the reliability range estimates the number of bytes
                        // to search before finding all solutions hidden behind the unreliability.
                        // So after the (estimated) reliability range, no better solution should be found anymore.
                        // reduce the lookahead to be sure and to improve performance
                        if (liMax > liRlb)
                            liMax = liRlb ;
                    } else if (liIdx == 5) {
                        // Perfect solution found : Stop right now !
                        break ;
                    }

                    // solution added
                    if (liIdx == 0 or liIdx == 1 or liIdx == 4){
                        if (mzAhdNew > azRedNew) {    // do not count lookback matches
                            liFnd ++ ;

                            if (liFnd >= miMchMin) liSftNew=2 ;   // switch to soft reading
                            if (liFnd >= miMchMax) break;         // stop lookahead
                        }
                    } /* solution added */
                } /* if usable */
            } /* lookup */

            /* show progress */
            if ((miVerbse > 1) && (lzLap <= mzAhdNew)) {
              fprintf(JDebug::stddbg, "+%-12" PRIzd "\b\b\b\b\b\b\b\b\b\b\b\b\b", (mzAhdNew - azRedNew) / PGSMRK);
              lzLap=lzLap+PGSMRK;
            }
        } /* while ! EOF */
    } /* if liFnd <= miMchMax */

    /* Check for errors  */
    if (miValNew < EOB || miValOrg < EOB) {
        return (miValNew < miValOrg) ? miValNew : miValOrg;
    }

    /* show progress */
    if ((miVerbse>1) && (lzLap > azRedNew+PGSMRK))  {
        fprintf(JDebug::stddbg, "+%-12" PRIzd "...\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b", (mzAhdNew - azRedNew) / PGSMRK);
    }

    /*
     * Get the best match and calculate the offsets
     */
    lbFre = gpMch->getbest(azRedOrg, azRedNew, /* out */ lzFndOrg, lzFndNew);

    /* clear search progress */
    if ((miVerbse>1) && (lzLap > azRedNew+PGSMRK)) {
        fprintf(JDebug::stddbg, "                \b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b") ;
    }

    /* Calculate the resulting offsets */
    if (azRedOrg == lzFndOrg && azRedNew == lzFndNew)  {
        // No solution has been found. Maybe the search window size is too small, or
        // the hashtable, or the buffers, or maybe the files are simply different.
        // Anyway, iterating over the same search windows makes no sense, except
        // for the part covered by the (un)reliability range.
        azSkpOrg = 0 ;
        azSkpNew = 0 ;
        azAhd    = (mzAhdNew - azRedNew) ; //@ - gpHsh->get_reliability() ; //@ - SMPSZE * 2 ;
        if (azAhd < SMPSZE)
            azAhd = SMPSZE ;
        return 0 ;
    }  else  {
        if (lzFndOrg >= azRedOrg) {
            if (lzFndOrg - azRedOrg >= lzFndNew - azRedNew) {
                /* go forward on original file */
                azSkpOrg = lzFndOrg - azRedOrg + azRedNew - lzFndNew ;
                azSkpNew = 0 ;
                azAhd    = lzFndNew - azRedNew ;
            } else {
                /* go forward on new file */
                azSkpOrg = 0;
                azSkpNew = lzFndNew - azRedNew + azRedOrg - lzFndOrg ;
                azAhd    = lzFndOrg - azRedOrg ;
            }
        } else {
            /* backtrack on original file */
            azSkpOrg = azRedOrg - lzFndOrg + lzFndNew - azRedNew ;
            if (azSkpOrg < azRedOrg) {
                azSkpNew = 0 ;
                azSkpOrg = - azSkpOrg ;
                azAhd = lzFndNew - azRedNew ;
            } else { /* do not bactrace before beginning of file */
                azSkpNew = azSkpOrg - azRedOrg ;
                azSkpOrg = - azRedOrg ;
                azAhd = (lzFndNew - azRedNew) - azSkpNew ;
            }
        }

        return 1 ;
    }
} /* ufFndAhd */

/**
 * @brief   Prescan the original file.
 *
 *          Calculates a hash-key for every 32-bytes sample
 *          in the source file and stores them with their position in a hash-table.
 */
int JDiff::ufFndAhdScn ()
{
    hkey  lkHshOrg=0;     // Current hash value for original file
    int   liEqlOrg=0;     // Number of times current value occurs in hash value
    int   lcValOrg=0;     // Current  file value
    int   lcValPrv=EOF;   // Previous file value
    off_t lzPosOrg=-1;    // Position within original file

    int liIdx ;

    if (miVerbse > 0) {
        fprintf(JDebug::stddbg, "\nIndexing  : ...           ");
    }

    /* Read SMPSZE-1 bytes (31 or 63) to initialize the hash function */
    for (liIdx=0; (liIdx < SMPSZE - 1); liIdx++) {
        lcValOrg = mpFilOrg->get(++ lzPosOrg, 1);
        if (lcValOrg <= EOF)
            break ;
        lkHshOrg = gpHsh->hash(lkHshOrg, lcValPrv, lcValOrg, liEqlOrg) ;
    }

    /* Build hashtable */
    if (miVerbse > 1) {
        /* slow version with user feedback */
        while (lcValOrg > EOF) {
            lcValOrg = mpFilOrg->get(++ lzPosOrg, 1);
            if (lcValOrg <= EOF)
                break ;
            lkHshOrg = gpHsh->hash(lkHshOrg, lcValPrv, lcValOrg, liEqlOrg) ;
            gpHsh->add(lkHshOrg, lzPosOrg, liEqlOrg) ;

            #if debug
                if (JDebug::gbDbg[DBGAHH])
                    fprintf(JDebug::stddbg, "ufHshAdd(%2x -> %8"PRIhkey", "P8zd", %8d)\n",
                            lcValOrg, lkHshOrg, lzPosOrg, 0);
            #endif

            /* output position every 16MB */
            if ((lzPosOrg & PGSMSK) == 0) {
              fprintf(JDebug::stddbg, "\b\b\b\b\b\b\b\b\b\b\b\b\b\b%12" PRIzd "Mb", lzPosOrg / PGSMRK);
            }
        }
    } else {
        /* fast version, no user feedback nor debug */
        while (lcValOrg > EOF) {
            lcValOrg = mpFilOrg->get(++ lzPosOrg, 1);
            if (lcValOrg <= EOF)
                break ;
            lkHshOrg = gpHsh->hash(lkHshOrg, lcValPrv, lcValOrg, liEqlOrg) ;
            gpHsh->add(lkHshOrg, lzPosOrg, liEqlOrg) ;
        }
    }

    if (miVerbse > 0) {
        /* output final position */
        fprintf(JDebug::stddbg, "\b\b\b\b\b\b\b\b\b\b\b\b\b\b%12" PRIzd "Mb\n", lzPosOrg / PGSMRK);
    }
    if (miVerbse>2){
        gpHsh->dist(lzPosOrg, 10);
    }


    if (lcValOrg < EOB)
        return lcValOrg ;
    else
        return 0 ;
} /* ufFndAhdScn */
} /* namespace */
