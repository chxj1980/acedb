/*  File: dceclientlib.c
 *  Author: Richard Bruskiewich (rbrusk@octogene.medgen.ubc.ca)
 *  Copyright (C) J Thierry-Mieg and R Durbin, 1992-1997
 *-------------------------------------------------------------------
 * This file is part of the ACEDB genome database package, written by
 * 	Richard Durbin (MRC LMB, UK) rd@mrc-lmb.cam.ac.uk, and
 *	Jean Thierry-Mieg (CRBM du CNRS, France) mieg@kaa.cnrs-mop.fr
 *
 * Description:
 * Distributed Computing Environment (DCE) RPC - client side
 * Adapted from aceclientlib.c, version 1.9 9/5/96
 * 
 * Exported functions:
	 openServer()
	 closeServer()
	 askServer()
	 askServerBinary()
 * HISTORY:
 * Last edited: Dec 31 01:15 1997 (rbrusk): ace4.5e, continuing development
 * * Aug 8 00:37 1997 (rbrusk): implemented full protocol specs
 * * Jul 22 20:17 1997 (rbrusk)
 * * Mar 16 14:20 1997 (rbrusk): rewrote ace_server() interface (again)
 * * Feb 13 01:20 1997 (rbrusk): rewrote ace_server() interface
 * * Jan 9 13:35 1997 (rbrusk)
 * Created: Jan 9 13:35 1997 (rbrusk)
 *-------------------------------------------------------------------
 */

/* %W% %G% */

#include "regular.h"
#include <errno.h>
#include "rpcace.h"

/* The semantics of the "clnt" symbol in 
   aceclient.h is really "svr" == pointer to SERVER  */
#define clnt svr
#include "aceclient.h"
#include "WinAceError.h"
#include "client2service.h"

/* A global client_debug flag */
ACLNT_VAR_DEF BOOL client_debug = FALSE ;

/*************************************************************
 * A Client Authentication routine?
 *************************************************************/
static int getMagic (int magic1, char *nm)
{ int magic = 0, magic2 = 0, magic3 = 0 ;
  FILE *f ;
  int level ;
  char *cp ;

  if (magic1 < 0) magic1 = -magic1 ; /* old system */
  if (!nm || !*nm) return 0 ;
  freeinit() ;
  level = freesettext(nm,0) ;
  if (!freecard(level))
    goto fin ;

  cp = freeword () ;
  if (!cp) goto fin ;
  if (client_debug)fprintf(stderr,"Write: %s ", cp) ;  
  if (strcmp(cp, "NON_WRITABLE"))
    {
      f = fopen (cp, "r") ;
      if (!f)
	goto tryread ;
      if (fscanf(f, "%d", &magic3) != 1)
	magic3 = 0 ;
      fclose(f) ;
    }

tryread:
  cp = freeword () ;
  if (magic3 || !cp) goto assumePublic ;
if (client_debug)fprintf(stderr,"Read: %s ", cp) ;  
  if (strcmp(cp, "PUBLIC") && strcmp(cp,"RESTRICTED"))
    {

      f = fopen (cp, "r") ;
      if (!f)
	{ messout ("// Access to this database is restricted sorry\n") ;
	goto fin ;
	}  
      if (fscanf(f, "%d", &magic2) != 1)
	messout ("// Access to this database is restricted sorry\n") ;
      fclose(f) ;
    }
  else if (strcmp(cp, "PUBLIC") && strcmp(cp,"RESTRICTED"))
assumePublic:  /* compatibility with older servers */
  magic = magic1 ;
  if (magic2)
    magic  = magic1 * magic2 % 73256171 ;
  if (magic3)
    magic = magic1 * magic3 % 43532334 ;

fin:
  freeclose(level) ;
if (client_debug)fprintf(stderr,"magic1=%d, magic2=%d, magic3=%d, magic=%d\n", 
		  magic1, magic2, magic3, magic) ;
  return magic ;
}

/*
 * This function returns a DCE/RPC server binding handle (in lpSvr object)
 */
