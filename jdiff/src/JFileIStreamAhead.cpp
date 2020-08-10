/*
 * JFileIStreamAhead.cpp
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

#include <stdlib.h>
#include <stdio.h>
#include <exception>
#if debug
#include <string.h>
#endif

#include "JFileIStreamAhead.h"
#include "JDebug.h"

namespace JojoDiff {

/**
 * Construct a buffered JFile on an istream.
 */
JFileIStreamAhead::JFileIStreamAhead(istream &apFil, char const * const asFid, const long alBufSze, const int aiBlkSze )
: msFid(asFid),mpStream(apFil), mlBufSze(alBufSze), miBlkSze(aiBlkSze), mlFabSek(0)
{
    mpBuf = (jchar *) malloc(mlBufSze) ;
#ifdef JDIFF_THROW_BAD_ALLOC
    if (mpBuf == null){
        throw bad_alloc() ;
    }
#endif

    mpMax = mpBuf + mlBufSze ;
    mpInp = mpBuf;
    mpRed = mpInp;

    miBufUsd = 0;
    mzPosInp = 0;
    mzPosEof = MAX_OFF_T ;
    mzPosRed = 0 ;
    mzPosBse = 0 ;
    miRedSze = 0 ;

    // Block size cannot be larger than buffer size and cannot be zero
    if (miBlkSze > mlBufSze)
        miBlkSze = mlBufSze ;
    else if (miBlkSze == 0)
        miBlkSze = 1;

#if debug
    if (JDebug::gbDbg[DBGBUF])
        fprintf(JDebug::stddbg, "ufFabOpn(%s):(buf=%p,max=%p,sze=%ld)\n",
                asFid, mpBuf, mpMax, mlBufSze);
#endif
}

JFileIStreamAhead::~JFileIStreamAhead() {
	if (mpBuf != null) free(mpBuf) ;
}

/**
 * Return number of seeks performed.
 */
long JFileIStreamAhead::seekcount() const {
    return mlFabSek;
}

/**
 * For buffered files, return the position of the buffer
 *
 * @return  -1=no buffering, > 0 : first position in buffer
 */
off_t JFileIStreamAhead::getBufPos(){
    return mzPosInp - miBufUsd ;
}

/**
 * @brief Set lookahead base: soft lookahead will fail when reading after base + buffer size
 *
 * Attention: the base will be implicitly changed by get on non-buffered reads too !
 *
 * @param   azBse	base position
 */
void JFileIStreamAhead::set_lookahead_base (
    const off_t azBse	/* new base position */
) {
    mzPosBse = azBse ;
}

/**
 * @brief Get next byte
 *
 * Soft read ahead will return an EOB when date is not available in the buffer.
 *
 * @param   aiSft	soft reading type: 0=read, 1=hard read ahead, 2=soft read ahead
 * @return 			the read character or EOF or EOB.
 */
int JFileIStreamAhead::get (
    const int aiSft   /* 0=read, 1=hard ahead, 2=soft ahead  */
) {
    return get(mzPosRed, aiSft);
}

/**
 * Gets one byte from the lookahead file.
 */
int JFileIStreamAhead::get (
    const off_t &azPos, /* position to read from                */
    const int aiSft     /* 0=read, 1=hard ahead, 2=soft ahead   */
) {
    if ((miRedSze > 0) && (azPos == mzPosRed)) {
        mzPosRed++ ;
        miRedSze--;
        #if debug
//@        if (JDebug::gbDbg[DBGRED])
//          fprintf(JDebug::stddbg, "JFileIStreamAhead(%s,"P8zd",%d)->%c=%2x (mem %p).\n",
//             msFid, azPos, aiSft, *mpRed, *mpRed, mpRed);
        #endif
        return *mpRed++;
    } else {
        return get_frombuffer(azPos, aiSft);
    }
} /* int get(...) */

/**
 * Tries to get data from the buffer. Calls get_outofbuffer if that is not possible.
 * @param azPos     position to read from
 * @param aiSft     0=read, 1=hard ahead, 2=soft ahead
 * @return data at requested position, EOF or EOB.
 */
