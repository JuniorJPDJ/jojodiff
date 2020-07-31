/*******************************************************************************
 * Jojo's Diff : diff on binary files
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
 *
 * Usage:
 * ------
 * jdiff [options] <original file> <new file> [<output file>]
 *   -v          Verbose (greeting, results and tips).
 *   -vv         Verbose (debug info).
 *   -h          Help (this text).
 *   -l          Listing (ascii output).
 *   -r          Regions (ascii output).
 *   -b          Try to be better (using more memory).
 *   -f          Try to be faster: no out of buffer compares.
 *   -ff         Try to be faster: no out of buffer compares, nor pre-scanning.
 *   -m size     Size (in kB) for look-ahead buffers (default 128).
 *   -k size     Block size (in bytes) for reading from files (default 4096).
 *   -s size     Number of samples per file (e.g. 8192).
 *   -n count    Minimum number of solutions to find before choosing one.
 *   -x count    Maximum number of solutions to find before choosing one.
 *
 * Exit codes
 * ----------
 *  0  ok, differences found
 *  1  ok, no differences found
 *  2  error: not enough arguments
 *  3  error: could not open first input file
 *  4  error: could not open second input file
 *  5  error: could not open output file
 *  6  error: seek or other i/o error when reading or writing
 *  7  error: 64 bit numbers not supported
 *  8  error on reading
 *  9  error on writing
 *  10  error: malloc failed
 *  20  error: any other error
 *
 * Author                Version Date       Modification
 * --------------------- ------- -------    -----------------------
 * Joris Heirbaut        v0.0    10-06-2002 hashed compare
 * Joris Heirbaut                14-06-2002 full compare
 * Joris Heirbaut                17-06-2002 global positions
 * Joris Heirbaut        v0.1    18-06-2002 first well-working runs!!!
 * Joris Heirbaut                19-06-2002 compare in buffer before read position
 * Joris Heirbaut        v0.1    20-06-2002 optimized esc-sequences & lengths
 * Joris Heirbaut        v0.2    24-06-2002 running okay again
 * Joris Heirbaut        v0.2b   01-07-2002 bugfix on length=252
 * Joris Heirbaut        v0.2c   09-07-2002 bugfix on divide by zero in statistics
 * Joris Heirbaut        v0.3a   09-07-2002 hashtable hint only on samplerate
 * Joris Heirbaut          |     09-07-2002 exit code 1 if files are equal
 * Joris Heirbaut          |     12-07-2002 bugfix using ufFabPos in function call
 * Joris Heirbaut        v0.3a   16-07-2002 backtrack on original file
 * Joris Heirbaut        v0.4a   19-07-2002 prescan sourcefile
 * Joris Heirbaut          |     30-08-2002 bugfix in ufFabRst and ufFabPos
 * Joris Heirbaut          |     03-08-2002 bugfix for backtrack before start-of-file
 * Joris Heirbaut          |     09-09-2002 reimplemented filebuffer
 * Joris Heirbaut        v0.4a   10-09-2002 take best of multiple possibilities
 * Joris Heirbaut        v0.4b   11-09-2002 soft-reading from files
 * Joris Heirbaut          |     18-09-2002 moved ufFabCmp from ufFndAhdChk to ufFndAhdAdd/Bst
 * Joris Heirbaut          |     18-09-2002 ufFabOpt - optimize a found solution
 * Joris Heirbaut          |     10-10-2002 added Fab->izPosEof to correctly handle EOF condition
 * Joris Heirbaut        v0.4b   16-10-2002 replaces ufFabCmpBck and ufFabCmpFwd with ufFabFnd
 * Joris Heirbaut        v0.4c   04-11-2002 use ufHshFnd after prescanning
 * Joris Heirbaut          |     04-11-2002 no reset of matching table
 * Joris Heirbaut          |     21-12-2002 rewrite of matching table logic
 * Joris Heirbaut          |     24-12-2002 no compare in ufFndAhdAdd
 * Joris Heirbaut          |     02-01-2003 restart finding matches at regular intervals when no matches are found
 * Joris Heirbaut          |     09-01-2003 renamed ufFabBkt to ufFabSek, use it for DEL and BKT instructions
 * Joris Heirbaut        v0.4c   23-01-2003 distinguish between EOF en EOB
 * Joris Heirbaut        v0.5    27-02-2003 dropped "fast" hash method (it was slow!)
 * Joris Heirbaut          |     22-05-2003 started    rewrite of FAB-abstraction
 * Joris Heirbaut          |     30-06-2003 terminated rewrite ...
 * Joris Heirbaut          |     08-07-2003 correction in ufMchBst (llTstNxt = *alBstNew + 1 iso -1)
 * Joris Heirbaut        v0.5    02-09-2003 production
 * Joris Heirbaut        v0.6    29-04-2005 large-file support
 * Joris Heirbaut        v0.7    23-06-2009 differentiate between position 0 and notfound in ufMchBst
 * Joris Heirbaut          |     23-06-2009 optimize collission strategy using sample quality
 * Joris Heirbaut          |     24-06-2009 introduce quality of samples
 * Joris Heirbaut          |     26-06-2009 protect first samples
 * Joris Heirbaut        v0.7g   24-09-2009 use fseeko for cygwin largefiles
 * Joris Heirbaut          |     24-09-2009 removed casts to int from ufFndAhd
 * Joris Heirbaut        v0.7h   24-09-2009 faster ufFabGetNxt
 * Joris Heirbaut          |     24-09-2009 faster ufHshAdd: remove quality of hashes
 * Joris Heirbaut        v0.7i   25-09-2009 drop use of ufHshBitCnt
 * Joris Heirbaut        v0.7l   04-10-2009 increment glMchMaxDst as hashtable overloading grows
 * Joris Heirbaut          |     16-10-2009 finalization
 * Joris Heirbaut        v0.7m   17-10-2009 gprof optimization ufHshAdd
 * Joris Heirbaut        v0.7n   17-10-2009 gprof optimization ufFabGet
 * Joris Heirbaut        v0.7o   18-10-2009 gprof optimization asFab->iiRedSze
 * Joris Heirbaut        v0.7p   19-10-2009 ufHshAdd: check uniform distribution
 * Joris Heirbaut        v0.7q   23-10-2009 ufFabGet: scroll back on buffer
 * Joris Heirbaut          |     19-10-2009 ufFndAhd: liMax = giBufSze
 * Joris Heirbaut          |     25-10-2009 ufMchAdd: gliding matches
 * Joris Heirbaut          |     25-10-2009 ufOut: return true for faster EQL sequences in jdiff function
 * Joris Heirbaut        v0.7r   27-10-2009 ufMchBst: test position for gliding matches
 * Joris Heirbaut          |     27-10-2009 ufMchBst: remove double loop
 * Joris Heirbaut          |     27-10-2009 ufMchBst: double linked list ordered on azPosOrg
 * Joris Heirbaut          |     27-10-2009 ufFndAhd: look back on reset (liBck)
 * Joris Heirbaut          |     27-10-2009 ufFndAhd: reduce lookahead after giMchMin (speed optimization)
 * Joris Heirbaut        v0.7x   05-11-2009 ufMchAdd: hashed method
 * Joris Heirbaut        v0.7y   13-11-2009 ufMchBst: store unmatched samples too (reduce use of ufFabFnd)
 * Joris Heirbaut        v0.8    Sep  2011  Conversion to C++
 * Joris Heirbaut        v0.8.1  Dec  2011  Correction in Windows exe for files > 2GB
 * Joris Heirbaut        v0.8.2  Dec  2015  use jfopen/jfclose/jfread/jfseek to avoid interferences with LARGEFILE redefinitions
 * Joris Heirbaut          |     Feb  2015  bugfix: virtual destructors for JFile and JOut to avoid memory leaks
 * Joris Heirbaut        v0.8.3  July 2020  improved progress feedback
 *                                          use getopts_long for option processing
 *
 *******************************************************************************/

