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

#include <stdlib.h>
#include <stdio.h>

#include "JFileIStream.h"
#include "JDebug.h"

namespace JojoDiff {
JFileIStream::JFileIStream(istream &apFil, char const * const asFid) :
    mpStream(apFil), msFid(asFid), mzPosInp(0), mlFabSek(0)
{
}

JFileIStream::~JFileIStream() {
}

/**
 * Return number of seeks performed.
 */
long JFileIStream::seekcount() const {return mlFabSek; }

/**
 * For buffered files, return the position of the buffer
 * JFileIStream does not buffer, so return -1
 * @return  -1=no buffering, > 0 : first position in buffer
 */
off_t JFileIStream::getBufPos() const {
    return -1;
};

/**
 * @brief Set lookahead base: soft lookahead will fail when reading after base + buffer size
 *
 * Attention: the base will be implicitly changed by get on non-buffered reads too !
 *
 * @param   azBse	base position
 */
void JFileIStream::set_lookahead_base (
    const off_t azBse	/* new base position */
) {
    // no need to do anything
}

/**
 * @brief Get next byte
 *
 * Soft read ahead will return an EOB when date is not available in the buffer.
 *
 * @param   aiSft	soft reading type: 0=read, 1=hard read ahead, 2=soft read ahead
 * @return 			the read character or EOF or EOB.
 */
int JFileIStream::get (
    const int aiSft   /* 0=read, 1=hard ahead, 2=soft ahead  */
) {
    return get(mzPosInp, aiSft);
}

/**
 * Gets one byte from the lookahead file.
 */
int JFileIStream:: get (
    const off_t &azPos,    	/* position to read from                */
    const int aiTyp     /* 0=read, 1=hard ahead, 2=soft ahead   */
) {
    if (azPos != mzPosInp){
        mlFabSek++;
        if (mpStream.eof())
            mpStream.clear();
        mpStream.seekg(azPos, std::ios::beg); // may throw an exception
    }
    mzPosInp = azPos + 1;
    return mpStream.get();
} /* function get */
} /* namespace JojoDiff */