static  LP_SERVER bindServer( unsigned char *szServerStringBinding, 
                              RPC_STATUS *status)
{
    RPC_BINDING_HANDLE hServerBinding ;
    LP_SERVER lpSvr = NULL ;

    if( szServerStringBinding == NULL || status == NULL ) {

#if defined(WIN32) && defined(_DEBUG)
	    AceASSERT(0) ; /* I want to trap this during debugging?! */
#endif
	    *status = RPC_S_INVALID_ARG ;
    }
    else
    {
        *status = RpcBindingFromStringBinding( szServerStringBinding, &hServerBinding )  ;

        if (*status == RPC_S_OK )
        {   /* Assume valid hServerBinding found, so assign bindings? */
            int bSize = strlen(szServerStringBinding) ;
            lpSvr = (LP_SERVER)messalloc( (mysize_t)sizeof(SERVER) ) ;  
            lpSvr->lpszServerStringBinding = messalloc( (mysize_t)(bSize + 1) ) ;
            strcpy(lpSvr->lpszServerStringBinding,szServerStringBinding) ;
            lpSvr->hServerBinding = hServerBinding ;
        }
    }
    return lpSvr ;  /* may be NULL */
}

/*
 * This function frees DCE server binding resources
 * If the function fails, the contents of lpSvr may be left indeterminate?
 */
static RPC_STATUS freeServer( LP_SERVER lpSvr )
{
    RPC_STATUS status ;

    if( lpSvr == NULL ) return RPC_S_INVALID_ARG ;

    if( !(lpSvr->lpszServerStringBinding == NULL ||
	  lpSvr->hServerBinding == NULL ) )
    {
        if( (status = RpcBindingFree( &(lpSvr->hServerBinding) )) 
             != RPC_S_OK ) return status ;
    }
    else 
        return RPC_X_SS_CONTEXT_MISMATCH ;

    /*Otherwise, OK to attempt SERVER deletion */
    if( messfree( lpSvr->lpszServerStringBinding ) &&
        messfree( lpSvr ) )
        return RPC_S_OK ;
    else
        return RPC_S_ACCESS_DENIED ;
}

RPC_STATUS freeReponse( unsigned char **reponse )
{
	RPC_STATUS rpc_status = RPC_S_OK ;
	rpc_status = RpcSmFree( *reponse ) ;
	*reponse = NULL ;
	return rpc_status ;
}

RPC_STATUS copyReponse( unsigned char **pAnswer, long *pAnswerLength, unsigned char **pReponse )
{
	RPC_STATUS rpc_status = RPC_S_OK ;

	if( *pReponse != NULL && **pReponse )
	{
		int reponseSize = strlen(*pReponse) ;
#if !defined(WIN32)
		*pAnswer = malloc( (mysize_t)(reponseSize + 1) ) ;
#else  /* defined(WIN32) */
                /*  Is it OK to use messalloc here
                    so the calling routine can use messfree()
                    which operates upon the heap of the DLL
                    not the application? May crash (never NULL) */
		*pAnswer = messalloc( (mysize_t)(reponseSize + 1) ) ;
#endif  /* defined(WIN32) */
		if( *pAnswer != NULL )
		{
			_mbscpy( *pAnswer, *pReponse ) ;
			*pAnswerLength = reponseSize ;
			rpc_status = freeReponse( pReponse ) ;
		}
                else  /* Note: never executed if pAnswer is messalloc()'d */
		{
			rpc_status = RPC_S_OUT_OF_MEMORY ;
			*pAnswer = NULL;
			*pAnswerLength = 0;
		}
	}
	else
	{
		*pAnswer = NULL;
		*pAnswerLength = 0;
	}
	return rpc_status ;
}

unsigned char *defaultProtocol()
{    
    static unsigned char pStr[] = "ncacn_ip_tcp" ;
    return pStr ;
}

unsigned char *defaultEndpoint(unsigned char *service, unsigned char *protocol)
{
    static unsigned char epStr[256]  ;
	protocol = checkProtocol(protocol) ;

    if ( !strcmp("ncacn_np", protocol) )
    {
        if( !service || !*service)
			strcpy(epStr, "\\pipe\\aceserver") ;
		else
		{
			strcpy(epStr, "\\pipe\\") ;
			strncat(epStr, service, 249) ;
		}
    }
    else if ( !strcmp("ncacn_nb_nb", protocol) )
        strcpy(epStr, "32") ;
    else
        strcpy(epStr, "11000") ;  /* a TCP/IP or other port #? */

    epStr[255] = '\0' ;
    strlwr(epStr) ;

    return epStr ;
}

