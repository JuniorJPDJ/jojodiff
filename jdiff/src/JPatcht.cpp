/*
 * JPatcht.cpp
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

#include <stdlib.h>
#include <new>
#include "JPatcht.h"
#include "JDebug.h"
#include "JDefs.h"

using namespace std;

namespace JojoDiff {

JPatcht::JPatcht(JFile &apFilOrg, JFile &apFilPch, JFileOut &apFilOut,
                 const int aiVerbse)
: mpFilOrg(apFilOrg), mpFilPch(apFilPch), mpFilOut(apFilOut)
, miVerbse(aiVerbse)
{

}

JPatcht::~JPatcht()
{
    //dtor
}

/** @brief Get an offset from the input file
*
* @param  lpFil  input file
* @return offset
*/
off_t JPatcht::ufGetInt( JFile &apFil){
  off_t liVal ;

  liVal = apFil.get() ;
  if (liVal < 252)
    return liVal + 1 ;
  else if (liVal == 252)
    return 253 + apFil.get() ;
  else if (liVal == 253) {
    liVal = apFil.get() ;
    liVal = (liVal << 8) + apFil.get() ;
    return liVal ;
  }
  else if (liVal == 254) {
    liVal = apFil.get() ;
    liVal = (liVal << 8) + apFil.get() ;
    liVal = (liVal << 8) + apFil.get() ;
    liVal = (liVal << 8) + apFil.get() ;
    return liVal ;
  } else {
#ifdef JDIFF_LARGEFILE
    liVal = apFil.get() ;
    liVal = (liVal << 8) + apFil.get() ;
    liVal = (liVal << 8) + apFil.get() ;
    liVal = (liVal << 8) + apFil.get() ;
    liVal = (liVal << 8) + apFil.get() ;
    liVal = (liVal << 8) + apFil.get() ;
    liVal = (liVal << 8) + apFil.get() ;
    liVal = (liVal << 8) + apFil.get() ;
    return liVal ;
#else
    fprintf(stderr, "64-bit length numbers not supported!\n");
    exit(EXI_LRG);
#endif
  }
}

/** @brief Put one byte of output data
*
* @param    azPosOrg    position on source file
* @param    azPosOut    position on output file
* @param    aiOpr       MOD or INS
* @param    aiOut       output byte
* @param    aiOff       offset
* @return   1
*/
int JPatcht::ufPutDta( off_t const lzPosOrg, off_t const lzPosOut,
                       int liOpr, int const aiDta, off_t azOff )
{
    mpFilOut.putc(aiDta) ;
    if (miVerbse > 2) {
        fprintf(JDebug::stddbg, P8zd " " P8zd " %s %3o %c\n",
                  lzPosOrg-1 + ((liOpr == MOD) ? azOff : 0),
                  lzPosOut-1,
                  (liOpr == MOD) ? "MOD" : "INS",
                  aiDta,
                  ((aiDta >= 32 && aiDta <= 127)?(char) aiDta:' '))  ;
    }
    return 1 ;
}

/** @brief Read a data sequence (INS or MOD)
*
* @param    azPosOrg    position on source file
* @param    azPosOut    position on output file
* @param    aiOpr       INS or MOD
* @param    azMod       out: offset counter
* @param    aiPnd       First pending byte (EOF = no pending byte)
* @param    aiDbl       Second pending byte (EOF = no pending byte)
*/
int JPatcht::ufGetDta(off_t const lzPosOrg, off_t const lzPosOut,
                      int const liOpr, off_t &lzMod, int liPnd, int liDbl )
{
    int liInp ;         /* Input from mpFilPch                  */
    int liNew;          /* New operator                         */

    lzMod = 0 ;

    /* First, output the pending bytes:
       liPnd  liDbl     Output
        xxx    <na>     xxx
        ESC    xxx      ESC xxx
        ESC    ESC      ESC ESC
    */
    if (liPnd != EOF) {
        lzMod += ufPutDta(lzPosOrg, lzPosOut, liOpr, liPnd, liDbl==EOF ? -1 : -2) ;
        if (liPnd == ESC && liDbl != ESC) {
          lzMod += ufPutDta(lzPosOrg, lzPosOut, liOpr, liDbl, -1) ;
        }
    }

    /* Read loop */
    while ((liInp = mpFilPch.get()) != EOF) {
        // Handle ESC-code
        if (liInp == ESC) {
            liNew = mpFilPch.get();
            switch (liNew) {
                case DEL:
                case EQL:
                case BKT:
                case MOD:
                case INS:
                    break ;
                case ESC:
                    // Double ESC: drop one
                    if (miVerbse > 1) {
                      fprintf(JDebug::stddbg, "" P8zd " " P8zd " ESC ESC\n",
                              lzPosOrg+lzMod, lzPosOut) ;
                    }

                    // Write the single ESC and continue
                    lzMod += ufPutDta(lzPosOrg, lzPosOut, liOpr, liInp, lzMod) ;
                    continue ;

                default:
                  // ESC <xxx> with <xxx> not an opcode: output as they are
                  if (miVerbse > 1) {
                    fprintf(JDebug::stddbg, "" P8zd " " P8zd " ESC XXX\n",
                            lzPosOrg+lzMod, lzPosOut) ;
                  }

                  // Write the escape, the <xxx> and continue
                  lzMod += ufPutDta(lzPosOrg, lzPosOut, liOpr, liInp, lzMod) ;
                  lzMod += ufPutDta(lzPosOrg, lzPosOut, liOpr, liNew, lzMod) ;
                  continue;
            }
            if (liNew == liOpr) {
                // <ESC> MOD within an <ESC> MOD is meaningless: handle as data
                // <ESC> INS within an <ESC> INS is meaningless: handle as data
                lzMod += ufPutDta(lzPosOrg, lzPosOut, liOpr, ESC, lzMod) ;
                if (miVerbse > 1) {
                    fprintf(JDebug::stddbg, P8zd" " P8zd " MOD %3o ESC\n",
                            lzPosOrg+lzMod-1, lzPosOut-1, ESC)  ;
                }
                liInp=liNew ;   // will be output below
            } else {
                return liNew ;
            }
        }

        // Handle data
        lzMod += ufPutDta(lzPosOrg, lzPosOut, liOpr, liInp, lzMod) ;
    } /* while ! EOF */

    return EOF ; // we
} /* ufGetDta */

