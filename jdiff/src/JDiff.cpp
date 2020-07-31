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

        } else if ((liFnd == 1) && (lzEql==0)){ //@882b } && (lzAhd == 0)) {
            // Oops: the "found" solution did not point to an equal region
            // This may happen, especialy for non-compared solutions,
            // but we should hope this does not happen too much
            //@882 lzAhd = SMPSZE / 2 ; // advance at least some bytes to avoid infine loop when ufFabFnd persists with lzSkpOrg, lzSkpNew, lzAhd all zero)
            liFnd = 0 ;

            //@#if debug
            //liErr = 2 ;	// ufFabFnd did not point to an equal region !
            //@#endif
            //@if (liErr==2 && (miVerbse>2 || mbCmpAll)){

            // Report the miss to our user
            giHshErr++ ;
            if (miVerbse>2){
              fprintf(JDebug::stddbg, "Hash miss !\n");
              //@liErr=0; // clear error state
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

            /* Output accumulated equals */
          	ufPutEql(lzPosOrg, lzPosNew, lzEql, lbEql);

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

            //@882b new skip ahead logic
            /* Avoid an infinite loop if ufFnhAhd doesn't advance.   */
            /* Usually, this happens only after a hash miss.         */
//            if (lzSkpNew == 0 && lzAhd == 0){
//                lzAhd=SMPSZE / 2; /* Advance at least half-a samplesize bock */
//                if (miVerbse>2){
//                  fprintf(JDebug::stddbg, "Skipping %" PRIzd " bytes !\n",lzAhd);
//                }
//            }

            /* show progress */
            if ((miVerbse > 1) && (lzLap <= lzPosNew)) {
              fprintf(JDebug::stddbg, "\rComparing : %12" PRIzd "Mb", lzPosNew / PGSMRK);
              lzLap=lzPosNew+PGSMRK;
            }

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

    /* Return code */
    if (lcNew < EOB || lcOrg < EOB){
        return (lcNew < lcOrg)?lcNew:lcOrg;
    }
    return 0;
} /* jdiff */

/**
 * Flush output
 */
void JDiff::ufPutEql(const off_t &lzPosOrg, const off_t &lzPosNew, off_t &lzEql, bool &lbEql){
    /* Output accumulated equals */
    if (lzEql > 0){
        mpOut->put(EQL, lzEql, 0, 0, lzPosOrg - lzEql, lzPosNew - lzEql);
        lzEql = 0;
    }
    lbEql=false;
}

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
)
{ off_t lzFndOrg=0;   /* Found position within original file                 */
  off_t lzFndNew=0;   /* Found position within new file                      */
  off_t lzBseOrg;     /* Base position on original file: gbSrcBkt?0:alRedOrg */

  int liMax;          /* Max number of bytes to read                    */
  int liIdx;          /* Index for initializing                         */
  int liFnd=0;        /* Number of matches found                        */
  int liSft;          /* 1 = hard look-ahead, 2 = soft look-ahead       */

  /* Start with hard lookahead unless the minimum number of matches to find == 0 */
  liSft = miMchMin > 0 ? 1 : 2 ;

  /* Prescan the source file? */
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

            // initialize the hash function (aiSft == 2 to read from memory)
            miValOrg = mpFilOrg->get(mzAhdOrg, 2) ;
            for (liIdx=0;(liIdx < SMPSZE - 1) && (miValOrg > EOF); liIdx++){
              gpHsh->hash(miValOrg, mlHshOrg) ;
              ufFndAhdGet(mpFilOrg, ++ mzAhdOrg, miValOrg, miEqlOrg, 2) ;
            }
        }

        // scan the sourcefile till the buffer is full
        for (liMax=miAhdMax; liMax > 0 && miValOrg > EOF; liMax --){
          gpHsh->hash(miValOrg, mlHshOrg) ;
          gpHsh->add(mlHshOrg, mzAhdOrg, miEqlOrg) ;
          ufFndAhdGet(mpFilOrg, ++ mzAhdOrg, miValOrg, miEqlOrg, 2) ;
        }
        break ;
  }

  /*
   * How many bytes to look ahead (search) ?
   * As far as possible, but avoid to lose too much time on unequal files.
   */
//@ if (miSrcScn == 2){ // 0.8.2b
//@ if (mzAhdNew == 0 || mzAhdNew < azRedNew) {
//          liMax = miAhdMax  ;
//      } else if (mzAhdNew > azRedNew + miAhdMax) {
//          liMax = miAhdMax  ;
//      } else {
//          liMax = miAhdMax - (mzAhdNew - azRedNew)  ;
//      }
//  } else {*/
      liMax = INT_MAX / 2 ;  // as far as possible