/* checkProtocol permits the user to use generic terms such as 
   PIPE and TCP/IP for protocols on the commandline,
   rather than "ncacn_*" notation, which is more esoteric?
   The user CAN use that notation, however, but this function
    does not otherwise (currently) validate the protocol strings
 */
unsigned char *checkProtocol(unsigned char *protocol)
{
    static unsigned char pStr[32] ;
 
    if(!protocol || !*protocol) 
		return defaultProtocol() ;

	if( !stricmp("pipe", protocol) )  /* the default anyhow...*/
        strcpy((char *)pStr, "ncacn_np" ) ;
    else if ( !stricmp("tcp/ip", protocol) ||
              !stricmp("tcpip",  protocol) ||
              !stricmp("tcp_ip",  protocol))
        strcpy((char *)pStr, "ncacn_ip_tcp" ) ;
    else if ( !stricmp("netbeui", protocol))
        strcpy((char *)pStr, "ncacn_nb_nb" ) ;
    else
        /* otherwise, should be "ncacn_*" notation; WARNING: Not validated! */
        strncpy((char *)pStr, protocol, 31) ;
    
    pStr[31] = '\0' ;
    strlwr(pStr) ;

    return pStr ;
}

unsigned char *checkEndpoint(unsigned char *service, 
							 unsigned char *protocol, 
							 unsigned char *endpoint )
{
    static unsigned char epStr[256]  ;

    if(!endpoint || !*endpoint)
		return defaultEndpoint(service, protocol) ;

	/* not really reentrant, but guaranteed to terminate? */
	if(!protocol || !*protocol)
		return checkEndpoint(service, defaultProtocol(), endpoint) ;

    /* This implementation doesn't do any further checking? */
    strncpy(epStr, endpoint, 255) ;
    epStr[255] = '\0' ;
    strlwr(epStr) ;

    return epStr ;
}

/*************************************************************
Open RPC connection to server

INPUT (Note: The defaults should eventually be user configurable?)

      unsigned char *service    server service name
                                    Default: AceServer 

      unsigned char *host       hostname running server
                                    Default: local host 

      unsigned char *protocol   communication protocol code
                                    pipe or "ncacn_np" == connection oriented named pipe
                                    tcp/ip or "ncacn_ip_tcp" == connection oriented TCP/IP
                                    Default: "pipe"
        
      unsigned char *endpoint   protocol dependent: port number (e.g. for TCP/IP) 
                                or name ("\\pipe\\pipename" for pipes)
                                    Default: AceServer

      int timeOut               nn_seconds maximum period to wait for service startup and answer

OUTPUT
 return value:
 ACE_HANDLE * pointer to structure containing open connection
              and client identification information
*/

#define serverHandle (lpSvr->hServerBinding)

/* Server Name extra characters for length of null terminated
   server name string of format "\\<host>:<rpc_port>\AceServer" */
#define SN_LEN 14

static DWORD serverTimeOut = 0 ; // in seconds
#define MAXTRIES 3

