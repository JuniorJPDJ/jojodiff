/*
 * JMatchTable.h
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
 */

#ifndef JMATCHTABLE_H_
#define JMATCHTABLE_H_

#include "JDefs.h"
#include "JFile.h"
#include "JHashPos.h"

#define MCH_PME 127                     // Matching hashtable prime
#define MCH_MAX 256                     // Maximum size of matching table

namespace JojoDiff {

/* JojoDiff Matching Table: this class allows to build and maintain  a table of matching regions
 * between two files and allows to select the "best" match from the table. */
class JMatchTable {
public:
	/* Construct a matching table for specified hashtable, original and new files. */
	JMatchTable(JHashPos const * cpHsh,  JFile  * apFilOrg, JFile  * apFilNew, const bool abCmpAll = true);

	/* Destructor */
	virtual ~JMatchTable();

	/* -----------------------------------------------------------------------------
	 * Add given match to the array of matches:
	 * - add to colliding match if possible, or
	 * - add at the end of the list, or
	 * - override an old match otherwise
	 * Returns
	 *   0   if a new entry has been added and table is full
	 *   1   if a new entry has been added
	 *   2   if an existing entry has been enlarged
	 * ---------------------------------------------------------------------------*/
	int add (
	  off_t const &azFndOrgAdd,      /* match to add               */
	  off_t const &azFndNewAdd,
	  off_t const &azRedNew
	);

	/* -----------------------------------------------------------------------------
	 * Get the nearest optimized and valid match from the array of matches.
	 * ---------------------------------------------------------------------------*/
	bool get (
	  off_t const &azBseOrg,       /* base positions       */
	  off_t const &azBseNew,
	  off_t &azBstOrg,             /* best position found  */
	  off_t &azBstNew
	) const;

	/* -----------------------------------------------------------------------------
	 * Cleanup & check if there is free space in the table of matches
	 * ---------------------------------------------------------------------------*/
	int cleanup ( off_t const &czBseNew ) ;

private:
	/**
	 * Verify and optimize matches:
     * Searches at given positions for a run of 8 equal bytes.
     * Searching continues for the given length unless soft-reading is specified
     * and the end-of-buffer is reached.
     *
     * @param   &rzPosOrg    in/out  position on first file
     * @param   &rzPosNew    in/out  position on second file
     * @param   aiLen       in      number of bytes to compare
     * @param   aiSft       in      1=hard read, 2=soft read
     *
     * @return  0 = no run of equal byes found
     * @return  1 = continuous run of 8 equal bytes found
     * @return  2 = EOB reached but at least 4 bytes are equal
     * @return  3 = EOB reached without equality
	 * ---------------------------------------------------------------------------*/
	int check (
	    off_t &rzPosOrg, off_t &rzPosNew,
	    int aiLen, int aiSft
	    ) const ;

	/*
	 * Context: we need the hashtable and the two source files
	 */
	JHashPos const * mpHsh ;
	JFile * mpFilOrg ;
	JFile * mpFilNew ;

	/*
	 * Matchtable elements
	 */
	typedef struct tMch {
	    struct tMch *ipNxt ;    /* next element in collision list */

	    int iiCnt ;             // number of colliding matches (= confirming matches)
	    int iiTyp ;             // type of match:  0=unknown, 1=colliding, -1=gliding
	    off_t izBeg ;           // first found match (new file position)
	    off_t izNew ;           // last  found match (new file position)
	    off_t izOrg ;           // last  found match (org file position)
	    off_t izDlt ;           // delta: izOrg = izNew + izDlt
	    off_t izTst ;           // result of last compare
	    int iiCmp ;             // result of last compare
	} rMch ;

	rMch *msMch ;               /* table of matches */
	rMch *mpMch[MCH_PME];       /* hastable on izDlt with matches       */
	rMch *mpMchFre ;            /* freelist of matches (iiCnt == -1)    */
    rMch *mpMchGld ;            /* last gliding match */
    off_t mzGldDlt ;            /* last gliding match next delta */

	/* settings */
	bool mbCmpAll ;             /* Compare all matches, even if data not in buffer? */

public:
	/* statistics */
	static int siHshRpr;        /* Number of repaired hash hits (by compare)         */
};

}

#endif /* JMATCHTABLE_H_ */
