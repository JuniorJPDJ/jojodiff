/*******************************************************************************
 * JDiff.cpp
 *
 * Jojo's diff on binary files: main class.
 *
 * Copyright (C) 2002-2011 Joris Heirbaut
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

#define PGSMRK 0x100000       // Progress mark: show progress in Mb (1024 * 1024 or 0x400 x 0x400)
#define PGSMSK 0xffffff       // Progress mask: show progress every 16Mb when (lzPos & PGSMSG == 0)

namespace JojoDiff {

/*
 * Constructor
 */
JDiff::JDiff(
    JFile * const apFilOrg,     /* Original file */
    JFile * const apFilNew,     /* New file */
    JOut  * const apOut,        /* Output patch file */
    const int aiHshSze,         /* Hashtable size */
    const int aiVerbse,         /* Verbosity level: between 0 and 3 */
    const int abSrcBkt,         /* Allow backtracking on original file (default: yes) */
    const int aiSrcScn,         /* Scan source-file: 0=no, 1=yes, 2=done */
    const int aiMchMax,         /* Maximum matches to search for */
    const int aiMchMin,         /* Minimum matches to search for */
    const int aiAhdMax,         /* Ahead buffer size (unused ?) */
    const bool abCmpAll         /* Compare all matches ? */
) : mpFilOrg(apFilOrg), mpFilNew(apFilNew), mpOut(apOut),
    miVerbse(aiVerbse), mbSrcBkt(abSrcBkt),
    miMchMax(aiMchMax), miMchMin(aiMchMin),
    miAhdMax(aiAhdMax<1024?1024:aiAhdMax),
    mbCmpAll(abCmpAll), miSrcScn(aiSrcScn),
    mzAhdOrg(0), mzAhdNew(0), mlHshOrg(0), mlHshNew(0), giHshErr(0)
{
	gpHsh = new JHashPos(aiHshSze) ;
	gpMch = new JMatchTable(gpHsh, mpFilOrg, mpFilNew, abCmpAll);
}

/*
 * Destructor
 */
JDiff::~JDiff() {
	delete gpHsh ;
	delete gpMch ;
}