/*******************************************************************************
* Patch function
*******************************************************************************
* Input stream consists of a series of
*   <op> (<data> || <len>)
* where
*   <op>   = <ESC> (<MOD>||<INS>||<DEL>||<EQL>)
*   <data> = <chr>||<ESC><ESC>
*   <chr>  = any byte different from <ESC><MOD><INS><DEL> or <EQL>
*   <ESC><ESC> yields one <ESC> byte
*******************************************************************************/
    int JPatcht::jpatch ()
    {
        int liInp ;         /* Current input from patch file          */
        int liDbl = EOF ;   /* Pending byte (EOF = no pending bye)    */
        int liOpr ;         /* Current operand                        */
        int liRet;          /* Return code from output file           */

        off_t lzOff ;       /* Current operand's offset               */
        off_t lzPosOrg=0;   /* Position in source file                */
        off_t lzPosOut=0;   /* Position in destination file           */

        liOpr = 0 ;   // no operator
        while (liOpr != EOF) {
            // Read operand from input
            if (liOpr == 0) {
                liInp = mpFilPch.get();
                if (liInp == EOF)
                    break ;

                // Handle ESC <opr>
                if (liInp == ESC) {
                    liDbl = mpFilPch.get();
                    switch (liDbl) {
                    case EQL:
                    case DEL:
                    case BKT:
                    case MOD:
                    case INS:
                        liOpr=liDbl;
                        liDbl=EOF;
                        liInp=EOF;
                        break ; // new operator found, all ok !
                    case EOF:
                        // serious error, let's call this a trailing byte
                        fprintf(stderr, "Warning: unexpected trailing byte at end of file, patch file may be corrupted.\n") ;
                        return (EXI_ERR);
                        break ;
                    default:
                        // hmmm, an ESC xxx or ESC ESC : this is awkward!
                        // try to resolve by having two pending bytes: liInp and liDbl
                        if (liOpr > 0) liOpr=0;
                        break ;
                    }
                } else {
                    liOpr = 0;    // it's not an operator sequence (gaining two bytes)
                }

                // If an ESC <opr> is missing, set default operator based on previous operator
                if (liOpr == 0 || liInp != EOF) {
                    liOpr=MOD ; // the default operator
                }
            } else {
                liInp = EOF ;
            }

            // Execute the new operator
            switch(liOpr) {
            case MOD:
                liOpr = ufGetDta(lzPosOrg, lzPosOut, liOpr, lzOff, liInp, liDbl) ;
                if (miVerbse == 1) {
                    fprintf(JDebug::stddbg, "" P8zd " " P8zd " MOD %" PRIzd "\n",
                            lzPosOrg, lzPosOut, lzOff) ;
                }
                lzPosOrg += lzOff ;
                lzPosOut += lzOff ;
                break ;

            case INS:
                liOpr = ufGetDta(lzPosOrg, lzPosOut, liOpr, lzOff, liInp, liDbl) ;
                if (miVerbse == 1) {
                    fprintf(JDebug::stddbg, "" P8zd " " P8zd " INS %" PRIzd "\n",
                            lzPosOrg, lzPosOut, lzOff)   ;
                }
                lzPosOut += lzOff ;
                break ;

            case DEL:
                lzOff = ufGetInt(mpFilPch);
                if (miVerbse >= 1) {
                    fprintf(JDebug::stddbg, "" P8zd " " P8zd " DEL %" PRIzd "\n",
                            lzPosOrg, lzPosOut, lzOff)  ;
                }
                lzPosOrg += lzOff ;
                liOpr = 0;    // to read next operator from input
                break ;

            case EQL:
                /* get length of operation */
                lzOff = ufGetInt(mpFilPch);

                /* show feedback */
                if (miVerbse >= 1) {
                    fprintf(JDebug::stddbg, "" P8zd " " P8zd " EQL %" PRIzd "\n",
                            lzPosOrg, lzPosOut, lzOff) ;
                }

                /* execute operation */
                liRet = mpFilOut.copyfrom(mpFilOrg, lzPosOrg, lzOff) ;
                if (liRet != EXI_OK){
                    return liRet ;
                }
                lzPosOrg += lzOff ;
                lzPosOut += lzOff ;

                /* Next operator */
                liOpr = 0;  // to read next operator from input
                break ;

            case BKT:
                lzOff = ufGetInt(mpFilPch) ;
                if (miVerbse >= 1) {
                    fprintf(JDebug::stddbg, "" P8zd " " P8zd " BKT %" PRIzd "\n",
                            lzPosOrg, lzPosOut, lzOff)   ;
                }
                lzPosOrg -= lzOff ;
                liOpr = 0;  // to read next operator from input
                break ;
            }
        } /* while ! EOF */

        if (miVerbse >= 1) {
            fprintf(JDebug::stddbg, P8zd" " P8zd " EOF\n",
                    lzPosOrg, lzPosOut)  ;
        }

        return 1 ;
    } /* jpatch */

} /* namespace */