ACLNT_FUNC_DEF ACE_HANDLE *openServer(  unsigned char *szDatabaseName, 
					unsigned char *szServiceName, 
					unsigned char *szHostName, 
					unsigned char *szProtocol, 
					unsigned char *szEndpoint,
					DWORD dwTimeOut  // in seconds
					)
{
  int length,
      clientId = 0, n,
      magic1, magic3;
  ACE_DATA ace_data ;   
  unsigned char  *answer ;
  long answerLength ;
  ACE_HANDLE *handle ;
  LP_SERVER lpSvr = NULL ;
  RPC_STATUS rpc_status = RPC_S_OK ;
  unsigned long eCode ;
  error_status_t rpcace_error ;
  int nRetry = MAXTRIES ;  // 3 strikes and you're out!

  unsigned char *sztheDatabaseName, 
                *sztheServiceName, 
                *sztheHostName, 
                *sztheProtocol, 
		*sztheEndpoint ;

  unsigned char *szServerStringBinding  = 0 ;
  unsigned char szDefaultDatabaseName[] = { "ACEDB" } ;
  unsigned char szDefaultServiceName[]  = { "aceserver" } ;
  
  // Save the timeout for future reference
  serverTimeOut = dwTimeOut ;

  if( szHostName  && *szHostName )
	  sztheHostName = szHostName ;
  else
	  sztheHostName = NULL ;  // localhost is default  */

  if( szDatabaseName && *szDatabaseName ) 
      sztheDatabaseName = szDatabaseName ; 
  else
      sztheDatabaseName = szDefaultDatabaseName ; 

  if( szServiceName && *szServiceName )
      sztheServiceName = szServiceName ; 
  else
      sztheServiceName = szDefaultServiceName ; 

  if(!client_debug ) 
		/* don't run aceserver as service if in client_debug mode?
	       Attempt to start the service on the specified host
	       Should also succeed if already open! Fail otherwise */
	  if( !startServer(sztheHostName, sztheDatabaseName, sztheServiceName, serverTimeOut) )
		  return NULL ;

  if( szProtocol && *szProtocol ) 
      sztheProtocol = checkProtocol(szProtocol) ;
  else
      /* use the default... 
         rbrusk:  should default eventually be user configurable? */
      sztheProtocol = defaultProtocol() ; /* named pipe */ 

  /* named pipe -- compatibility with given protocol NOT verified */
  if( szEndpoint && *szEndpoint )
      sztheEndpoint = checkEndpoint( sztheServiceName, sztheProtocol, szEndpoint ) ;
  else
      sztheEndpoint = defaultEndpoint(sztheServiceName, sztheProtocol) ;

  RpcTryExcept
  { 
	/* rpcgen code:
		clnt = clnt_create (host, RPC_ACE, RPC_ACE_VERS, "tcp");
		if (!clnt) return NULL ;
	*/

	/* Construct DCE server network address */

	RPC_IF_ID  interfaceID ;
	unsigned char *ObjectUUID ;

	rpc_status = RpcIfInqId( RPC_ACE_v1_2_c_ifspec, &interfaceID ) ;
	if (rpc_status != RPC_S_OK)
	{
		ErrorMsg("DCE openServer()@RpcIfInqId()", rpc_status) ;
		return  NULL ;
	}
	rpc_status = UuidToString( &interfaceID.Uuid, &ObjectUUID );
	if (rpc_status != RPC_S_OK)
	{
		ErrorMsg("DCE openServer()@UuidToString()", rpc_status) ;
		return  NULL ;
	}
	rpc_status = 
		RpcStringBindingCompose(ObjectUUID,
					sztheProtocol,  
					sztheHostName,
					sztheEndpoint,
					NULL,
					&szServerStringBinding ) ;
	RpcStringFree(&ObjectUUID) ;
	if (rpc_status != RPC_S_OK)
	{
		ErrorMsg("DCE openServer(%d)@RpcStringBindingCompose()", rpc_status) ;
		return  NULL ;
	}

	/* The main operation: attempt to establish contact with the server... */
	lpSvr = bindServer(szServerStringBinding, &rpc_status ) ;

	RpcStringFree(&szServerStringBinding) ;

	if(rpc_status)
	{
		ErrorMsg("DCE openServer()@bindServer()", rpc_status) ;
		return NULL  ; /* Non zero binding_status => error */
	}

	/* authenticate */
	ace_data.question = "" ;
	ace_data.clientId = 0;
	ace_data.magic = 0;
	ace_data.aceError = 0;
	ace_data.encore = 0;
	ace_data.kBytes = 0 ;

	nRetry = MAXTRIES ;
	while(nRetry--)
	{
		ace_data.reponse = NULL ;
		ace_server( serverHandle, &ace_data, &rpcace_error) ;
		if( rpcace_error == RPC_S_OK ) break ;

		ErrorMsg("openServer()@ace_server_1a()", rpcace_error) ;

		// Wait locally first, then...
		Sleep(serverTimeOut) ; 

		/* ...set a longer RPC comm timeout */
		RpcMgmtSetComTimeout(serverHandle,RPC_C_BINDING_MAX_TIMEOUT) ; 
	}

	if (rpc_status != RPC_S_OK)
	{
	    ErrorMsg("openServer()@ace_server_1b()",rpcace_error) ;	
            freeServer(lpSvr);
	    return  NULL ;
	}

	rpc_status = copyReponse( &answer, &answerLength, &ace_data.reponse ) ;
	if (rpc_status != RPC_S_OK)
	{
	    ErrorMsg("openServer()@copyReponse_1()", rpc_status ) ;	
	    freeServer(lpSvr);
	    return  NULL ;
	}
  }
  RpcExcept(1) {
		eCode = RpcExceptionCode();
		ErrorMsg("openServer()@exception_1()", eCode ) ;	
		printf("Exception reported during aceserver binding: 0x%lx = %ld\n", eCode, eCode);
		freeServer(lpSvr);
		return  NULL ;
  }
  RpcEndExcept /* bind server*/

  clientId = ace_data.clientId ;
  magic1 = ace_data.magic ;

  if (!clientId) 
  {
        ErrorMsg("openServer(): clientId_1 is NULL?", -1 ) ;	
	freeServer(lpSvr);
	return  NULL ;
  }

  if (ace_data.aceError) 
  {
        ErrorMsg("openServer(): ace_data.aceError_1", ace_data.aceError ) ;	
	freeServer(lpSvr);
	return  NULL ;
  }

  length = answerLength ;

  /*  aceserver authentication */
  RpcTryExcept {
	  if ( answer && length )
	  {
		magic3 = getMagic(magic1, answer) ;

		/* confirm magic by reaccessing client */
		ace_data.question = "";
		ace_data.reponse = NULL ;
		ace_data.clientId = clientId ;
		ace_data.magic = magic3 ;
		ace_data.aceError = 0 ;
		ace_data.encore = 0 ;

		ace_server( serverHandle, &ace_data, &rpcace_error) ;
		if (rpcace_error != RPC_S_OK)
	        {
	            ErrorMsg("openServer()@ace_server_2() authentication error",
                             rpcace_error) ;	
		    freeServer(lpSvr);
		    return  NULL ;
	        }
	        rpc_status = freeReponse( &ace_data.reponse ) ;
		if ( rpc_status != RPC_S_OK ) 
                {
			ErrorMsg("openServer()@freeReponse_2()",
						 rpc_status) ;	
			freeServer(lpSvr) ;
			return  NULL ;
		}
		n = ace_data.clientId;
	  } 
	  else
		n = clientId + 1 ; /* so we fail */

	if (ace_data.aceError) 
	{
		ErrorMsg("openServer(): ace_data.aceError_2", ace_data.aceError ) ;	
		freeServer(lpSvr) ;
		return  NULL ;
	}
	if (n != clientId) 
          {
	  /* authentication failed */
                ErrorMsg("openServer(): clientId_2", n ) ;	
		freeServer(lpSvr) ;
		return  NULL ;
	  }
	
	  /* create mem for handle - malloc OK because DLL in control of free() */
	  if ((handle = (ACE_HANDLE *)malloc(sizeof(ACE_HANDLE))) == NULL) {
		 ace_data.question = "Quit";
		 ace_data.reponse = NULL ;
		 ace_data.clientId = clientId ;
		 ace_data.magic = magic3 ;
		 ace_data.aceError = 0 ;
		 ace_data.encore = 0 ;

		 ace_server( serverHandle, &ace_data, &rpcace_error) ;
		 if (rpcace_error == RPC_S_OK)
	         {
                        freeReponse( &ace_data.reponse ) ;
	         }
                 else
                 {
                        ErrorMsg("openServer()@ace_server_3()",
                                  rpcace_error) ;	
                 }
		 freeServer(lpSvr) ;
		 return  NULL ;
	  }
  }
  RpcExcept(1) {
	eCode = RpcExceptionCode();
	ErrorMsg("openServer()@exception_2()", eCode ) ;	
	printf("Exception reported during aceserver authentication: 0x%lx = %ld\n", eCode, eCode);
  }
  RpcEndExcept /* Server authentication */

  handle->clientId = clientId;
  handle->magic = magic3;
  handle->svr = (void *)lpSvr;

  return handle ;
}