/*******************************************************************************
* Difference function
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
*******************************************************************************/
int JDiff::jdiff()
{
    int lcOrg;              /* byte from original file */
    int lcNew;              /* byte from new file */
    off_t lzPosOrg = 0 ;    /* position in original file */
    off_t lzPosNew = 0 ;    /* position in new file */

    bool  lbEql = false;    /* accumulate equal bytes? */
    off_t lzEql = 0;        /* accumulated equal bytes */

    int liFnd = 0;          /* offsets are pointing to a valid solution (= equal regions) ?   */
    off_t lzAhd=0;          /* number of bytes to advance on both files to reach the solution */
    off_t lzSkpOrg=0;       /* number of bytes to skip on original file to reach the solution */
    off_t lzSkpNew=0;       /* number of bytes to skip on new      file to reach the solution */
    off_t lzLap=0;          /* lap for reducing number of progress messages for -vv           */

//@#if debug
//@    int liErr=0 ;		    /* check for misses : 0=not checking, 1=checking, 2=miss detected */
//@#endif

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
      fprintf(JDebug::stddbg, "\rComparing : %12" PRIzd "Mb", lzPosNew / PGSMRK);
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
 * Flush output
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
    off_t lzFndOrg=0;   /* Found position within original file                 */
    off_t lzFndNew=0;   /* Found position within new file                      */
    off_t lzBseOrg;     /* Base position on original file                      */
    off_t lzLap ;       /* Stop-lap for progress counter                       */

    int liMax;          /* Max number of bytes to look ahead              */
    int liBck;          /* Number of bytes to look back                   */
    int liIdx;          /* Index for initializing                         */
    int liRlb;          /* Reliability range for current hashtable        */

    int liFnd;          /* Number of matches found                        */
    int liSft;          /* 1 = hard look-ahead, 2 = soft look-ahead       */

    /* Set Lap for progress counter */
    if (miVerbse > 1) lzLap = azRedNew + PGSMRK ;

    /* Start with hard lookahead unless the minimum number of matches to find == 0 */
    liSft = ((miMchMin == 0) ? 2 : 1) ;

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
            miValOrg = mpFilOrg->get(mzAhdOrg, 2) ;

            // check if the source position has jumped forward
            if (miValOrg == EOB && azRedOrg > mzAhdOrg){
                mzAhdOrg=0 ; // re-initialize and restart from zero
            }
        }

        // (re-)initialize the hash function
        if (mzAhdOrg==0){
            // scan the part of the source file around the reading position
            mzAhdOrg=mpFilOrg->getBufPos();
            mlHshOrg = 0 ;
            miEqlOrg = 0 ;
            miValOrg = 0 ;

            // initialize the hash function (aiSft == 2 to read from memory)
            miValOrg = mpFilOrg->get(mzAhdOrg, 2) ;
            for (liIdx=0;(liIdx < SMPSZE - 1) && (miValOrg > EOF); liIdx++){
               gpHsh->hash(miValOrg, mlHshOrg, miEqlOrg) ;
               ufFndAhdGet(mpFilOrg, ++ mzAhdOrg, miValOrg, miEqlOrg, 2) ;
            }
        }

        // scan the sourcefile till the buffer is full
        for (liMax=miAhdMax; liMax > 0 && miValOrg > EOF; liMax --){
          gpHsh->hash(miValOrg, mlHshOrg, miEqlOrg) ;
          gpHsh->add(mlHshOrg, mzAhdOrg, miEqlOrg) ;
          ufFndAhdGet(mpFilOrg, ++ mzAhdOrg, miValOrg, miEqlOrg, 2) ;
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
    */
    liRlb = liBck = liMax = gpHsh->get_reliability();
    if (liMax < miAhdMax){
        liMax = miAhdMax;
    }

    /*
    * How many bytes to look back ?
    * In theory: no, it makes no sense to look back
    * In practice:
    * - looking back avoids the need to reinitialize the hash function
    * - reinitialization of the hash function is not 100% correct if miEqlNew > 0
    * - looking back allows to keep the existing match-table up-to-date
    */
    if (liBck < 1024)   // just a reasonable tradeoff between
        liBck = 1024 ;  // reinitialization and a probably useless lookback

    /* Cleanup the old matches */
    liFnd=gpMch->cleanup(azRedNew - 128);

    /* If there's room to work */
    if (liFnd < miMchMax) {
        // Start soft or hard reading */
        if (liFnd >= miMchMin){
            // If there's still a "large" number of matches,
            // then the previous search did not return a good result.
            // Try harder, but be soft
            liFnd = liFnd - (MCH_MAX - liFnd) ;  // can't reduce more
            if (liFnd < miMchMin)
                liFnd=miMchMin ;
            liSft = 2 ;
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
                miValNew = mpFilNew->get(mzAhdNew, liSft) ;

                // force a reset if re-read failed
                if (miValNew == EOB)
                    mzAhdNew = 0;
            }
        }
        if (mzAhdNew == 0 || mzAhdNew + liBck < azRedNew)  {
            // Don't go back more than the buffer allows (to avoid EOB)
            mzAhdNew = mpFilNew->getBufPos() ;
            if (mzAhdNew < 0) mzAhdNew = 0 ;

            // Set looking back position, but never before the buffer
            if (azRedNew > liBck + mzAhdNew){
                mzAhdNew = azRedNew - liBck ;
            }

            // Initialize hash
            miEqlNew = 0 ;
            mlHshNew = 0 ;
            miValNew = mpFilNew->get(mzAhdNew, liSft) ;
            for (liIdx=0;(liIdx < SMPSZE - 1) && (miValNew > EOF); liIdx++){
                gpHsh->hash(miValNew, mlHshNew, miEqlNew) ;
                ufFndAhdGet(mpFilNew, ++ mzAhdNew, miValNew, miEqlNew, liSft) ;
            }
        }

        // Adapt liMax to lookback
        if (mzAhdNew < azRedNew) {
            liBck = (azRedNew - mzAhdNew);
            liMax += liBck;
        } else {
            liBck = 0;
        }

        /* Do not backtrace before lzBseOrg */
        lzBseOrg = (mbSrcBkt?0:mpFilOrg->getBufPos()) ;

        /*
        * Build the table of matches
        */
        while ((liFnd < miMchMax) && (liMax > 0) && (miValNew > EOF )) {
            /* hash the new value */
            gpHsh->hash(miValNew, mlHshNew, miEqlNew) ;

            /* lookup the new value in the hashtable and add it to the table of matches...*/
            if (gpHsh->get(mlHshNew, lzFndOrg)) {
              /* ...unless it's not usable because we've been instructed not to backtrack on source file */
              if (lzFndOrg > lzBseOrg) {
                  /* it's usable: add to the table of matches */
                  liIdx=gpMch->add(lzFndOrg, mzAhdNew, azRedNew);
                  if (liIdx < 0){
                    // panic: table is full unexpectedly, try to cleanup and add again
                    liFnd = gpMch->cleanup(azRedNew);
                    liIdx = gpMch->add(lzFndOrg, mzAhdNew, azRedNew);
                    if (liIdx < 0) break;  // really full
                  } else if (liIdx == 0){
                    // solution added, but table is now full
                    // try to cleanup
                    liFnd = gpMch->cleanup(azRedNew);
                    if (mzAhdNew > azRedNew)
                        liFnd --;
                  }

                  if (liIdx == 0 or liIdx == 1){
                      // solution added
                      if (mzAhdNew > azRedNew) {    // do not count lookback matches
                          liFnd ++ ;

                          if (liFnd==miMchMin)  liSft=2 ;   // switch to soft reading
                          if (liFnd == miMchMax) break;     // stop lookahead

                          // Reduce the lookahead range when minimum number of solutions has been found
                          if ((liFnd == miMchMin) && (liMax > liRlb)) {
                              // The first solution is not always the best one,
                              // due to the unreliable nature of the checksums and the hash-table,
                              // but a better one should be found within the reliability range.
                              //
                              // Why ? Because the reliability range estimates the number of bytes
                              // to search before finding any solutions hidden behind the unreliability.
                              // So after the (estimated) reliability range, no better solution will be found.
                              liMax = liRlb ;
                          }
                      }
                  }
              } /* if usable */
          } /* while ! EOF */

          /* get next value from file */
          ufFndAhdGet(mpFilNew, ++ mzAhdNew, miValNew, miEqlNew, liSft) ;
          liMax -- ;

          /* show progress */
          if ((miVerbse > 1) && (lzLap <= mzAhdNew)) {
            fprintf(JDebug::stddbg, "+%-12" PRIzd "\b\b\b\b\b\b\b\b\b\b\b\b\b", (mzAhdNew - azRedNew) / PGSMRK);
            lzLap=lzLap+PGSMRK;
          }
        } /* while ! EOF */
    } /* if liFnd <= miMchMax */

  /*
   * Check for errors
   */
  if (miValNew < EOB || miValOrg < EOB){
      return (miValNew < miValOrg) ? miValNew : miValOrg;
  }

  /* show progress */
  if ((miVerbse>1) && (lzLap > azRedNew+PGSMRK)){
    fprintf(JDebug::stddbg, "+%-12" PRIzd "...\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b", (mzAhdNew - azRedNew) / PGSMRK);
  }

  /*
   * Get the best match and calculate the offsets
   */
  if (! gpMch->get(azRedOrg, azRedNew, /* out */ lzFndOrg, lzFndNew))
  { azSkpOrg = 0 ;
    azSkpNew = 0 ;
    azAhd    = (mzAhdNew - azRedNew) - gpHsh->get_reliability() ;
    if (azAhd < SMPSZE) azAhd = SMPSZE ;
    return 0 ;
  }  else  {
    if (lzFndOrg >= azRedOrg)
    { if (lzFndOrg - azRedOrg >= lzFndNew - azRedNew)
      { /* go forward on original file */
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
      if (azSkpOrg < azRedOrg)
      { azSkpNew = 0 ;
        azSkpOrg = - azSkpOrg ;
        azAhd = lzFndNew - azRedNew ;
      }
      else /* do not bactrace before beginning of file */
      { azSkpNew = azSkpOrg - azRedOrg ;
        azSkpOrg = - azRedOrg ;
        azAhd = (lzFndNew - azRedNew) - azSkpNew ;
      }

      /* reset ahead position when backtracking */
      mzAhdOrg = 0 ; // TODO reset matching table too?
    }

    /* clear searh progress */
    if ((miVerbse>1) && (lzLap > azRedNew+PGSMRK)){
        fprintf(JDebug::stddbg, "                \b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b") ;
    }

    return 1 ;
  }
} /* ufFndAhd */