int JFileIStreamAhead::get_frombuffer (
    const off_t azPos,     /* position to read from                */
    const int aiSft        /* 0=read, 1=hard ahead, 2=soft ahead   */
){
	jchar *lpDta ;
	off_t lzLen ;

	lpDta = getbuf(azPos, lzLen, aiSft) ;
	if (lpDta == null) {
	    // EOF, EOB or any other problem
        mzPosRed = -1;
        mpRed = null;
        miRedSze = 0 ;
        return lzLen ;
	} else {
	    #if debug
	    // double-verify contents of the buffer
        if (JDebug::gbDbg[DBGRED]) {
            // detect buffer logic failure
            int lzDbg ;
            jchar *lpDbg ;
            lzDbg = (mzPosInp - azPos) ;
            lpDbg = (mpInp - lzDbg) ;
            if (lpDbg < mpBuf)
                lpDbg += mlBufSze ;
            if (lpDbg != lpDta){
                fprintf(JDebug::stddbg, "JFileIStreamAhead(%s,"P8zd",%d)->%c=%2x (mem %p): pos-error !\n",
                   msFid, azPos, aiSft, *lpDta, *lpDta, lpDta );
            }

            // detect buffer contents failure
            jchar lcTst[lzLen] ;
            int liDne ;
            int liCmp;
            mpStream.seekg(azPos) ;
            mpStream.read((char *)lcTst, lzLen) ;
            liDne = mpStream.gcount();
            liCmp = memcmp(lpDta, lcTst, lzLen) ;
            if (liCmp != 0) {
                fprintf(JDebug::stddbg, "JFileIStreamAhead(%s,"P8zd",%d)->%c=%2x (mem %p): buf-error !\n",
                   msFid, azPos, aiSft, *lpDta, *lpDta, lpDta );
            }
            mpStream.seekg(mzPosInp);
	    }
	    #endif

        // prepare next reading position (but do not increase lpDta!!!)
        mzPosRed = azPos + 1;
        miRedSze = lzLen - 1;
        mpRed = lpDta + 1;
        if (mpRed == mpMax) {
            mpRed = mpBuf ;
        }

        // return data at current position
        return *lpDta ;
	}
}

/**
 * @brief Get access to buffered read.
 *
 * @param   azPos   in:  position to get access to
 * @param   azLen   out: number of bytes in buffer
 * @param   aiSft   in:  0=read, 1=hard read ahead, 2=soft read ahead
 *
 * @return  buffer, null = azPos not in buffer
 */
jchar * JFileIStreamAhead::getbuf(const off_t azPos, off_t &azLen, const int aiSft) {
	jchar *lpDta ;

	if (azPos >= mzPosEof) {
        /* eof */
        #if debug
        //if (JDebug::gbDbg[DBGRED])
        //fprintf(JDebug::stddbg, "JFileIStreamAhead::getbuf(%s,"P8zd",%d)->EOF (mem).\n",
        //   msFid, azPos, aiSft);
        #endif

        azLen = EOF ;
        return null ;
	} else if (azPos < mzPosInp && azPos >= mzPosInp - miBufUsd){
	    // Data is already in the buffer
        azLen = mzPosInp - azPos ;
        lpDta = mpInp - azLen ;
        if (lpDta < mpBuf ){
            lpDta += mlBufSze ;
            if (mpMax - lpDta != azLen)
                azLen = mpMax - lpDta ;
        }
	} else {
	    // Get data from underlying file
        switch (get_fromfile(azPos, aiSft)) {
        case EndOfBuffer:
            azLen = EOB ;
            return null;
        case EndOfFile:
            azLen = EOF ;
            return null;
        case Added: // data added
            azLen = mzPosInp - azPos ;
            lpDta = mpInp - azLen ;
            break ;
        case Cycled: // buffer cycled
            azLen = mzPosInp - azPos - (mpInp - mpBuf);
            lpDta = mpMax - azLen ;
            //aLen = mpMax - lpDta ;
            break ;
        case Partial:
            // partial read: a second get_fromfile is needed
            // this can only happen when mlBufsze is not a multiple of miBlksze
            switch (get_fromfile(azPos, aiSft)) {
            case EndOfBuffer:
            case EndOfFile:
            case Partial:
                // This should never happen: fail
                fprintf(stderr, "Read buffer logic error at %" PRIzd")!", azPos);
                azLen = EXI_RED ;
                return null ;
            case Added: // data added
                azLen = mzPosInp - azPos ;
                lpDta = mpInp - azLen ;
                break ;
            case Cycled: // buffer cycled
                azLen = mzPosInp - azPos ;
                lpDta = mpMax - azLen ;
                break ;
            }
        }
    }

    #if debug
    if (azPos >= mzPosInp || azPos < mzPosInp - miBufUsd) {
        fprintf(JDebug::stddbg, "JFileIStreamAhead::getbuf(%s,"P8zd",%d)->%2x (sto %p) failed !\n",
                msFid, azPos, aiSft, *mpInp, mpInp);
    }
    #endif

    return lpDta ;
}

/**
 * Retrieve requested position into the buffer, trying to keep the buffer as
 * large as possible (i.e. invalidating/overwriting as less as possible).
 *
 * @param azPos     position to read from
 * @param azLen     out: > 0 : available bytes in buffer, < 0 : EOF or EOB.
 * @param aiSft     0=read, 1=hard ahead, 2=soft ahead
 *
 * @return EOF, EOB
 * @return 0 = data read
 * @return 1 = data read but buffer cycled
 * @return 3 = partial read, unaligned/broken read
 */