/*************************************************************
notify server of intent to close connection
close connection
free structures

INPUT 
 ACE_HANDLE *  pointer to structure containing open connection
               and client identification information
OUTPUT
none
*/
#define aceServerHandle (((LP_SERVER)(handle->svr))->hServerBinding)

ACLNT_FUNC_DEF void closeServer(ACE_HANDLE *handle)
{
    ACE_DATA ace_data ;
    error_status_t rpcace_error ; 
    unsigned long eCode ;

    /* aceserver closure */
    RpcTryExcept
	{
	   if (handle != NULL)
	   {
		 if ( (LP_SERVER)handle->svr != NULL )
		 { 
			ace_data.question = "Quit";
			ace_data.reponse = NULL ;
			ace_data.clientId = handle->clientId ;
			ace_data.magic = handle->magic ;
			ace_data.aceError = 0 ;
			ace_data.encore = 0 ;
			ace_data.kBytes = 0;

			ace_server( aceServerHandle, &ace_data, &rpcace_error) ;
		        if (rpcace_error == RPC_S_OK)
	                {
    		            freeReponse( &ace_data.reponse ) ;
	                }
                        else
                        {
                            ErrorMsg("closeServer()@ace_server()\n",
                                      rpcace_error) ;	
                        }
			freeServer((LP_SERVER)handle->svr) ;
		 }
		 free((char *)handle);
	   }
    }
    RpcExcept(1)
	{
        eCode = RpcExceptionCode();
		ErrorMsg("closeServer()@exception_1()", eCode ) ;	
        printf("Exception reported during aceserver closure: 0x%lx = %ld\n", eCode, eCode);
    }
    RpcEndExcept /* Server closure */

}