//  }

  /*
   * How many bytes to look back ?
   */
  int liBck; /* Number of bytes to look back */
  if (gpHsh->get_reliability() < miAhdMax)
      liBck = gpHsh->get_reliability() ; //@082b / 2 ;
  else
      liBck = miAhdMax / 2 ;    /* go back half a buffer (to avoid reading from disk) */

  /*
   * Re-Initialize hash function (read 31 or 63 bytes) if
   * - ahead position has been reset, or
   * - read position has jumped over the ahead position
   */
//@  if (miSrcScn == 0){
//    if ((mzAhdOrg == 0 || mzAhdOrg + liBck < azRedOrg)) {
//        mzAhdOrg = azRedOrg - liBck ;
//        if (mzAhdOrg < 0) mzAhdOrg = 0 ;
//
//        mlHshOrg = 0 ;
//        miEqlOrg = 0 ;
//        miValOrg = mpFilOrg->get(mzAhdOrg, liSft) ;
//        for (liIdx=0;(liIdx < SMPSZE - 1) && (miValOrg > EOF); liIdx++){
//          gpHsh->hash(miValOrg, mlHshOrg) ;
//          ufFndAhdGet(mpFilOrg, ++ mzAhdOrg, miValOrg, miEqlOrg, liSft) ;
//        }
//    } else if (miValOrg==EOB) {
//        // Reset an EOB (or at least, try to)
//        miValOrg = mpFilOrg->get(mzAhdOrg, liSft) ;
//    }
//  }
  if (mzAhdNew == 0 || mzAhdNew + liBck < azRedNew)  {
    mzAhdNew = azRedNew - liBck ;
    if (mzAhdNew < 0) mzAhdNew = 0 ;
    liMax += liBck ;    //@ TODO better remove this and make liMax=INT_MAX

    miEqlNew = 0 ;
    mlHshNew = 0 ;
    miValNew = mpFilNew->get(mzAhdNew, liSft) ;
    liMax -- ;
    for (liIdx=0;(liIdx < SMPSZE - 1) && (miValNew > EOF); liIdx++){
      gpHsh->hash(miValNew, mlHshNew) ;
      ufFndAhdGet(mpFilNew, ++ mzAhdNew, miValNew, miEqlNew, liSft) ;
      liMax -- ;
    }
  } else if (miValNew == EOB){
    // Reset EOB (or at least, try to)
    miValNew = mpFilNew->get(mzAhdNew, liSft) ;
  }

  /*
   * Build the table of matches
   */
  if (gpMch->cleanup(azRedNew - gpHsh->get_reliability())){
      /* Do not backtrace before lzBseOrg */
      lzBseOrg = (mbSrcBkt?0:azRedOrg) ;

      /* Do not read from original file if it has been prescanned */
      if (miSrcScn > 0)
            miValOrg = EOB ;

      /* Read-ahead on both files until an equal hash value has been found */
      while ((liMax > 0) && ((miValNew > EOF ) || (miValOrg > EOF))) {
          /* insert original file's value into hashtable (if no prescanning has been done) */
          if (miValOrg > EOF){
              /* hash the new value and add to hashtable */
              gpHsh->hash(miValOrg, mlHshOrg) ;
              gpHsh->add(mlHshOrg, mzAhdOrg, miEqlOrg) ;

              #if debug
              if (JDebug::gbDbg[DBGAHH])
                  fprintf(JDebug::stddbg, "ufHshAdd(%2x -> %8"PRIhkey", "P8zd", "P8zd")\n",
                          miValOrg, mlHshOrg, mzAhdOrg, lzBseOrg);
              #endif

              /* get next value from file */
              ufFndAhdGet(mpFilOrg, ++ mzAhdOrg, miValOrg, miEqlOrg, liSft) ;
          }

          /* check new file against original file */
          if (miValNew > EOF){
              /* hash the new value and lookup in hashtable */
              gpHsh->hash(miValNew, mlHshNew) ;
              if (gpHsh->get(mlHshNew, lzFndOrg)) {
                  /* there's a match: add it to the table of matches if its usuable */
                  if (lzFndOrg > lzBseOrg) {
                      /* verify the match and add to the table of solutions if ok */
                      switch (gpMch->add(lzFndOrg, mzAhdNew, azRedNew, miEqlNew)){
                        case 0: /* solution added, table is full */
                          if (liBck > 0 && gpMch->cleanup(azRedNew)){
                              // tried to make more room
                          } else {
                              liMax = 0 ; // stop lookahead
                              continue;
                          }
                          // no break !
                        case 1: /* new solution found added */
                          if (mzAhdNew > azRedNew) {
                              liFnd ++ ;

                              // Switch to soft lookahead if we found a minimum number of matches
                              if (liFnd==miMchMin){
                                liSft=2 ;
                              }

                              // Stop lookahead if the maximum number of matches is reached
                              if (liFnd == miMchMax) {
                                  liMax = 0 ; // stop lookahead
                                  continue;
                              }

                              // Reduce the lookahead range when minimum number of solutions has been found
                              if ((liFnd == miMchMin) && (liMax > gpHsh->get_reliability() * 2)) {
                                  // The first solution is not always the best one,
                                  // due to the unreliable nature of the checksums and the hash-table,
                                  // but a better one should be found within the reliability range.
                                  //
                                  // Why ? Because the reliability range estimates the number of bytes
                                  // to search before finding any solutions hidden behind the unreliability.
                                  // So after the (estimated) reliability range, no better solution will be found.
                                  liMax = gpHsh->get_reliability() ;
                              }
                          }
                          break ;
                        case 2:  ; /* alternative collided */
                          // the match confirmed an already existing solution: great !
                        case -1: ;/* compare failed      */
                          // the match verification failed: can happen (checksums don't guarantee equality).
                      }
                  }
              }

              /* get next value from file */
              ufFndAhdGet(mpFilNew, ++ mzAhdNew, miValNew, miEqlNew, liSft) ;
              liMax -- ;
          } /* if siValNew > EOF */
      } /* while */
  } /* if ufMchFre(..) */

  /*
   * Check for errors
   */
  if (miValNew < EOB || miValOrg < EOB){
      return (miValNew < miValOrg) ? miValNew : miValOrg;
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

    return 1 ;
  }
}

