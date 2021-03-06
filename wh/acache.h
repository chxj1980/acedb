/*  File: acache.h
 *  Author: Fred Wobus (fw@sanger.ac.uk)
 *  Copyright (c) J Thierry-Mieg and R Durbin, 1999
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
 *	Jean Thierry-Mieg (CRBM du CNRS, France) mieg@kaa.crbm.cnrs-mop.fr
 *
 * Description: prototypes for the adisk routines
 *              new way to store data in test for acedb5
 * Exported functions: 
 * HISTORY:
 * Last edited: Aug 26 16:39 1999 (fw)
 * Created: Thu Aug 26 16:39:04 1999 (fw)
 * CVS info:   $Id: acache.h,v 1.4 1999-09-01 11:01:29 fw Exp $
 *-------------------------------------------------------------------
 */

#ifndef ACEDB_ACACHE_H
#define ACEDB_ACACHE_H

void aCacheKill (KEY key) ;
BOOL aCacheGet (KEY key, KEY *pp, Array *ap, char *format) ;
void aCacheStore (KEY key, KEY parent, Array a, char *format) ;
void aCacheStoreDestroy (KEY key, KEY parent, Array a, char *format) ;

#endif /* !ACEDB_ACACHE_H */