/*************************************************************
transfer request to server, and wait for binary answer
INPUT
 char * request  string containing request
 unsigned char ** answer  ptr to char ptr, that has to be filled with answer
 ACE_HANDLE *    pointer to structure containing open connection
                 and client identification information
 int chunkSize desired size (in kBytes) of returned data-block
            This is only a hint. The server can return more.
            The server splits on ace boundaries

            a chunkSize of 0 allocates a default reponse buffer (64KB in size)

OUTPUT
 unsigned char ** answer  ptr to char ptr. Pointing to allocated memory containing 
                 answer string. This memory will be filled with the 
                 unmodified data handled as binary bytes.
 return value:
 int      error condition
  ESUCCESS  (0)  no error.
  EIO       (5)  no response received from server.
  ENOMEM   (12)  no memory available to store answer.
  or a server generated error 

JC if the server can return both an encore and an aceError at the same time
I'm in trouble. I use only one int return value for both 
*/

#define DEFACLNTT_CHUNK_SIZE 64 

ACLNT_FUNC_DEF int askServerBinary(ACE_HANDLE *handle, char *request, unsigned char **answerPtr, 
								 int *answerLength, int *encorep, int chunkSize) 
{
  long newBytes ;
  static ACE_DATA ace_data ;
  RPC_STATUS  rpc_status = RPC_S_OK ;
  unsigned long eCode ;
  error_status_t rpcace_error ;
  int aceError, encore = 0 ;

  if ( handle == NULL || (LP_SERVER)handle->svr == NULL )
        return (int)(RPC_X_SS_CONTEXT_MISMATCH) ;

  ace_data.question = "" ;
  ace_data.clientId = handle->clientId ;
  ace_data.magic = handle->magic ;
  ace_data.aceError = 0 ;
  ace_data.kBytes = chunkSize ? chunkSize : DEFACLNTT_CHUNK_SIZE ;

  /* check if request contains a local command */
  if (!strncasecmp(request,"encore",6)) 
    {
      /* encore request */
      ace_data.question = "";
      ace_data.encore = WANT_ENCORE;
    } 
  else if (!strncasecmp(request,"noencore",8)) 
    {
      /* encore request */
      ace_data.question = "";
      ace_data.encore = DROP_ENCORE;
    } 
  else if (!strncasecmp(request,"quit",4)) 
    { /* ignore quit request. Must go through closeServer routine */
      *answerLength = 0;
      *answerPtr = NULL;
      return 0;
    } 
  else
    {
      ace_data.encore = 0;
      ace_data.question = request;
    }
  
  if (*encorep == 3) ace_data.encore = -3 ;
  
  /* aceserver RPC query call */
  RpcTryExcept {

	  ace_data.reponse = NULL ;
	  ace_server( aceServerHandle, &ace_data, &rpcace_error) ;
	  if ( rpcace_error != RPC_S_OK )
	  {
                ErrorMsg("askServerBinary()@ace_server()",
                          rpcace_error) ;	
                freeServer((LP_SERVER)handle->svr) ;	/* fatal error: release the server? */
                return (int)(rpcace_error) ;		/* and return the error number? */
	  }
	  rpc_status = copyReponse( answerPtr, answerLength, &ace_data.reponse ) ;

	  /* validity checking of reponse */
	  /* no data was received, return error */
	  if ( (rpcace_error = (error_status_t)rpc_status) != RPC_S_OK )
	  {
              ErrorMsg("askServerBinary()@copyReponse()",
                          rpcace_error) ;	
	      freeServer((LP_SERVER)handle->svr) ;	/* fatal error: release the server? */
	      return (int)(rpcace_error) ;		/* and return the error number? */
	  }

	  /* no answer was received or error, return NULL answer 
	     leave checking for NULL reponse to upper layer */
	  /* store server returned error status. Give this to the client */
	  if( !*answerPtr || !answerLength || ace_data.aceError )
	  {
              ErrorMsg("askServerBinary(): no answer",
                          ace_data.aceError) ;	
	        return ace_data.aceError ;
	  }
  }
  RpcExcept(1) {
	eCode = RpcExceptionCode();
	ErrorMsg("askServerBinary()@exception_1()", eCode ) ;	
	printf("Exception reported during aceserver RPC query call: 0x%lx = %ld\n", eCode, eCode);
  }
  RpcEndExcept /* aceserver RPC query call */
  
  *encorep = ace_data.encore ;
  return ace_data.aceError ? ace_data.aceError : - encore ; /* surcharge pour JD */
}