JFileIStreamAhead::readresult JFileIStreamAhead::get_fromfile (
    const off_t azPos,      /**< position to read from                */
    const int aiSft         /**< 0=read, 1=hard ahead, 2=soft ahead   */
){
        readtype liSek ;        /**< buffer logic type              */
        readresult liRet=Added; /**< result type                    */
        int liTdo ;             /**< number of bytes to read        */
        int liDne ;             /**< number of bytes read           */
        jchar *lpInp ;          /**< place in buffer to read to     */
        off_t lzPos ;           /**< position to seek               */

        /* Preparation: Check what should be done and set liSek accordingly */
        if (azPos < mzPosInp - miBufUsd ) {
            // Reading before the start of the buffer:
            // - either cancel the whole buffer: easiest, but we loose all data in the buffer
            // - either scroll back the buffer: harder, but we may not loose all data in the buffer
            if (azPos + miBlkSze < mzPosInp - miBufUsd) {
                // scrolling back more than the blocksize is not (yet) possible: just reset
                liSek = Reset ;
            } else {
                // no more than one block: scroll back
                liSek = Scrollback;
            }
        } else if (azPos >= mzPosInp + miBlkSze ) {
            // Reading one block will not be enough: seek and reset
            liSek = Reset ;
        } else {
            // Reading less than one additional block: just append to the buffer
            liSek = Append ;
        }

        /* Soft ahead: seek or overreading the buffer is not allowed */
        if (aiSft == 2 && ((liSek != 0) || (azPos >= mzPosBse + mlBufSze - miBlkSze))) {
            #if debug
            if (JDebug::gbDbg[DBGRED])
                fprintf(JDebug::stddbg, "get_fromfile(%p,"P8zd",%d)->EOB.\n",
                        msFid, azPos, aiSft);
            #endif
            return EndOfBuffer ;
        }

        /* Preparation: Determine the reading position lzPos, lpInp and liTdo */
        switch (liSek) {
        case (Append):
            /* Read new block of data: how many bytes can we read ? */
            liTdo = mpMax - mpInp ;             // Calculate max bytes we can read
            if (liTdo > miBlkSze)
                liTdo = miBlkSze ;              // Reduce to blocksize
            if (mzPosInp + liTdo > mzPosEof)
                liTdo = mzPosEof - mzPosInp ;   // Don't read more than eof
            lpInp = mpInp ;                     // Set new physical read position
            lzPos = mzPosInp ;                  // Set seek position
            break ;

        case (Reset):
            /* Set position: perform an aligned read on miBlksze */
            lzPos = (azPos / miBlkSze) * miBlkSze ;
            lpInp = mpBuf;
            if (lzPos + miBlkSze > mzPosEof)
                liTdo = mzPosEof - mzPosInp ;   // Don't read more than eof
            else
                liTdo = miBlkSze ;

            /* Reset buffer */
            mpInp = mpBuf;
            mzPosInp = lzPos;
            mzPosBse = lzPos;
            miBufUsd = 0 ;

            break ;

        case (Scrollback):
            /* Scroll back: first make room in buffer */
            if (miBufUsd + miBlkSze > mlBufSze) {
                // number of bytes to remove in order to have room for miBlkSze
                liTdo = miBufUsd + miBlkSze - mlBufSze ;
                miBufUsd -= liTdo ;
                mzPosInp -= liTdo ;
                mpInp -= liTdo ;
                if (mpInp < mpBuf)
                    mpInp += mlBufSze ;
            }

            /* scroll back on buffer logic
            *  case 1: [^***********$-----------Tdo]
            *  case 2: [************$----Tdo^******]
            *  case 3: [Td^*********$--------------]
            *  case 4: [--Tdo^*****$---------------]
            *  $   = end of data : mpInp/mzPosInp
            *  ^   = start of data : mpInp / mzPosInp - miBufUsd
            *  *   = data
            *  -   = empty or freed space
            *  Tdo = scrollback
            */
            lzPos = mzPosInp - miBufUsd ;   // start of data
            liTdo = miBlkSze ;              // number of bytes to scroll back
            if ( lzPos < liTdo ) {
                liTdo = lzPos ;             // to not scroll back before zero
            }
            lpInp = mpInp - miBufUsd ;      // start of data
            if (lpInp == mpBuf) {
                /* Case 1 : current start of data is at the start of the buffer,
                *  so we have to read at the end of the buffer (create a cycle).
                */
                lpInp = mpMax - liTdo ;
                liRet = Cycled ;
            } else if (lpInp > mpBuf) {
                /* Case 3 or 4 (untested !!) */
                if (lpInp - liTdo >= mpBuf) {
                    /* Case 4 : there's room at the start of the buffer, use it */
                    lpInp -= liTdo ;
                } else {
                    /* Case 3 : reduce liTdo                                */
                    /* By consequence, maybe azPos will not be read.        */
                    /* In such a case, a second getfromfile is needed !     */
                    /* This can only happen when buffer is unaligned, i.e.  */
                    /* mlBufSze is not a multiple of miBlkSze.              */
                    liTdo = lpInp - mpBuf ;
                    lpInp = mpBuf ;
                    if (lzPos - liTdo > azPos)
                        liRet = Partial ;     // we cannot scroll back to azPos in one read !
                }
            } else {
                /* case 2 : use the space before the start of data */
                lpInp += mlBufSze - liTdo ;
                liRet = Cycled ;
            }
            miBufUsd += liTdo ;
            lzPos -= liTdo ;

            break ;

        } /* switch liSek */

        /* Execute: Perform seek */
        if (liSek != 0) {
            #if debug
            if (JDebug::gbDbg[DBGBUF]) fprintf(JDebug::stddbg, "getfromfile: Seek %" PRIzd ".\n", azPos);
            #endif

            mlFabSek++ ;
            mpStream.seekg(lzPos) ; // throws an exception in case of error
        } /* if liSek */

        /* Execute: Read a block of data (miBlkSze) */
        mpStream.read((char *)lpInp, liTdo) ;
        liDne = mpStream.gcount();
        if (liDne < liTdo) {
            #if debug
            if (JDebug::gbDbg[DBGRED])
                fprintf(JDebug::stddbg, "getfromfile(%p,"P8zd",%d)->EOF.\n",
                        msFid, azPos, aiSft);
            #endif

            // Reset EOF state
            if (mpStream.eof())
                mpStream.clear();

            mzPosEof = lzPos + (off_t) liDne ;
            if (liDne == 0 || azPos >= mzPosEof)
                return EndOfFile ;
        }
        #if debug
        //if (JDebug::gbDbg[DBGRED])
        //    fprintf(JDebug::stddbg, "JFileIStreamAhead::get_fromfile(%s,"P8zd",%d)->%c=%2x (sto %p).\n",
        //            msFid, azPos, aiSft, *mpInp,*mpInp, mpInp);
        #endif

        /* Update the buffer variables */
        /* In principle, all buffer logic has been done during the preparation phase    */
        /* except if the read hit an EOF. Therefore, the length of the buffer is only   */
        /* calculated here: mpInp, miRedSze, azLen, miBufUsd, and mpInp)                */
        switch (liSek) {
        case (Scrollback):
            /* Handle scroll-back */
            if (liDne < liTdo) {
                /* repair buffer when EOF has been hit */
                // 08-2020 How can we hit EOF when scrolling back ?
                // Do we ever come here ???
                mpInp = lpInp + liDne;
                if ( mpInp >= mpMax) {
                    mpInp -= mlBufSze ;
                }

                /* set some safe values */
                mzPosInp = lzPos + liDne ;
                miBufUsd = liDne;
                mzPosBse = mzPosInp ;
                liRet = Partial ;
            } else {
                /* Restore input position (not needed after EOF) */
                mlFabSek++;
                mpStream.seekg(mzPosInp); // throws an exception in case of error
            }
            break ;

        default:
            /* Advance input position */
            mzPosInp += liDne ;
            mpInp    += liDne ;

            // Cycle buffer
            if ( mpInp == mpMax ) {
                mpInp = mpBuf ;
                liRet = Cycled ;
            } else if ( mpInp > mpMax ) {
                fprintf(stderr, "Buffer out of bounds on position %" PRIzd")!", azPos);
                exit(EXI_SEK);
            }
            if ( miBufUsd < mlBufSze ) miBufUsd += liDne ;
            if ( miBufUsd > mlBufSze ) {
                // Quite uncommon, but not an error.
                // Can happen for example after scrolling back (case 1) less than miBlkSze
                miBufUsd = mlBufSze ;
                mzPosBse = mzPosInp - mlBufSze ;
            }
            miRedSze += liDne ;
            if (mpRed == mpMax)
                mpRed = mpBuf ;
            break;
        } /* switch aiSek */

        #if debug
        if (azPos >= mzPosInp || azPos < mzPosInp - miBufUsd ){
            fprintf(JDebug::stddbg, "JFileIStreamAhead(%s,"P8zd",%d)->%2x (sto %p) failed !\n",
                    msFid, azPos, aiSft, *mpInp, mpInp);
        }
        #endif

        /* Return about what we have done */
        return liRet ;
} /* get_fromfile */

} /* namespace JojoDiff */