/**
 * @brief  Get next character from file (lookahead) and count number of equal chars
 *         in current sample.
 * @detail Auxiliary function to get next character and prepare the equal count for the hash
 *         function.
 * @param  apFil     File to read
 * @param  azPos     Position to read (will be incremented by one)
 * @param  aiVal     in: previous byte read, out: new byte read
 * @param  aiEql     Incremented by one if previous == new byte
 * @param  aiSft     Soft or hard read-ahead (see JFile.get)
 */
void JDiff::ufFndAhdGet(JFile *apFil, const off_t &azPos,int &acVal, int &aiEql, int aiSft) const
{
    int lcNew = apFil->get(azPos, aiSft) ;
    if (acVal != lcNew){
        acVal = lcNew ;
        if (lcNew > EOF && aiEql > 0) aiEql = 0 ;
    }
    else if (aiEql < SMPSZE) aiEql ++ ;
}

/**
 * Prescan the original file: calculates a hash-key for every 32-byte sample
 * in the left file and stores them with their position in a hash-table.
 */
int JDiff::ufFndAhdScn ()
{
    hkey  lkHshOrg=0;     // Current hash value for original file
    int   liEqlOrg=0;     // Number of times current value occurs in hash value
    int   lcValOrg;       // Current file value
    off_t lzPosOrg=0;     // Position within original file

    int liIdx ;

    if (miVerbse > 0) {
        fprintf(JDebug::stddbg, "\nIndexing  : ...           ");
    }

    /* Read SMPSZE-1 bytes (31 or 63) to initialize the hash function */
    lcValOrg = mpFilOrg->get(lzPosOrg, 1) ;
    for (liIdx=0; (liIdx < SMPSZE - 1) && (lcValOrg > EOF); liIdx++) {
        gpHsh->hash(lcValOrg, lkHshOrg, liEqlOrg) ;
        ufFndAhdGet(mpFilOrg, ++ lzPosOrg, lcValOrg, liEqlOrg, 1) ;
    }

    /* Build hashtable */
    if (miVerbse > 1) {
        /* slow version with user feedback */
        while (lcValOrg > EOF) {
            gpHsh->hash(lcValOrg, lkHshOrg, liEqlOrg) ;
            gpHsh->add(lkHshOrg, lzPosOrg, liEqlOrg) ;
            #if debug
                if (JDebug::gbDbg[DBGAHH])
                    fprintf(JDebug::stddbg, "ufHshAdd(%2x -> %8"PRIhkey", "P8zd", %8d)\n",
                            lcValOrg, lkHshOrg, lzPosOrg, 0);
            #endif

            ufFndAhdGet(mpFilOrg, ++ lzPosOrg, lcValOrg, liEqlOrg, 1) ;

            /* output position every 16MB */
            if ((lzPosOrg & PGSMSK) == 0) {
              fprintf(JDebug::stddbg, "\b\b\b\b\b\b\b\b\b\b\b\b\b\b%12" PRIzd "Mb", lzPosOrg / PGSMRK);
            }
        }
    } else {
        /* fast version, no user feedback nor debug */
        while (lcValOrg > EOF) {
            gpHsh->hash(lcValOrg, lkHshOrg, liEqlOrg) ;
            gpHsh->add(lkHshOrg, lzPosOrg, liEqlOrg) ;
            ufFndAhdGet(mpFilOrg, ++ lzPosOrg, lcValOrg, liEqlOrg, 1) ;
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