/*
 * Includes
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <inttypes.h>
#include <getopt.h>

using namespace std ;

#ifdef __MINGW32__
#include "JFileAhead.h"
#else
#include <iostream>
#include <istream>
#include <fstream>
#include "JFileIStream.h"
#include "JFileIStreamAhead.h"
#endif

#include "JDefs.h"
#include "JDiff.h"
#include "JOutBin.h"
#include "JOutAsc.h"
#include "JOutRgn.h"
#include "JFile.h"

using namespace JojoDiff ;

/*********************************************************************************
* Options parsing
*********************************************************************************/
const char *gcOptSht = "a:bcd:fhi:k:lm:n:pqrvx:"; /* u:: for optional aruments */

struct option gsOptLng [] = {
        {"better",            no_argument,      NULL,'b'},
        {"faster",            no_argument,      NULL,'f'},
        {"console",           no_argument,      NULL,'c'},
        {"debug",             required_argument,NULL,'d'},
        {"help",              no_argument,      NULL,'h'},
        {"listing",           no_argument,      NULL,'l'},
        {"regions",           no_argument,      NULL,'r'},
        {"sequential-source", no_argument,      NULL,'p'},
        {"sequential-dest",   no_argument,      NULL,'q'},
        {"index-size",        required_argument,NULL,'i'},
        {"block-size",        required_argument,NULL,'k'},
        {"buffer-size",       required_argument,NULL,'m'},
        {"search-size",       required_argument,NULL,'a'},
        {"search-min",        required_argument,NULL,'n'},
        {"search-max",        required_argument,NULL,'x'},
        {"verbose",           no_argument,      NULL,'v'},
//      {"log-summary",       optional_argument,NULL,'u'},
        {NULL,0,NULL,0}
};

