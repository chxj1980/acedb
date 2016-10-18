/*  File: aceclient.c
 *  Author: Jean Thierry-Mieg (mieg@kaa.cnrs-mop.fr)
 *  Copyright (C) J Thierry-Mieg and R Durbin, 1992
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
 * 	Richard Durbin (MRC LMB, UK) rd@mrc-lmb.cam.ac.uk, and
 *	Jean Thierry-Mieg (CRBM du CNRS, France) mieg@kaa.cnrs-mop.fr
 *
 *
 * Description:
 * I started from a sample code generated by rpcgen on Solaris
 * and a first version by Peter Kocab.
 * 
 * Exported functions:
 * HISTORY:
 * Last edited: Jul 11 14:31 2000 (edgrif)
 * * Nov  4 17:22 1999 (fw): switched input to ACEIN
 * * Dec  2 15:55 1998 (edgrif): Correct decl. of main, add code to
 *              record build time of this module.
 * Created: Wed Nov 25 20:02:45 1992 (mieg)
 *-------------------------------------------------------------------
 */

 /* $Id: aceclient.c,v 1.41 2003-07-04 01:50:14 mieg Exp $ */

#include <sys/signal.h>
#include <wh/regular.h>
#include <wh/aceio.h>
#include <wh/aceclient.h>
#include <wh/version.h>
#include <wh/aceversion.h>

static int nAceIn = 0;
static int chunkSize = 10;

extern int accessDebug ; /* in aceclientlib.c */



/****************************************************************/

static char* getAceLevel (ACEIN fi, char *prompt, int limit, BOOL mask)
     /* read */
{
  static Stack s = 0 ;
  BOOL in = FALSE ;
  char *cp, cutter ;
  int nn = 1 ;

  s = stackReCreate (s, 30000) ;

  /* If mask is true then "parse =" is pushed on to the front of whatever    */
  /* the user enters at the command line. This is all implicit and horrible, */
  /* we already have one mechanism for passing pure ace data to the server,  */
  /* why do we need another one ????????  uuuggggghhhhh.......               */
  if (mask)
    pushText (s, "parse = ") ;

  while (aceInPrompt (fi, prompt))
    { 
      cp = aceInWordCut (fi, "\n" ,&cutter) ;
      if (cp && *cp)
	{ if (!in)
	    { nn++ ;
	      nAceIn++ ; in = TRUE ;
	    }
	  catText (s, cp) ;
	  if (mask) catText (s, "\\n") ;
	  else catText (s, "\n") ;
	}
      else
	{ if (mask) catText (s, "\\n") ;
	  else catText (s, "\n") ;
	  in = FALSE ;
	  if (limit && !(nn % limit))
	    break ;
	}
    }

  if (nn > 1)
    return stackText (s, 0);

  return NULL;
} /* getAceLevel */

/*****************************************************************/

static char* getAceData (char *prompt)
{ 
  ACEIN fi;
  char *cp ;

  fi = aceInCreateFromStdin (TRUE, "", 0);
  /* do NOT test stdinIsInteractive(), 
   * this would  changes the behaviour of the
   * code in pipe calls
   */

  aceInSpecial (fi, "\n/\\\t@") ;
  
  cp = getAceLevel (fi, prompt, 0, TRUE) ;

  aceInDestroy (fi);

  return cp ;
} /* getAceData */

/*****************************************************************/

static char* getOrder (ACEIN fi, char *prompt, BOOL silent, 
		       BOOL isAceIn, BOOL isAceOut, BOOL isReport)
{ 
  static Stack s = 0 ;
  char *cp = 0, cutter, *errtext;
  int np ;
  
  if (isAceIn)
    return getAceLevel (fi, prompt, 500, FALSE) ;
 lao:
  cutter = 0 ;   
  cp = aceInWordCut (fi, isReport ? "#" : "" ,&cutter) ;
  if ((!cp && !cutter)  ||
      (cp && *cp == '/' && *(cp+1) == '/'))
    {
      if (!aceInPrompt(fi, prompt))
	return 0 ;                       /* just get a card */

      if (isReport && (!cp || *cp != '/'))
	{ putchar ('\n'); fflush(stdout); }
      goto lao ;
    }

  if (!silent || isAceOut)
    return cp ;   /* the whole line will be processed by the server */
  
    /* Now silent = TRUE and we are in report mode*/

  if (!cutter)    /* just print out the line */
    { 
      if (cp)
	{ printf("%s",cp); fflush(stdout); }
      goto lao ;
    }

  if (cp)
    { 
      printf("%s",cp) ; /* print the beginning */
      fflush(stdout);
    }
  s = stackReCreate(s, 50) ;
  s->textOnly = TRUE ;
  pushText(s,"") ;
  
  if (!freestep('('))
    { putchar (cutter) ; goto lao ; }

  np = 1 ;
  while (np && (cp = aceInWordCut(fi, "()" ,&cutter)))
    { 
      catText(s, cp) ;
      switch (cutter)
	{ 
	case '(':
	  catText(s,"(") ;
	  np++ ;
	  break ;
	case ')':
	  np-- ;
	  if (np)
	    catText(s,")") ; 
	  break ;
	case 0:
	  if (np)
	    { errtext = "Missing right parenthese" ;
	      goto error ;
	    }
	}
    }
  cp = stackText(s,0) ;
  return cp ;  /* let the server process this command */

 error:
  printf("//! %s after %% in %s\n", errtext, stackText(s,0)) ;
  cp = aceInPos(fi) ;
  if (cp)
    printf("//! %s",cp) ;
  fflush(stdout);
  goto lao ;
} /* getOrder */

