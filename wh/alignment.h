/*  File: alignment.h
 *  Author: Fred Wobus (fw@sanger.ac.uk)
 *  Copyright (C) J Thierry-Mieg and R Durbin, 1998
 * -------------------------------------------------------------------
 * Acedb is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 * or see the on-line version at http://www.gnu.org/copyleft/gpl.txt
 * -------------------------------------------------------------------
 * This file is part of the ACEDB genome database package, written by
 * 	Richard Durbin (Sanger Centre, UK) rd@sanger.ac.uk, and
 *	Jean Thierry-Mieg (CRBM du CNRS, France) mieg@crbm.cnrs-mop.fr
 *
 * Description: public header for alignment.c
 * Exported functions:
 *              alignDumpKeySet
 *              alignDumpKey
 * HISTORY:
 * Last edited: Oct 25 14:42 1999 (fw)
 * Created: Thu Nov 19 14:59:51 1998 (fw)
 *-------------------------------------------------------------------
 */

#ifndef _ALIGNMENT_H
#define _ALIGNMENT_H

#include "acedb.h"
#include "aceiotypes.h"

int alignDumpKeySet (ACEOUT dump_out, KEYSET kSet);
BOOL alignDumpKey (ACEOUT dump_out, KEY key);

#endif /* _ALIGNMENT_H */