/***************************************************************
transfer request to server, and wait for binary answer. Convert answer 
to ASCII string

INPUT
 char * request  string containing request
 char ** answer  ptr to char ptr, that has to be filled with answer
 ACE_HANDLE *    pointer to structure containing open connection
                 and client identification information
 int chunkSize desired size (in kBytes) of returned data-block
            This is only a hint. The server can return more.
            The server splits on ace boundaries
            a chunkSize of 0 indicates a request for unbuffered answers

OUTPUT
 char ** answer  ptr to char ptr. Pointing to allocated memory containing 
                 answer string.
 return value:
 int      error condition
  ESUCCESS  (0)  no error.
  EIO       (5)  no response received from server.
  ENOMEM   (12)  no memory available to store answer.
  or a server generated error 
*/

ACLNT_FUNC_DEF int askServer(ACE_HANDLE *handle, char *request, char **answerPtr, int chunkSize) 
{ int length, i, encore ;
  int returnValue;
  unsigned char *binaryAnswer;
  char *answer;
  char *loop;
  
  returnValue = askServerBinary(handle, request, &binaryAnswer, &length, &encore, chunkSize) ;
  if (returnValue <= 0)
    { /* allocate memory for return string */
      /* if memory is more important than speed, we could run
	 through the string first and count the number of '\0''s 
	 and substract this from the memoryblock we allocate */
      if (!length )   /* empty string */
	{ *answerPtr = 0;
	  return returnValue;
	}
#if !defined(WIN32)
      if ((answer = (char *)malloc(length+1)) == NULL) 
	{ free(binaryAnswer);
	  return(ENOMEM);
	}
#else /* defined(WIN32) */
      /* Use messalloc, so invoking application can use messfree()? 
         Crashes if out of memory (no error code returned)? */
      answer = (char *)messalloc(length+1) ; 
#endif  /* defined(WIN32) */
      /* initial step of the copy process */
      loop = (char *)binaryAnswer;
      strcpy(answer,loop);
      i = *loop ? strlen(loop): 0 ;
      loop += i;
      for (;(*loop == '\0')&&(i<length) ;loop++,i++);
      
      for (;i<length;) 
	{ strcat(answer,loop);
	  i += strlen(loop);
	  loop +=  strlen(loop);
	  for (;(*loop == '\0')&&(i<length) ;loop++,i++);
	}
      *(answer+i) = '\0'; /* for safety, make sure the string is terminated */
      free((char *)binaryAnswer);
      *answerPtr = answer;
    }
  return returnValue;
}


 /************** end of file **************/