/*********************************************************************/

static void signalHandler (int sig)
{
  if (sig == SIGINT)
    messExit ("SIGINT");
  else if (sig == SIGQUIT)
    messExit ("SIGQUIT");
} /* signalHandler */

/*********************************************************************/

/* Defines a routine to return the compile date of this file. */
UT_MAKE_GETCOMPILEDATEROUTINE()


int main (int argc, char *argv[])
{
  void *handle; /* JC do not need to know what it is */
  char *host;
  char *prompt;
  unsigned char *answer;
  int   
    n, retval,
    length,
    t, timeOut = 300 ,
    encore = 0 ;
  unsigned long port = DEFAULT_PORT;
  unsigned long p;
  BOOL 
    silent = FALSE, doWrite = TRUE , isReport = FALSE, encoring,
    isAceIn = FALSE, isAceOut = FALSE, isQuit = FALSE, isShutdown = FALSE ;
  char *cp ;
  ACEIN fi = 0;
  Stack s = 0 ;
  struct sigaction sa;


  /* Does not return if user typed "-version" option.   
     checkForCmdlineVersion(&argc, argv) ;
                     */
  memset (&sa, 0, sizeof(struct sigaction));

  sa.sa_handler = signalHandler;
  sigemptyset(&sa.sa_mask);

  if (sigaction(SIGINT, &sa, 0) != 0 ||	   /* Ctrl-C */
      sigaction(SIGQUIT, &sa, 0) != 0)     /* Ctrl-\ */
    messerror ("Signal handler initialisation failed (%s)",
	       messSysErrorText());

  if ( argc < 2 ) 
    goto abort ;
  host = argv[1] ;
  prompt = strnew(messprintf("acedb@%s> ", host), 0);

  n = 2 ;
  if (argc > n + 1 && strcmp("-port", argv[n]) == 0)
    {
      if (sscanf(argv[n+1],"%lu",&p)== 1)
	{
	  port = p ;
	  n += 2 ;
	}
      else
	goto abort ;
    }

  if (argc > n + 1 && strcmp("-time_out", argv[n]) == 0)
    {
      if (sscanf(argv[n+1],"%ic",&t)== 1)
	{
	  timeOut = t ;
	  n += 2 ;
	}
      else
	goto abort ;
    }

  if (argc > n && (strcmp("-access_info", argv[n]) == 0
		   || strcmp("access_info", argv[n]) == 0))
    { 
      accessDebug = TRUE ;
      n++ ;
    }
  
  if (argc > n && (strcmp("-ace_out", argv[n]) == 0
		   || strcmp("ace_out", argv[n]) == 0))
    { 
      isAceOut = TRUE ;
      silent = TRUE ;
      n++ ;
    }

  if (argc > n && (strcmp("-ace_in", argv[n]) == 0
		   || strcmp("ace_in", argv[n]) == 0))
    { 
      isAceIn = TRUE ;
      encore = 3 ;
      fi = aceInCreateFromStdin (FALSE, "", 0);/* non-interactive */
      n++ ;
    }
 
  if (argc > n)
    {
      if (!strcmp("-f", argv[n]))
	{ 
	  n++ ;
	  isReport = TRUE ;
	  silent = TRUE;
	  fi = aceInCreateFromFile (argv[n], "r", "", 0);
	  if (!fi)
	    {
	      printf ("//! Cannot open file %s given on command line\n",
		      argv[n]) ;
	      fflush(stdout);
	      goto abort ;
	    }
	}
      else
	goto abort ;
    }

  s = stackReCreate(s,50) ;
  pushText(s,"") ;

  if (argc > n)
    { 
      int i ;
      n++ ;
      for (i= n ; i < argc ; i++)
	{
	  catText(s, argv[i]) ;
	  catText(s, " ") ;
	}
    }

  if (!fi)
    {
      if (silent)
	/* non-interactive input */
	fi = aceInCreateFromStdin (FALSE, "", 0);
      else
	fi = aceInCreateFromStdin (TRUE, "", 0);
  /* do NOT test stdinIsInteractive(), 
   * this would  changes the behaviour of the
   * code in pipe calls
   */

    }

  if (isReport)
    aceInSpecial (fi, "\n\t\\@%$") ; /* no // (to echo empty lines), allow client-side sub shells */
  else
    aceInSpecial (fi, "\n\t\\/@%$") ; /* allow client-side sub shells */


  if ((handle = openServer(host, port, timeOut)) == NULL)
    { 
      printf("//! cannot establish connection\n") ; 
      fflush(stdout);
      goto abort;
    }
#ifdef DEBUG
  else
    printf("opened connection to %s on port %lu\n\n",host,port);
#endif

  while((cp = getOrder(fi, prompt, silent, isAceIn, isAceOut, isReport)))
    { 
      if (strncasecmp(cp, "query", 5) != 0
	  && freelower(*cp) == 'q')  /* quit command */
	{ 
	  cp = "quit" ;
	  isQuit = TRUE ;  /* transmit the quit to the server */
	}

      if (strncasecmp(cp, "shutdown", 8) == 0)
	isShutdown = TRUE ;
      else
	isShutdown = FALSE ;

      doWrite = TRUE ;
      if (isAceOut && strncasecmp(cp,"Write",5) && strncasecmp (cp, "Table", 5))
	doWrite = FALSE ;

      /* If someone just enters "parse" then get the ace data from stdin.    */
      if (!strncasecmp(cp, "parse", 5) && strNumWords(cp) == 1)
	{
	  cp = getAceData ("parse> ") ;
	  if (!cp)
	    continue;
	}

      /* For requests that generate large responses from the server, this    */
      /* gets set TRUE and we have an inner loop with goto suiteReponse.     */
      encoring = FALSE ;

    suiteReponse:
      retval = askServerBinary (handle, cp, &answer, &length, &encore, chunkSize) ;
      if (isShutdown && retval == 5)
	{ printf("// The server has disconnected\n\n") ; /* DWB - report error code */
	exit (0) ;
	}
      if (isQuit) break ; /* reply is null */
      if (retval > 0)  /* error handling */
	{ printf("//! Null answer - Error Code: %d\n", retval) ; /* DWB - report error code */
	  printf("//! You may have timed out") ;
	    break ;
	  continue ;
	}
      if (!encore && !answer) /* error handling */
	{ printf("//! Null reponse, you may have timed out \n") ;
	  if (silent || messQuery("// Do you want to quit (y/n) ?"))
	    break ;
	  continue ;
	}

      if (length && !encoring && !silent)
	printf ("// Reponse: %d bytes.\n", length) ;

      s = stackReCreate(s,length) ;
      stackTextForceFeed(s, length) ;
      memcpy(stackText(s,0), answer, length) ;
      free(answer);

      stackCursor(s, 0) ;
   
      if (doWrite)
	{ 
	  while ((cp = stackNextText(s)))
	    {
	      if ((silent || (encore && encore != 3)) && (!*cp || *cp == '/'))
		continue ;

	      if (silent)  /* jump // lines */
		{ register char *cp1 = cp, *cp2, cc ;
		  while (*cp1)
		    { cp2 = cp1 ;
		      while (*cp2 && *cp2 != '\n') cp2++ ;
		      cc = *cp2 ; *cp2 = 0 ;
		      if (!(*cp1 == '/' && *(cp1 + 1) == '/'))
			puts (cp1) ;
		      if (cc) *cp2++ = cc ;
		      cp1 = cp2 ;
		    }
		}
	      else
		{ 
		  if (*cp)
		    { register char *cp1 = cp + strlen(cp) - 1 ;
		      if (cp1 >= cp && *cp1 == '\n')
			*cp1 = 0 ; /* because puts adds a terminal \n */
		      if (*cp) puts (cp) ; else putchar('\n') ;
		    }
		}
	    }
	}
      if (isAceIn)
	continue ;
      if (encore)
	{ encoring = TRUE ; cp = "encore" ;
	  goto suiteReponse ; /* get rest of data */
	}
    }

  closeServer(handle);
  if (!silent)
    printf("\n"
	   "// Please report problems to mieg@kaa.crbm.cnrs-mop.fr\n"
	   "// Bye\n"
	   "\n") ;
  if (isAceIn)
    printf ("\n// Passed %d objects to the server, \n// ciao bye \n\n", nAceIn) ;
  return(EXIT_SUCCESS) ;

 abort:
  fprintf(stderr, 
	  "//! usage: aceclient host [-port number] [-time_out secs] "
	  "[-access_info] [-ace_out] [-ace_in] [-f reportfile parameters]\n") ;
  return(EXIT_FAILURE) ;
} /* main */



/*********************** eof ********************************/
 
 