/* -----------------------------------------------------------------------------
 * Auxiliary function:
 * Get next character from file (lookahead) and count number of equal chars
 * in current cluster
 * ---------------------------------------------------------------------------*/
/**
 * @brief Get next character from file (lookahead) and count number of equal chars
 *        in current sample.
 * @param apFil     File to read
 * @param azPos     Position to read (will be incremented by one)
 * @param aiVal     in: previous byte read, out: new byte read
 * @param aiEql     Incremented by one if previous == new byte
 * @param aiSft     Soft or hard read-ahead (see JFile.get)
 */
void JDiff::ufFndAhdGet(JFile *apFil, const off_t &azPos, int &acVal, int &aiEql, int aiSft)
{
  int lcPrv = acVal ;
  acVal = apFil->get(azPos, aiSft) ;
  if (acVal != lcPrv) {
      if (aiEql > 0) aiEql -= 2 ;
  } else {
      if (aiEql < SMPSZE) aiEql += 1 ;
  }
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
    for (liIdx=0;(liIdx < SMPSZE - 1) && (lcValOrg > EOF); liIdx++) {
        gpHsh->hash(lcValOrg, lkHshOrg) ;
        ufFndAhdGet(mpFilOrg, ++ lzPosOrg, lcValOrg, liEqlOrg, 1) ;
    }

    /* Build hashtable */
    if (miVerbse > 1) {
        /* slow version with user feedback */
        while (lcValOrg > EOF) {
            gpHsh->hash(lcValOrg, lkHshOrg) ;
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
            gpHsh->hash(lcValOrg, lkHshOrg) ;
            gpHsh->add(lkHshOrg, lzPosOrg, liEqlOrg) ;
            ufFndAhdGet(mpFilOrg, ++ lzPosOrg, lcValOrg, liEqlOrg, 1) ;
        }
    }

    if (miVerbse > 0) {
        /* output final position */
        fprintf(JDebug::stddbg, "\b\b\b\b\b\b\b\b\b\b\b\b\b\b%12" PRIzd "Mb\n", lzPosOrg / PGSMRK);
    }
    if (miVerbse>2){
    //#if debug
    //    if (JDebug::gbDbg[DBGDST])
          gpHsh->dist(lzPosOrg, 10);
    //#endif
    }


    if (lcValOrg < EOB)
        return lcValOrg ;
    else
        return 0 ;
} /* ufFndAhdScn */
} /* namespace */
