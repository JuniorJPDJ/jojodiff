/*
 * JFileIStream.cpp
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

#ifndef JFILEISTREAM_H_
#define JFILEISTREAM_H_

#include <istream>
using namespace std ;

#include "JDefs.h"
#include "JFile.h"

namespace JojoDiff {

/*
 * Unbuffered IStream access: all calls to JFile go straight through to istream.
 */
class JFileIStream: public JFile {
public:
    /**
     * Construct an unbuffered JFile on an istream.
     */
	JFileIStream(istream &apFil, char const * const asFid);

	/**
	 * Destroy the JFile.
	 */
	virtual ~JFileIStream();

	/**
	 * Get one byte from the file from the given position.
	 */
	int get (
		    const off_t &azPos,	/* position to read from                */
		    const int aiTyp     /* 0=read, 1=hard ahead, 2=soft ahead   */
		);

	/**
	 * @brief Get next byte
	 *
	 * Soft read ahead will return an EOB when date is not available in the buffer.
	 *
	 * @param   aiSft	soft reading type: 0=read, 1=hard read ahead, 2=soft read ahead
	 * @return 			the read character or EOF or EOB.
	 */
	virtual int get (
	    const int aiSft = 0   /* 0=read, 1=hard ahead, 2=soft ahead   */
	) ;

	/**
	 * @brief Set lookahead base: soft lookahead will fail when reading after base + buffer size
	 *
	 * Attention: the base will be implicitly changed by get on non-buffered reads too !
	 *
	 * @param   azBse	base position
	 */
	virtual void set_lookahead_base (
	    const off_t azBse	/* new base position for soft lookahead */
	) ;

    /**
     * Return number of seek operations performed.
     */
    long seekcount() const ;

     /**
     * For buffered files, return the current position of the buffer
     * JFileIStream does not buffer so returns -1
     *
     * @return  -1=no buffering, > 0 : first position in buffer
     */
	off_t getBufPos() const ;


private:
	/* Context */
    istream    &mpStream;       /**< file handle                            */
    char const * const &msFid;  /**< file id (for debugging)                */

    /* State */
    off_t mzPosInp;         /**< current position in file                   */

    /* Statistics */
    long mlFabSek ;         /**< Number of times an fseek was performed     */
};
}
#endif /* JFILEISTREAM_H_ */
