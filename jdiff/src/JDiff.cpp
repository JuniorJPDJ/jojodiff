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

#define XSTR(x) STR(x)
#define STR(x) #x
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

#if debug
    int liErr=0 ;		/* check for malfunction: 0=not checking, 1=checking, 2=error */
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
            lzAhd = SMPSZE ; // to avoid infine loop (when ufFabFnd persists with lzSkpOrg, lzSkpNew, lzAhd all zero)
            liFnd = 0 ;
            #if debug
            liErr = 2 ;	// ufFabFnd did not point to an equal region !
            #endif
        } else {
            #if debug
            /* Flush output buffer in debug */
            if (JDebug::gbDbg[DBGAHD] || JDebug::gbDbg[DBGMCH]){
              ufPutEql(lzPosOrg, lzPosNew, lzEql, lbEql);
              mpOut->put(ESC, 0, 0, 0, lzPosOrg, lzPosNew);
            }

            /* If lbChk is unset, then an expected equal block has not been reached.
             * In fast mode, when doing non-compared jumps, this may happen.
             * In normal mode (mbCmpAll), this should never happen.
             */
            if (liErr==2 && (miVerbse>2 || mbCmpAll)){
              fprintf(JDebug::stddbg, "Hash miss!\n");
              giHshErr++ ;
            }
            liErr=0; // clear error state
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

  int liIdx;          /* Index for initializing                         */
  int liFnd=0;        /* Number of matches found                        */
  int liSft;          /* 1 = hard look-ahead, 2 = soft look-ahead       */

  /* Start with hard lookahead, till we've found at least one match */
  liSft = 1 ;

  /* Prescan the original file? */
  if (miSrcScn == 1) {
    int liRet = ufFndAhdScn() ;
    if (liRet < 0) return liRet ;
    miSrcScn = 2 ;
  }

  /*
   * How many bytes to look ahead ?
   */
  int liMax; /* Max number of bytes to read */
  if (miSrcScn == 2){
      if (mzAhdNew == 0 || mzAhdNew < azRedNew) {
          liMax = miAhdMax  ;
      } else if (mzAhdNew > azRedNew + miAhdMax) {
          liMax = miAhdMax  ;
      } else {
          liMax = miAhdMax - (mzAhdNew - azRedNew)  ;
      }
  } else {
      liMax = INT_MAX / 2 ;  // as far as possible
  }

  /*
   * How many bytes to look back ?
   */
  int liBck; /* Number of bytes to look back */
  if (gpHsh->get_reliability() < miAhdMax)
      liBck = gpHsh->get_reliability() / 2 ;
  else
      liBck = miAhdMax / 2 ;

  /*
   * Re-Initialize hash function (read 31 or 63 bytes) if
   * - ahead position has been reset, or
   * - read position has passed the ahead position
   */
  if (miSrcScn == 0 && (mzAhdOrg == 0 || mzAhdOrg + liBck < azRedOrg)) {
    mzAhdOrg = azRedOrg - liBck ;
    if (mzAhdOrg < 0) mzAhdOrg = 0 ;
    miEqlOrg = 0 ;
    mlHshOrg = 0 ;

    miEqlOrg = 0 ;
    miValOrg = mpFilOrg->get(mzAhdOrg, liSft) ;
    for (liIdx=0;(liIdx < SMPSZE - 1) && (miValOrg > EOF); liIdx++){
      gpHsh->hash(miValOrg, mlHshOrg) ;
      ufFndAhdGet(mpFilOrg, ++ mzAhdOrg, miValOrg, miEqlOrg, liSft) ;
    }
  }
  if (mzAhdNew == 0 || mzAhdNew + liBck < azRedNew)  {
    mzAhdNew = azRedNew - liBck ;
    if (mzAhdNew < 0) mzAhdNew = 0 ;
    miEqlNew = 0 ;
    mlHshNew = 0 ;
    liMax += liBck ;

    miEqlNew = 0 ;
    miValNew = mpFilNew->get(mzAhdNew, liSft) ;
    liMax -- ;
    for (liIdx=0;(liIdx < SMPSZE - 1) && (miValNew > EOF); liIdx++){
      gpHsh->hash(miValNew, mlHshNew) ;
      ufFndAhdGet(mpFilNew, ++ mzAhdNew, miValNew, miEqlNew, liSft) ;
      liMax -- ;
    }
  }

  /*
   * Build the table of matches
   */
  if (gpMch->cleanup(azRedNew - gpHsh->get_reliability())){
      /* Do not backtrace before lzBseOrg */
      lzBseOrg = (mbSrcBkt?0:azRedOrg) ;

      /* Do not read from original file if it has been prescanned */
      if (miSrcScn > 0) miValOrg = EOB ;

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
                  /* add found position into table of matches */
                  if (lzFndOrg > lzBseOrg) {
                      /* add solution to the table of matches */
                      switch (gpMch->add(lzFndOrg, mzAhdNew, azRedNew, miEqlNew))
                      { case 0: /* table is full */
                          if (liBck > 0 && gpMch->cleanup(azRedNew)){
                              // made more room
                          } else {
                              liMax = 0 ; // stop lookahead
                              continue;
                          }
                      case 1: /* alternative added */
                          if (mzAhdNew > azRedNew) {
                              liFnd ++ ;

                              if (liFnd == miMchMax) {
                                  liMax = 0 ; // stop lookahead
                                  continue;
                              } else if ((liFnd == miMchMin) && (liMax > gpHsh->get_reliability())) {
                                  liMax = gpHsh->get_reliability() ; // reduce lookahead
                              }
                          }
                          break ;
                      case 2:  ; /* alternative collided */
                      case -1: ;/* compare failed      */
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

    #if debug
        if (JDebug::gbDbg[DBGDST])
          gpHsh->dist(lzPosOrg, 128);
    #endif

    if (lcValOrg < EOB)
        return lcValOrg ;
    else
        return 0 ;
} /* ufFndAhdScn */
} /* namespace */
