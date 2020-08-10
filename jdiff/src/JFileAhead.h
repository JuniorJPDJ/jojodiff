/*
 * JFileAhead.cpp
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

#ifndef JFILEAHEAD_H_
#define JFILEAHEAD_H_

using namespace std;

#include "JDefs.h"
#include "JFile.h"

namespace JojoDiff {
/**
 * Buffered JFile access: optimized buffering logic for the specific way JDiff
 * accesses files, that is reading ahead to find equal regions and then coming
 * back to the base position for actual comparisons.
 */
class JFileAhead: public JFile {
public:
    virtual ~JFileAhead();
    JFileAhead(FILE * const apFil, char const * const asFid,
               const long alBufSze = 256*1024, const int aiBlkSze = 4096 );

	/**
	 * @brief Get byte at specified address and increment the address to the next byte.
	 *
	 * Soft read ahead will return an EOB when date is not available in the buffer.
	 *
	 * @param   azPos	position to read, incremented on read (not for EOF or EOB)
	 * @param   aiTyp	0=read, 1=hard read ahead, 2=soft read ahead
	 * @return 			the read character or EOF or EOB.
	 */
    int get(
        const off_t &azPos,   /* position to read from                */
        const int aiTyp 	  /* 0=read, 1=hard ahead, 2=soft ahead   */
    );

 	/**
	 * @brief Get next byte
	 *
	 * Soft read ahead will return an EOB when date is not available in the buffer.
	 *
	 * @param   aiSft	soft reading type: 0=read, 1=hard read ahead, 2=soft read ahead
	 * @return 			the read character or EOF or EOB.
	 */
	int get (
	    const int aiSft = 0   /* 0=read, 1=hard ahead, 2=soft ahead   */
	) ;

    /**
	 * @brief Set lookahead base: soft lookahead will fail when reading after base + buffer size
	 *
	 * Attention: the base will be implicitly changed by get on non-buffered reads too !
	 *
	 * @param   azBse	base position
	 */
	void set_lookahead_base (
	    const off_t azBse	/* new base position for soft lookahead */
	) ;

	/**
	 * @brief For buffered files, return the position of the buffer
	 *
	 * @return  -1=no buffering, > 0 : first position in buffer
	 */
	 off_t getBufPos() const ;

    /**
     * Return number of seek operations performed.
     */
    long seekcount() const ;

private:
    /**
     * @brief Get data from the buffer. Call get_fromfile such is not possible.
     *
     * @param azPos		position to read from
     * @param aiTyp		0=read, 1=hard ahead, 2=soft ahead
     * @return data at requested position, EOF or EOB.
     */
    int get_frombuffer(
        const off_t &azPos,   /* position to read from                */
        const int aiTyp       /* 0=read, 1=hard ahead, 2=soft ahead   */
    );

    /**
     * @brief Get data from the underlying system file.
     * Retrieves requested position into the buffer, trying to keep the buffer as
     * large as possible (i.e. invalidating/overwriting as less as possible).
     * Calls get_frombuffer afterwards to get the data from the buffer.
     *
     * @param azPos		position to read from
     * @param aiSft		0=read, 1=hard ahead, 2=soft ahead
     * @return data at requested position, EOF or EOB.
     */
    int get_outofbuffer(
        const off_t &azPos, /* position to read from                */
        const int aiSft     /* 0=read, 1=hard ahead, 2=soft ahead   */
    );

private:
    /* Context */
    char const * const msFid;   /**< file id (for debugging)              */
    FILE * const mpFile;        /**< file handle                          */

    /* Settings */
    long mlBufSze;      /**< File lookahead buffer size                   */
    int miBlkSze;       /**< Read file in blocks of 4096 bytes            */

    /* Buffer state */
    long miRedSze;      /**< distance between izPosRed to izPosInp        */
    long miBufUsd;      /**< number of bytes used in buffer               */
    jchar *mpBuf;       /**< read-ahead buffer                            */
    jchar *mpMax;       /**< read-ahead buffer end                        */
    jchar *mpInp;       /**< current position in buffer                   */
    jchar *mpRed;       /**< last position read from buffer			      */
    off_t mzPosInp;     /**< current position in file                     */
    off_t mzPosRed;     /**< last position read from buffer				  */
    off_t mzPosEof;     /**< eof position 			                      */
    off_t mzPosBse;     /**< base position for soft reading               */

    /* Statistics */
    long mlFabSek ;     /**< Number of times an fseek operation was performed  */
};
}
#endif /* JFILEAHEAD_H_ */
