/*
 * Copyright (c) Medical Research Council 1994. All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * this copyright and notice appears in all copies.
 *
 * This file was written by James Bonfield, Simon Dear, Rodger Staden,
 * as part of the Staden Package at the MRC Laboratory of Molecular
 * Biology, Hills Road, Cambridge, CB2 2QH, United Kingdom.
 *
 * MRC disclaims all warranties with regard to this software.
 */

/*
  Title:  traceType

  File:   traceType.c
  Purpose: determining trace format

  Last update: 01/09/94

  Change log : Update for use with the Read library.
   mieg added recognize ctf format 15/2/2000
*/

/* ---- Imports ---- */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "traceType.h"
#include "Read.h"
#include "filecompress.h"

/* SunOS4 has it's definitions in unistd, which we won't include for compat. */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* ---- Privates ---- */
static struct {
    int type;
    int offset;
    char *string;
} magics[] = {
	{ TT_SCF , 0,   ".scf" } ,
	{ TT_CTF , 1,   "\007\375\343\000" } ,   /* mieg */
	{ TT_ABI , 0,   "ABIF" } ,
	{ TT_ALF , 518, "ALF " } ,
	{ TT_SCF , 0,   "\234\330\300\000" }, /* Amersham variant */
	{ TT_EXP , 0,	"ID   " } ,
	{ TT_ALF , 0,   "ALF " } , /* Added by newer alfsplit programs */
	{ TT_ALF , 0,   "\021G\021G" } , /* Pharmacia's alfsplit equiv */
	{ TT_ALF , 1546,"X-axis" } /* Good guestimation if all else fails */
};

#define Number(A) ( sizeof(A) / sizeof((A)[0]) )

/* ---- exported ---- */

char *trace_types[6] = {
    "unknownFmt",
    "scfFmt",
    "ctfFmt",    /* mieg */
    "abiFmt",
    "alfFmt",
    "plainFmt"
    };


/* unix specific file deletion routine */
int remove_file(char *fn) { return unlink(fn); }


/*
 * Determine the trace type for FILE * 'fp'.
 *
 * Returns:
 *     TT_SCF, TT_CTF, TT_ABI, TT_ALF, or TT_PLN for success.
 *     TT_UNK for unknown type.
 *     TT_ERR for error.
 */
int fdetermine_trace_type(FILE *fp)
{
    int i;
    int len;
    char buf[512];
    int ps;
    int acgt;
    int c;

    /* check magics */
    for (i = 0 ; i < Number(magics) ; i++) {
	if (fseek(fp,magics[i].offset,0) == 0) {
	    len = strlen(magics[i].string);
	    if (fread(buf,1,len,fp)==len) {
		if (strncmp(buf,magics[i].string,len)==0) {
		    return magics[i].type;
		}
	    }
	}
    }
    fseek(fp, 0, 0);

    /* determine if this is a text file */
    len = 0; ps = 0; acgt = 0;
    for (i = 0; i < 512; i++) {
	if ( ( c = fgetc(fp) ) == EOF ) break;
	switch(c) {
	case 'a': case 'c': case 'g': case 't':
	case 'A': case 'C': case 'G': case 'T':
	/*YUK! need the next line?*/
	case 'n': case 'N': case '-':
	    acgt++;
	default:
	    len++;
	    if ( (isprint(c) && isascii(c)) || isspace(c) ) ps++;
	}
    }
    /*YUK! 75% of characters printable means text*/
    if ( 100 * ps > 75 * len ) {
	/*YUK! 75% of printables ACGTN means plain*/
	if (100 * acgt > 75 * ps) {
	    return TT_PLN;
	}
    }

    /* YUK! short files are not traces? */
    if (len<512) {
        return TT_UNK;
    }

    return TT_UNK;
}

/*
 * Determine the trace type for file 'fn'.
 *
 * Returns:
 *     TT_SCF, TT_CTF, TT_ABI, TT_ALF, or TT_PLN for success.
 *     TT_UNK for unknown type.
 *     TT_ERR for error.
 */
int determine_trace_type(char *fn)
{
    FILE *fp;
    int r;

    if ( (fp = fopen_compressed(fn, NULL)) == NULL ) return TT_ERR;

    r = fdetermine_trace_type(fp);
    fclose(fp);

    return r;
}

/*
 * Converts a trace type string to an integer.
 */
int trace_type_str2int(char *str) {
    if (strcmp(str, "SCF") == 0)
	return TT_SCF;
    else if (strcmp(str, "CTF") == 0 || strcmp(str, "ctf") == 0)
        return TT_CTF;   /* mieg */
    else if (strcmp(str, "ABI") == 0 || strcmp(str, "abi") == 0)
	return TT_ABI;
    else if (strcmp(str, "ALF") == 0 || strcmp(str, "alf") == 0)
	return TT_ALF;
    else if (strcmp(str, "PLN") == 0 || strcmp(str, "pln") == 0)
	return TT_PLN;
    else if (strcmp(str, "EXP") == 0 || strcmp(str, "exp") == 0)
	return TT_EXP;
    else
	return TT_UNK;
}

/*
 * Converts a trace type integer to a string.
 */
char *trace_type_int2str(int type) {
    char *t;

    switch(type) {
    case TT_SCF: t = "SCF"; break;
    case TT_CTF: t = "CTF"; break;  /* mieg */
    case TT_ABI: t = "ABI"; break;
    case TT_ALF: t = "ALF"; break;
    case TT_PLN: t = "PLN"; break;
    case TT_EXP: t = "EXP"; break;
    default:
    case TT_UNK: t = "UNK"; break;
    }

    return t;
}

/*
 * Returns a statically declared string containing a 3 character
 * identifier for the trace type of this file.
 * "ERR" represents error, and "UNK" for unknown.
 * Successful values are "SCF", "ABI", "ALF" and "PLN".
 */
char *trace_type_str(char *traceName)
{
    int t;

    if ((t = determine_trace_type(traceName)) == TT_ERR)
	return "ERR";
    else
	return trace_type_int2str(t);
}
