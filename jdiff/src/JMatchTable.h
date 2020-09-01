/*
 * JMatchTable.h
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

#ifndef JMATCHTABLE_H_
#define JMATCHTABLE_H_

#include "JDefs.h"
#include "JFile.h"
#include "JHashPos.h"

namespace JojoDiff {

/* JojoDiff Matching Table: this class allows to build and maintain  a table of matching regions
 * between two files and allows to select the "best" match from the table. */
class JMatchTable {
public:
	/**
	* @brief Construct a matching table for specified hashtable, original and new files.
	*
	* @param  cpHsh     JHashPos Hashtable
	* @param  apFilOrg  Source file
	* @param  apFilNew  Destination file
	* @param  aiSze     Size of the matychtable (in number of elements, one element = 9 words)
	* @param  abCmpAll  Compare all matches, or only those available within the filebuffers
	*/
	JMatchTable(JHashPos const * cpHsh,  JFile  * apFilOrg, JFile  * apFilNew,
             const int aiMchSze, const bool abCmpAll = true);

	/* Destructor */
	virtual ~JMatchTable();

	/**
	 * @brief Add given match to the array of matches
	 *
	 * - Add to a colliding or gliding match if possible, or
	 * - Add at the end of the list, or
	 * - Override an old match if posible
	 *
	 * @return  0   if a new entry has been added and table is full
	 * @return  1   if a new entry has been added
	 * @return  2   if an existing entry has been enlarged
     * @return  3   match was found to be invalid
     * @return  4   added match is very good, no need to continue much longer
     * @return  5   added match is perfect, ne need to continue
	 * @return -1   if a new entry could not be added (table full)
	 * ---------------------------------------------------------------------------*/
	int add (
	  off_t const &azFndOrgAdd,      /* match to add               */
	  off_t const &azFndNewAdd,
	  off_t const &azRedNew
	);

	/**
	 * @brief   Get the best (=nearest) optimized and valid match from the array of matches.
	 *
	 * @param   azBseOrg Current reading position in original file
	 * @param   azBseNew Current reading position in new file
	 * @param   azBstOrg    out: best found new position for original file
	 * @param   azBstNew    out: best found new position for new file
	 * @return  false= no solution has been found
     * @return  true = a solution has been found
	 * ---------------------------------------------------------------------------*/
	bool getbest (
	  off_t const &azBseOrg,       /* base positions       */
	  off_t const &azBseNew,
	  off_t &azBstOrg,             /* best position found  */
	  off_t &azBstNew
	) ;

    /**
     * @brief Cleanup, check free space and fastcheck best match.
     *
     * @param       azBseOrg    Cleanup all matches before this position
     * @param       azRedNew    Current reading position
     * @param       liBck       Max distance to look back
     *
     * @return < 0  Negated number valid matches: one of the matches meets azBseNew
     * @return > 0  Number of valid matches
     * ---------------------------------------------------------------------------*/
    int cleanup ( off_t const azBseOrg, off_t const azRedNew, int const liBck );

private:
    /**
    * Matchtable strcuture
    */
 	typedef struct tMch {
	    struct tMch *ipNxt ;    /**< next element in collision list */

	    int iiCnt ;             /**< number of colliding matches (= confirming matches) */
 	    int iiTyp ;             /**< type of match:  0=unknown, 1=colliding, -1=gliding */
	    off_t izBeg ;           /**< first found match (new file position)              */
	    off_t izNew ;           /**< last  found match (new file position)              */
	    off_t izOrg ;           /**< last  found match (org file position)              */
	    off_t izDlt ;           /**< delta: izOrg = izNew + izDlt                       */
	    off_t izTst ;           /**< result of last compare                             */
	    int iiCmp ;             /**< result of last compare                             */
	} rMch ;


	/**
	 * @brief Verify and optimize matches
	 *
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
	    int aiLen, const JFile::eAhead aiSft
	    ) const ;

    /**
    * @brief   Calculate position on original file corresponding to given new file position.
    *
    * @param   rMch     *apCur    Match (in)
    * @param   off_t    azTstOrg  Position on org file (out only)
    * @param   off_t    azTstNew  Position on new file (in/out, adapted if azTstOrg would become negative)
    */
    void calcPosOrg(rMch *apCur, off_t &azTstOrg, off_t &azTstNew) const ;

	/*
	 * Context: we need the hashtable and the two source files
	 */
	JHashPos const * mpHsh ;
	JFile * mpFilOrg ;
	JFile * mpFilNew ;

	/*
	 * Matchtable elements
	 */
	rMch *msMch ;               /**< table of matches (dynamic array)                    */
	rMch **mpMch ;              /**< hastable on izDlt with matches (dynamic array)      */
	rMch *mpMchFre ;            /**< freelist of matches                                 */
    rMch *mpMchGld ;            /**< last gliding match                                  */
    rMch *mpMchBst ;            /**< found best match                                    */
    off_t mzGldDlt ;            /**< last gliding match next delta                       */
    int miMchFre ;              /**< free index (0 at start)                             */

	/* settings */
	int  miMchSze ;             /**< Size of the matching table                       */
	int  miMchPme ;             /**< Size of matching hashtable                       */
	bool mbCmpAll ;             /**< Compare all matches, even if data not in buffer? */

public:
	/* statistics */
	static int siHshRpr;        /**< Number of repaired hash hits (by compare)         */
};

}

#endif /* JMATCHTABLE_H_ */
