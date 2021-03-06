/*
 * JOutBin.cpp
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

#include "JDefs.h"
#include "JOutBin.h"

namespace JojoDiff {

JOutBin::JOutBin(FILE *apFilOut ) : mpFilOut(apFilOut), miOprCur(MOD), mzEqlCnt(0), mbOutEsc(false) {
}

JOutBin::~JOutBin() {
}

/*******************************************************************************
* Output functions
*
* The output has following format
*   <esc> <opcode> [<length>|<data>]
* where
*   <esc>    =   ESC
*   <opcode> =   MOD | INS | DEL | EQL | BKT
*   <data>   :   A series of data bytes.
*        The series is ended with a new "<esc> <opcode>" sequence.
*        If an "<esc> <opcode>" sequence occurs within the data, it is
*        prefixed with an additional <esc>.
*        E.g.: <ESC><MOD>This data contains an <ESC><ESC><EQL> sequence.
*   <length> :   1 to 5 bytes for specifying a 32-bit unsigned number.
*              1 <= x < 252        1 byte:   0-251
*            252 <= x < 508        2 bytes:  252, x-252
*            508 <= x < 0x10000    3 bytes:  253, xx
*        0x10000 <= x < 0x100000000        5 bytes:  254, xxxx
*                          9 bytes:  255, xxxxxxxx
*
*******************************************************************************/

/* ---------------------------------------------------------------
 * ufPutLen outputs a length as follows
 * byte1  following      formula              if number is
 * -----  ---------      -------------------- --------------------
 * 0-251                 1-252                between 1 and 252
 * 252    x              253 + x              between 253 and 508
 * 253    xx             253 + 256 + xx       a 16-bit number
 * 254    xxxx           253 + 256 + xxxx     a 32-bit number
 * 255    xxxxxxxx       253 + 256 + xxxxxxxx a 64-bit number
 * ---------------------------------------------------------------*/
void JOutBin::ufPutLen ( off_t azLen  )
{ if (azLen <= 252) {
    putc(azLen - 1, mpFilOut) ;
    gzOutBytCtl += 1;
  } else if (azLen <= 508) {
    putc(252, mpFilOut);
    putc((azLen - 253), mpFilOut) ;
    gzOutBytCtl += 2;
  } else if (azLen <= 0xffff) {
    putc(253, mpFilOut);
    putc((azLen >>  8)       , mpFilOut) ;
    putc((azLen      ) & 0xff, mpFilOut) ;
    gzOutBytCtl += 3;
#ifdef JDIFF_LARGEFILE
  } else if (azLen <= 0xffffffff) {
#else
  } else {
#endif
    putc(254, mpFilOut);
    putc((azLen >> 24)       , mpFilOut);
    putc((azLen >> 16) & 0xff, mpFilOut) ;
    putc((azLen >>  8) & 0xff, mpFilOut) ;
    putc((azLen      ) & 0xff, mpFilOut) ;
    gzOutBytCtl += 5;
  }
#ifdef JDIFF_LARGEFILE
  else {
    putc(255, mpFilOut);
    putc((azLen >> 56)       , mpFilOut) ;
    putc((azLen >> 48) & 0xff, mpFilOut) ;
    putc((azLen >> 40) & 0xff, mpFilOut) ;
    putc((azLen >> 32) & 0xff, mpFilOut) ;
    putc((azLen >> 24) & 0xff, mpFilOut);
    putc((azLen >> 16) & 0xff, mpFilOut) ;
    putc((azLen >>  8) & 0xff, mpFilOut) ;
    putc((azLen      ) & 0xff, mpFilOut) ;
    gzOutBytCtl += 9;
  }
#endif
} /* ufPutLen */

/* ---------------------------------------------------------------
 * ufPutOpr outputs a new opcode and closes the previous
 * data stream.
 * ---------------------------------------------------------------*/
void JOutBin::ufPutOpr ( int aiOpr )
{   // first output a pending escape
    // as a real escape will follow, the data escape must be protected
    if (mbOutEsc) {
        putc(ESC, mpFilOut) ;
        putc(ESC, mpFilOut) ;
        mbOutEsc = false ;
        gzOutBytEsc++ ;
        gzOutBytDta++ ;
    }

    if ( aiOpr != ESC ) {
        // No need to output a MOD after an EQL, BKT or DEL
        if ( aiOpr != MOD || miOprCur == INS ) {
            putc(ESC, mpFilOut);
            putc(aiOpr, mpFilOut);
            gzOutBytCtl+=2;
        }
    }
    miOprCur = aiOpr ;
}

/* ---------------------------------------------------------------
 * ufPutByt outputs a byte, prefixing a data sequence <esc> <opcode>
 * with an addition <esc> byte.
 * ---------------------------------------------------------------*/
void JOutBin::ufPutByt ( int aiByt )
{
  // handle a pending escape data byte
  if (mbOutEsc) {
    mbOutEsc = false;
    if (aiByt >= BKT && aiByt <= ESC) {
      // an <es><opcode> sequence within the datastrem,
      // is protected by an additional <esc>
      putc(ESC, mpFilOut) ;
      gzOutBytEsc++ ;
    }
    // write the pending escape
    putc(ESC, mpFilOut) ;
    gzOutBytDta++;
  }
  // output the incoming byte
  if (aiByt == ESC) {
    // do not output now, wait to see what follows
    mbOutEsc = true ;
  } else {
    // output byte
    putc(aiByt, mpFilOut) ;
    gzOutBytDta++;
  }
}

/* ---------------------------------------------------------------
 * ufOutBytBin: binary output function for generating patch files
 * ---------------------------------------------------------------*/
bool JOutBin::put (
  int   aiOpr,
  off_t azLen,
  int   aiOrg,
  int   aiNew,
  off_t azPosOrg,
  off_t azPosNew
)
{ /* Output a pending EQL operand (if MINEQL or more equal bytes) */
  if (aiOpr != EQL && mzEqlCnt > 0) {
    if (mzEqlCnt > MINEQL || (miOprCur != MOD && aiOpr != MOD)) {
      // as of 3 equal bytes => output as EQL (ESC EQL <cnt>)
      ufPutOpr(EQL) ;
      ufPutLen(mzEqlCnt);

      gzOutBytEql+=mzEqlCnt;
    } else {
      // less than 3 equal bytes => output as MOD
      if (miOprCur != MOD) {
        ufPutOpr(MOD) ;
      }
      for (int liCnt=0; liCnt < mzEqlCnt; liCnt++)
        ufPutByt(miEqlBuf[liCnt]) ;
    }
    mzEqlCnt=0;
  }

  /* Handle current operand */
  switch (aiOpr) {
    case ESC : /* before closing the output */
      ufPutOpr(ESC);
      break;

    case MOD :
    case INS :
      if (miOprCur != aiOpr) {
        ufPutOpr(aiOpr) ;
      }
      ufPutByt(aiNew) ;
      break;

    case DEL :
      ufPutOpr(DEL) ;
      ufPutLen(azLen);

      gzOutBytDel+=azLen;
      break;

    case BKT :
      ufPutOpr(BKT) ;
      ufPutLen(azLen);

      gzOutBytBkt+=azLen;
      break;

    case EQL :
      if (mzEqlCnt < MINEQL) {
          miEqlBuf[mzEqlCnt++] = aiOrg ;
          return (mzEqlCnt >= MINEQL) ;
      } else {
          mzEqlCnt+=azLen ;
          return true ;
      }
      break;
  }

  return false ;
} /* put() */
} /* namespace */