/************************************************************************************
* Main function
*************************************************************************************/
int main(int aiArgCnt, char *acArg[])
{
  const char *lcFilNamOrg;      /* Source filename                                  */
  const char *lcFilNamNew;      /* Destination filename                             */
  const char *lcFilNamOut;      /* Output filename (-=stdout)                       */

  FILE  *lpFilOut;              /* Output file                                      */

  /* Default settings */
  int liOutTyp = 0 ;            /* 0 = JOutBin, 1 = JOutAsc, 2 = JOutRgn            */
  int liVerbse = 0;             /* Verbose level 0=no, 1=normal, 2=high             */
  int lbSrcBkt = true;          /* Backtrace on sourcefile allowed?                 */
  bool lbCmpAll = true ;        /* Compare even if data not in buffer?              */
  int liSrcScn = 1 ;            /* Prescan source file: 0=no, 1=do, 2=done          */
  int liMchMax = 32 ;           /* Maximum entries in matching table.               */
  int liMchMin = 8 ;            /* Minimum entries in matching table.               */
  int liHshMbt = 8 ;            /* Hashtable size in mega-samples (* 1024 * 1024)   */
  long llBufSze = 1024*1024 ;   /* Default file-buffers size (in bytes)             */
  int liBlkSze = 8192 ;         /* Default block size (in bytes)                    */
  int liAhdMax = 0;             /* Lookahead range (0=same as llBufSze)             */
  int liHlp=0;                  /* -h/--help flag: 0=no, 1=-h, 2=-hh, 3=error       */

  JDebug::stddbg = stderr ;     /* Debug and informational (verbose) output         */

  /* optional arguments parsing */
  int liOptArgCnt=0 ;           /* number of options */
  char lcOptSht;                /* short option code */
  int liOptLng;                 /* long option index */

  /* TODO: create options
    -p for sequential source: no indexing nor out-of-buffer reads for source file, pipe or stdin allowed
    -q for sequential destination: no out-of-buffer reads for destination file, pipe or stdin allowed
    ==> drop lbCmpAll and lbSrcBkt, replace by lbSeqOrg, lbSeqNew

    Use standard getopt with long options.
    */

  /* Read options */
  /* parse option-switches */
  while((lcOptSht = getopt_long(aiArgCnt, acArg, gcOptSht, gsOptLng, &liOptLng))!=EOF){
    switch(lcOptSht){
    case 'b': // try-harder: increase hashtable size and more searching
        lbCmpAll = true ;         // verify all hashtable matches
        llBufSze = 4096 * 1024 ;  // larger buffer (more soft-ahead searching)
        lbSrcBkt = true;          // allow going back on source file
        liSrcScn = 1 ;            // create full index on source file
        liMchMin = 16 ;           // search at least 16 matches (go out of buffer if needed)
        liMchMax = MCH_MAX ;      // maximum buffered search
        liHshMbt = 32 ;           // +/-32meg elements
        break;
    case 'f': // faster
        if (lbCmpAll){
            lbCmpAll = false ;      // No compares out-of-buffer (only verify hashtable matches when data is available in memory buffers)
            llBufSze = 64 * 1024 ;
            lbSrcBkt = true ;
            liSrcScn = 1  ;
            liMchMin = 8 ;
            liMchMax = 16 ;
            liHshMbt = 8 ;          // 8Meg samples
        } else {
            // even faster
            lbCmpAll = false ;      // No compares out-of-buffer
            llBufSze = 4096 * 1024; // increase buffer size to have more lookahead indexing
            lbSrcBkt = true ;
            liSrcScn = 0 ;          // No indexing scan, indexing is limited ookahead search
            liMchMin = 4 ;
            liMchMax = MCH_MAX ;    // Increase buffered lookahead to its maximum
            liHshMbt = 4 ;          // 4Meg samples
        }
        break;
    case 'p': // sequential source file
        lbCmpAll=false ;            // only compare data within the buffer
        lbSrcBkt=false ;            // only advance on source file
        liSrcScn=0;                 // no pre-scan indexing
        liMchMin=0;                 // do not search out of buffer
        break;
    case 'q': // sequential destination file
        lbCmpAll=false ;            // only compare data within the buffer
        liMchMin=0;                 // do not search out of buffer
        break;
    case 'c': // verbose-stdout
        JDebug::stddbg = stdout;
        break;
    case 'h': // help
        liHlp++;
        break;
    case 'v': // "verbose",           no_argument
        liVerbse++;
        break;
    case 'l': // "list-details",      no_argument
        liOutTyp = 1 ;
        break;
    case 'r': // "list-groups",       no_argument
        liOutTyp = 2 ;
        break;
    case 'a': // search-ahead-size
        liAhdMax = atoi(optarg) / 2 * 1024 ;
        break;
    case 'i': // index-size
        liHshMbt = atoi(acArg[liOptArgCnt]) ;
        while (liHshMbt > 1024) liHshMbt /= 1024 ;
        break;
    case 'k': // "block-size"
        liBlkSze = atoi(optarg) ;
        break;
    case 'm': // "buffer-size",       required_argument
        llBufSze = atoi(optarg) / 2 * 1024;
        break;
    case 'n': // "search-min",        required_argument
        liMchMin = atoi(optarg) ;
        if (liMchMin > MCH_MAX)
          liMchMin = MCH_MAX ;
        break;
    case 'x': // "search-max",        required_argument
        liMchMax = atoi(optarg) ;
        if (liMchMax > MCH_MAX)
          liMchMax = MCH_MAX ;
        break;

    #if debug
    case 'd': // debug
        if (strcmp(optarg, "-dhsh") == 0) {
          JDebug::gbDbg[DBGHSH] = true ;
        } else if (strcmp(optarg, "-dahd") == 0) {
          JDebug::gbDbg[DBGAHD] = true ;
        } else if (strcmp(optarg, "-dcmp") == 0) {
          JDebug::gbDbg[DBGCMP] = true ;
        } else if (strcmp(optarg, "-dprg") == 0) {
          JDebug::gbDbg[DBGPRG] = true ;
        } else if (strcmp(optarg, "-dbuf") == 0) {
          JDebug::gbDbg[DBGBUF] = true ;
        } else if (strcmp(optarg, "-dhsk") == 0) {
          JDebug::gbDbg[DBGHSK] = true ;
        } else if (strcmp(optarg, "-dahh") == 0) {
          JDebug::gbDbg[DBGAHH] = true ;
        } else if (strcmp(optarg, "-dbkt") == 0) {
          JDebug::gbDbg[DBGBKT] = true ;
        } else if (strcmp(optarg, "-dred") == 0) {
          JDebug::gbDbg[DBGRED] = true ;
        } else if (strcmp(optarg, "-dmch") == 0) {
          JDebug::gbDbg[DBGMCH] = true ;
        } else if (strcmp(optarg, "-ddst") == 0) {
          JDebug::gbDbg[DBGDST] = true ;
        }
        break ;
    #endif
    default:
        liHlp=1;
    }
  }
  liOptArgCnt=optind-1;

//  int lbOptArgDne=false ;
  /*while (! lbOptArgDne && (aiArgCnt-1 > liOptArgCnt)) {
    liOptArgCnt++ ;
    } else if (strcmp(acArg[liOptArgCnt], "-ff") == 0) {
    } else if (strcmp(acArg[liOptArgCnt], "-p") == 0) {

    } else if (strcmp(acArg[liOptArgCnt], "-0") == 0) {
        lbSrcBkt = false ;
    } else if (strcmp(acArg[liOptArgCnt], "-v") == 0) {
      liVerbse = 1;
    } else if (strcmp(acArg[liOptArgCnt], "-vv") == 0) {
      liVerbse = 2;
    } else if (strcmp(acArg[liOptArgCnt], "-vvv") == 0) {
      liVerbse = 3;
    } else if (strcmp(acArg[liOptArgCnt], "-h") == 0) {
      lcHlp = 'h' ;

    } else if (strcmp(acArg[liOptArgCnt], "-a") == 0) {
        liOptArgCnt ++;
        if (aiArgCnt > liOptArgCnt) {
          liAhdMax = atoi(acArg[liOptArgCnt]) / 2 * 1024 ;
        }
    } else if (strcmp(acArg[liOptArgCnt], "-m") == 0) {
        liOptArgCnt ++;
        if (aiArgCnt > liOptArgCnt) {
          llBufSze = atoi(acArg[liOptArgCnt]) / 2 * 1024;
        }
    } else if (strcmp(acArg[liOptArgCnt], "-b") == 0) {
        liOptArgCnt ++;
        if (aiArgCnt > liOptArgCnt) {
        	liBlkSze = atoi(acArg[liOptArgCnt]) ;
        }
    } else if (strcmp(acArg[liOptArgCnt], "-i") == 0) {
        liOptArgCnt++;
        if (aiArgCnt > liOptArgCnt) {
        	liHshMbt = atoi(acArg[liOptArgCnt]) ;
        	while (liHshMbt > 1024) liHshMbt /= 1024 ;
        }
    } else if (strcmp(acArg[liOptArgCnt], "-n") == 0) {
        liOptArgCnt++;
        if (aiArgCnt > liOptArgCnt) {
          liMchMin = atoi(acArg[liOptArgCnt]) ;
          if (liMchMin > MCH_MAX)
              liMchMin = MCH_MAX ;
        }
    } else if (strcmp(acArg[liOptArgCnt], "-x") == 0) {
        liOptArgCnt++;
        if (aiArgCnt > liOptArgCnt) {
          liMchMax = atoi(acArg[liOptArgCnt]) ;
          if (liMchMax > MCH_MAX)
              liMchMax = MCH_MAX ;
        }

    } else if (strcmp(acArg[liOptArgCnt], "-l") == 0) {
        liOutTyp = 1 ;
    } else if (strcmp(acArg[liOptArgCnt], "-r") == 0) {
        liOutTyp = 2 ;
    } else if (strcmp(acArg[liOptArgCnt], "-c") == 0) {
      JDebug::stddbg = stdout;

    #if debug
    } else if (strcmp(acArg[liOptArgCnt], "-dhsh") == 0) {
      JDebug::gbDbg[DBGHSH] = true ;
    } else if (strcmp(acArg[liOptArgCnt], "-dahd") == 0) {
      JDebug::gbDbg[DBGAHD] = true ;
    } else if (strcmp(acArg[liOptArgCnt], "-dcmp") == 0) {
      JDebug::gbDbg[DBGCMP] = true ;
    } else if (strcmp(acArg[liOptArgCnt], "-dprg") == 0) {
      JDebug::gbDbg[DBGPRG] = true ;
    } else if (strcmp(acArg[liOptArgCnt], "-dbuf") == 0) {
      JDebug::gbDbg[DBGBUF] = true ;
    } else if (strcmp(acArg[liOptArgCnt], "-dhsk") == 0) {
      JDebug::gbDbg[DBGHSK] = true ;
    } else if (strcmp(acArg[liOptArgCnt], "-dahh") == 0) {
      JDebug::gbDbg[DBGAHH] = true ;
    } else if (strcmp(acArg[liOptArgCnt], "-dbkt") == 0) {
      JDebug::gbDbg[DBGBKT] = true ;
    } else if (strcmp(acArg[liOptArgCnt], "-dred") == 0) {
      JDebug::gbDbg[DBGRED] = true ;
    } else if (strcmp(acArg[liOptArgCnt], "-dmch") == 0) {
      JDebug::gbDbg[DBGMCH] = true ;
    } else if (strcmp(acArg[liOptArgCnt], "-ddst") == 0) {
      JDebug::gbDbg[DBGDST] = true ;
    #endif

    } else {
      lbOptArgDne = true ;
      liOptArgCnt-- ;
    }
  } */

  /* Output greetings */
  if ((liVerbse>0) || (liHlp > 0 ) || (aiArgCnt - liOptArgCnt < 3)) {
    fprintf(JDebug::stddbg, "\nJDIFF - binary diff version " JDIFF_VERSION "\n") ;
    fprintf(JDebug::stddbg, JDIFF_COPYRIGHT "\n");
    fprintf(JDebug::stddbg, "\n") ;
    fprintf(JDebug::stddbg, "JojoDiff is free software: you can redistribute it and/or modify\n");
    fprintf(JDebug::stddbg, "it under the terms of the GNU General Public License as published by\n");
    fprintf(JDebug::stddbg, "the Free Software Foundation, either version 3 of the License, or\n");
    fprintf(JDebug::stddbg, "(at your option) any later version.\n");
    fprintf(JDebug::stddbg, "\n");
    fprintf(JDebug::stddbg, "This program is distributed in the hope that it will be useful,\n");
    fprintf(JDebug::stddbg, "but WITHOUT ANY WARRANTY; without even the implied warranty of\n");
    fprintf(JDebug::stddbg, "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n");
    fprintf(JDebug::stddbg, "GNU General Public License for more details.\n");
    fprintf(JDebug::stddbg, "\n");
    fprintf(JDebug::stddbg, "You should have received a copy of the GNU General Public License\n");
    fprintf(JDebug::stddbg, "along with this program.  If not, see <http://www.gnu.org/licenses/>.\n");

    off_t maxoff_t_gb = (MAX_OFF_T >> 30) + 1 ;
    const char *maxoff_t_mul = "GB";
    if (maxoff_t_gb > 1024){
    	maxoff_t_gb = maxoff_t_gb >> 10 ;
    	maxoff_t_mul = "TB";
    }
    fprintf(JDebug::stddbg, "File adressing is %d bit (files up to %" PRIzd " %s), samples are %d bytes.\n\n",
        (int) (sizeof(off_t) * 8), maxoff_t_gb, maxoff_t_mul, SMPSZE) ;
  }

  if ((aiArgCnt - liOptArgCnt < 3) || (liHlp > 0) || (liVerbse>2)) {
    // ruler:                0---------1---------2---------3---------4---------5---------6---------7---------8
    fprintf(JDebug::stddbg, "\n");
    fprintf(JDebug::stddbg, "Usage: jdiff [options] <source file> <destination file> [<output file>]\n") ;
    fprintf(JDebug::stddbg, "  -v --verbose             Verbose: greeting, results and tips.\n");
    fprintf(JDebug::stddbg, "  -vv                      Extra Verbose: progress info and statistics.\n");
    fprintf(JDebug::stddbg, "  -vvv                     Ultra Verbose: all info, including help and details.\n");
    fprintf(JDebug::stddbg, "  -h --help                Help (this text) and exit.\n");
    fprintf(JDebug::stddbg, "  -hh                      Additional help (performance options).\n");
    fprintf(JDebug::stddbg, "  -l --listing             Detailed human readable output.\n");
    fprintf(JDebug::stddbg, "  -r --regions             Grouped  human readable output.\n");
    fprintf(JDebug::stddbg, "  -c --console             Write verbose and debug info to stdout.\n");
    fprintf(JDebug::stddbg, "  -b --better              Be better: use more memory and probably slower.\n");
    fprintf(JDebug::stddbg, "  -f --faster              Be faster: avoid non-buffered searching.\n");
    fprintf(JDebug::stddbg, "  -ff                      Be faster: also drop indexing scan.\n");
    fprintf(JDebug::stddbg, "  -p --sequential-source   Sequential source file (to avoid).\n");
    fprintf(JDebug::stddbg, "  -q --sequential-dest     Sequential destination file.\n");
    fprintf(JDebug::stddbg, "  -m --buffer-size <size>  Size (in kB) for search buffer (0=no buffering)\n");
    fprintf(JDebug::stddbg, "  -k --block-size  <size>  Block size in bytes for reading (default 8192).\n");
    fprintf(JDebug::stddbg, "  -i --index-size  <size>  Index table in mega-words (default 8,max.2048).\n");
    fprintf(JDebug::stddbg, "                           Rounded down to a power of 2: 2,4,8,16,32,... .\n");
    fprintf(JDebug::stddbg, "  -a --search-size <size>  Size (in kB) to search (default=buffer-size).\n");
    fprintf(JDebug::stddbg, "  -n --search-min <count>  Minimum number of solutions to search (default %d).\n", liMchMin);
    fprintf(JDebug::stddbg, "  -x --search-max <count>  Maximum number of solutions to search (default %d).\n", liMchMax);
    fprintf(JDebug::stddbg, "Principles:\n");
    fprintf(JDebug::stddbg, "  JDIFF searches equal regions between two binary files using a heuristic\n");
    fprintf(JDebug::stddbg, "  hash-index algorithm to find a smallest-as-possible set of differences.\n");
    fprintf(JDebug::stddbg, "Notes:\n");
    fprintf(JDebug::stddbg, "  Options -b, -f or -ff should be used before other options.\n");
    fprintf(JDebug::stddbg, "  Accuracy may be improved by increasing the index table size (-i).\n");
    fprintf(JDebug::stddbg, "  Index table size is always lowered to the largest prime below a power of 2.\n");
    fprintf(JDebug::stddbg, "  Source and destination files must be random access files.\n");
    fprintf(JDebug::stddbg, "  Output is sent to standard output if output file is missing.\n");
    fprintf(JDebug::stddbg, "Hint:\n");
    fprintf(JDebug::stddbg, "  Do not use jdiff directly on compressed files (zip, gzip, rar, 7z, ...)\n");
    fprintf(JDebug::stddbg, "  Instead use uncompressed files (cpio, tar, zip-0, ...) and then recompress\n");
    fprintf(JDebug::stddbg, "  after using jdiff.\n");
    if ((liHlp>1) || (liVerbse>2)){
        // ruler:                0---------1---------2---------3---------4---------5---------6---------7---------8
        fprintf(JDebug::stddbg, "Additional help: rationale of the -i, -m, -n -x, -b, -f and other options.\n");
        fprintf(JDebug::stddbg, "  JDiff starts by comparing source and destination files.\n");
        fprintf(JDebug::stddbg, "  \n");
        fprintf(JDebug::stddbg, "  When a difference is found, JDiff will first index the source file.\n");
        fprintf(JDebug::stddbg, "  Under normal operation, the full source file is indexed, but this can be\n");
        fprintf(JDebug::stddbg, "  disabled with the -ff option (faster, but a big loss of accuracy). Indexing\n");
        fprintf(JDebug::stddbg, "  will then be done during searching (so only small differences will be found).\n");
        fprintf(JDebug::stddbg, "  \n");
        fprintf(JDebug::stddbg, "  Next, JDiff will search for ""solutions"": equal regions between both files.\n");
        fprintf(JDebug::stddbg, "  The search will use the index table (a hash-table).\n");
        fprintf(JDebug::stddbg, "  However, the index table is not perfect: too small and inaccurate:\n");
        fprintf(JDebug::stddbg, "  - too small, because a full index would require too much memory.\n");
        fprintf(JDebug::stddbg, "  - inaccurate, because the hash-keys are only 32 or 64 bit checksums.\n");
        fprintf(JDebug::stddbg, "  That's why a bigger index (-i) improves accuracy (and often also speed).\n");
        fprintf(JDebug::stddbg, "  \n");
        fprintf(JDebug::stddbg, "  Also, a ""match"" from the index table index is verified to improve accuracy:\n");
        fprintf(JDebug::stddbg, "  - by comparing the matched regions.\n");
        fprintf(JDebug::stddbg, "  - by confirmation from colliding matches further on.\n");
        fprintf(JDebug::stddbg, "  \n");
        fprintf(JDebug::stddbg, "  Confirmations however will never fully guarantee a correct match.\n");
        fprintf(JDebug::stddbg, "  Comparing however is slow when data is not bufferred (must be read from disk).\n");
        fprintf(JDebug::stddbg, "  With -f/-ff options, JDiff will not compare unbuffered data (faster).\n");
        fprintf(JDebug::stddbg, "  \n");
        fprintf(JDebug::stddbg, "  Moreover, the first solution is not always the best solution.\n");
        fprintf(JDebug::stddbg, "  Therefore, JDiff will search a minimum (-n) number of solutions, and\n");
        fprintf(JDebug::stddbg, "  will continue up to a maximum (-x) number of solutions if data is buffered.\n");
        fprintf(JDebug::stddbg, "  That's why, besides speed, bigger buffers (-m) may also improve accuracy.\n");
        fprintf(JDebug::stddbg, "  \n");
        fprintf(JDebug::stddbg, "  With the -p and -ff options, JDiff can only index the source file within the\n");
        fprintf(JDebug::stddbg, "  buffer, so accuracy will be much reduced (higher -m and -b may re-improve).\n");
        fprintf(JDebug::stddbg, "  \n");
        fprintf(JDebug::stddbg, "  Option -b increases the index table and buffers (more speed and accuracy),\n");
        fprintf(JDebug::stddbg, "  and also the number of solutions to search (slower but better).\n");
    }
    if ((aiArgCnt - liOptArgCnt < 3) || (liHlp > 0))
        exit(EXI_ARG);
  } else if (liVerbse > 0) {
    fprintf(JDebug::stddbg, "\nUse -h for additional help and usage description.\n");
  }

  /* Read filenames */
  lcFilNamOrg = acArg[1 + liOptArgCnt];
  lcFilNamNew = acArg[2 + liOptArgCnt];
  if (aiArgCnt - liOptArgCnt >= 4)
    lcFilNamOut = acArg[3 + liOptArgCnt];
  else
    lcFilNamOut = "-" ;

  /* MinGW does not correctly handle files > 2GB using fstream.gseek */
#ifdef __MINGW32__
  if (llBufSze == 0){
      llBufSze = liBlkSze ;
  }
#endif

  JFile *lpFilOrg = NULL ;
  JFile *lpFilNew = NULL ;

  FILE *lfFilOrg = NULL ;
  FILE *lfFilNew = NULL ;

#ifndef __MINGW32__
  ifstream *liFilOrg = NULL ;
  ifstream *liFilNew = NULL ;
#endif

  /* Open first file */
#ifdef __MINGW32__
  lfFilOrg = jfopen(lcFilNamOrg, "rb") ;
  if (lfFilOrg != NULL){
      lpFilOrg = new JFileAhead(lfFilOrg, "Org", llBufSze, liBlkSze);
  }
#else
  liFilOrg = new ifstream();
  liFilOrg->open(lcFilNamOrg, ios_base::in | ios_base::binary) ;
  if (liFilOrg->is_open()){
	  if (llBufSze > 0){
		  lpFilOrg = new JFileIStreamAhead(liFilOrg, "Org",  llBufSze, liBlkSze);
	  } else {
		  lpFilOrg = new JFileIStream(liFilOrg, "Org");
	  }
  }
#endif
  if (lpFilOrg == NULL){
      fprintf(JDebug::stddbg, "Could not open first file %s for reading.\n", lcFilNamOrg);
      exit(EXI_FRT);
  }

  /* Open second file */
#ifdef __MINGW32__
  lfFilNew = jfopen(lcFilNamNew, "rb") ;
  if (lfFilNew != NULL){
      lpFilNew = new JFileAhead(lfFilNew, "New", llBufSze, liBlkSze);
  }
#else
  liFilNew = new ifstream();
  liFilNew->open(lcFilNamNew, ios_base::in | ios_base::binary) ;
  if (liFilNew->is_open()){
	  if (llBufSze > 0){
		  lpFilNew = new JFileIStreamAhead(liFilNew, "New",  llBufSze, liBlkSze);
	  } else {
		  lpFilNew = new JFileIStream(liFilNew, "New");
	  }
  }
#endif
  if (lpFilNew == NULL){
      fprintf(JDebug::stddbg, "Could not open second file %s for reading.\n", lcFilNamNew);
      exit(EXI_SCD);
  }

  /* Open output */
  if (strcmp(lcFilNamOut,"-") == 0 )
      lpFilOut = stdout ;
  else
      lpFilOut = fopen(lcFilNamOut, "wb") ;
  if ( lpFilOut == null ) {
    fprintf(JDebug::stddbg, "Could not open output file %s for writing.\n", lcFilNamOut) ;
    exit(EXI_OUT);
  }

  /* Init output */
  JOut *lpOut ;
  switch (liOutTyp){
  case 0:
      lpOut = new JOutBin(lpFilOut);
      break;
  case 1:
      lpOut = new JOutAsc(lpFilOut);
      break;
  case 2:
  default:  // XXX get rid of uninitialized warning
      lpOut = new JOutRgn(lpFilOut);
      break;
  }

  /* Initialize JDiff object */
  JDiff loJDiff(lpFilOrg, lpFilNew, lpOut,
      liHshMbt * 1024 * 1024, liVerbse,
      lbSrcBkt, liSrcScn, liMchMax, liMchMin, liAhdMax==0?llBufSze:liAhdMax, lbCmpAll);
  if (liVerbse>1) {
      fprintf(JDebug::stddbg, "Lookahead buffers: %lu kb. (%lu kb. per file).\n",llBufSze * 2 / 1024, llBufSze / 1024) ;
      fprintf(JDebug::stddbg, "Hastable size    : %d kb. (%d samples).\n", (loJDiff.getHsh()->get_hashsize() + 512) / 1024, loJDiff.getHsh()->get_hashprime()) ;
  }

  /* Show final execution parameters */
  if (liVerbse>1){
      fprintf(JDebug::stddbg, "\n");
      fprintf(JDebug::stddbg, "Min number of matches to search  (-n): %d\n", liMchMin);
      fprintf(JDebug::stddbg, "Max number of matches to search  (-x): %d\n", liMchMax);
      fprintf(JDebug::stddbg, "Max number of matches to search  (-x): %d\n", liMchMax);
      fprintf(JDebug::stddbg, "Hashtable size    (default: 8Mb) (-s): %dMw, %dMb (%d samples)\n",
              liHshMbt,
              ((loJDiff.getHsh()->get_hashsize() + 512) / 1024 + 512) / 1024,
              loJDiff.getHsh()->get_hashprime()) ;
      fprintf(JDebug::stddbg, "Filebuffers size   (default 2Mb) (-m): %ldMb\n", llBufSze * 2 / 1024 / 1024);
      fprintf(JDebug::stddbg, "Block size         (default 8kb) (-b): %dkb\n",  liBlkSze / 1024);
      fprintf(JDebug::stddbg, "Search ahead max, 0 = buffersize (-a): %dkb\n",  liAhdMax * 2 / 1024 );
      fprintf(JDebug::stddbg, "Backtrace allowed     (-0 to disable): %s\n",    lbSrcBkt?"yes":"no");
      fprintf(JDebug::stddbg, "Compare out-of-buffer (-f to disable): %s\n",    lbCmpAll?"yes":"no");
      fprintf(JDebug::stddbg, "Full indexing scan   (-ff to disbale): %s\n",   (liSrcScn>0)?"yes":"no");
  }

  /* Execute... */
  int liRet = loJDiff.jdiff();

  /* Write statistics */
  if (liVerbse > 1) {
      fprintf(JDebug::stddbg, "\nHashtable size          = %d bytes, %d KB, %d MB\n",
              loJDiff.getHsh()->get_hashsize(),
              (loJDiff.getHsh()->get_hashsize() + 512) / 1024,
              ((loJDiff.getHsh()->get_hashsize() + 512) / 1024 + 512) / 1024) ;
      fprintf(JDebug::stddbg, "Hashtable prime         = %d\n",   loJDiff.getHsh()->get_hashprime()) ;
      fprintf(JDebug::stddbg, "Hashtable hits          = %d\n",   loJDiff.getHsh()->get_hashhits()) ;
      fprintf(JDebug::stddbg, "Hashtable errors        = %d\n",   loJDiff.getHshErr()) ;
      fprintf(JDebug::stddbg, "Hashtable repairs       = %d\n",   JMatchTable::siHshRpr) ;
      fprintf(JDebug::stddbg, "Hashtable overloading   = %d\n",   loJDiff.getHsh()->get_hashcolmax() / 3 - 1);
      fprintf(JDebug::stddbg, "Reliability distance    = %d\n",   loJDiff.getHsh()->get_reliability());
      fprintf(JDebug::stddbg, "Random    accesses      = %ld\n",  lpFilOrg->seekcount() + lpFilNew->seekcount());
      fprintf(JDebug::stddbg, "Delete    bytes         = %" PRIzd "\n", lpOut->gzOutBytDel);
      fprintf(JDebug::stddbg, "Backtrack bytes         = %" PRIzd "\n", lpOut->gzOutBytBkt);
      fprintf(JDebug::stddbg, "Escape    bytes written = %" PRIzd "\n", lpOut->gzOutBytEsc);
      fprintf(JDebug::stddbg, "Control   bytes written = %" PRIzd "\n", lpOut->gzOutBytCtl);
  }
  if (liVerbse > 0) {
      fprintf(JDebug::stddbg, "Equal     bytes         = %" PRIzd "\n", lpOut->gzOutBytEql);
      fprintf(JDebug::stddbg, "Data      bytes written = %" PRIzd "\n", lpOut->gzOutBytDta);
      fprintf(JDebug::stddbg, "Overhead  bytes written = %" PRIzd "\n", lpOut->gzOutBytCtl + lpOut->gzOutBytEsc);
  }

  /* Cleanup */
  delete lpFilOrg;
  delete lpFilNew;
#ifndef __MINGW32__
  if (liFilOrg != NULL) {
	  liFilOrg->close();
	  delete liFilOrg ;
  }
  if (liFilNew != NULL) {
	  liFilNew->close();
	  delete liFilNew ;
  }
#endif
  if (lfFilOrg != NULL) jfclose(lfFilOrg);
  if (lfFilNew != NULL) jfclose(lfFilNew);


  /* Exit */
  switch (liRet){
  case - EXI_SEK:
      fprintf(JDebug::stddbg, "Seek error !");
      exit (EXI_SEK);
  case - EXI_LRG:
      fprintf(JDebug::stddbg, "64-bit offsets not supported !");
      exit (EXI_LRG);
  case - EXI_RED:
      fprintf(JDebug::stddbg, "Error reading file !");
      exit (EXI_RED);
  case - EXI_WRI:
      fprintf(JDebug::stddbg, "Error writing file !");
      exit (EXI_WRI);
  case - EXI_MEM:
      fprintf(JDebug::stddbg, "Error allocating memory !");
      exit (EXI_MEM);
  case - EXI_ERR:
      fprintf(JDebug::stddbg, "Other error occured !");
      exit (EXI_ERR);
  }

  if (lpOut->gzOutBytDta == 0 && lpOut->gzOutBytDel == 0)
      return(1);    /* no differences found */
  else
      return(0);    /* differences found    */
}
