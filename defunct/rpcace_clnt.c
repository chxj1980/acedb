/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#include "rpcace.h"

/* Default timeout can be changed using clnt_control() */
static struct timeval TIMEOUT = { 25, 0 };

ace_reponse *
ace_server_1(argp, clnt)
	ace_data *argp;
	CLIENT *clnt;
{
	static ace_reponse clnt_res;

	memset((char *)&clnt_res, 0, sizeof (clnt_res));
	if (clnt_call(clnt, ACE_SERVER,
		(xdrproc_t) xdr_ace_data, (caddr_t) argp,
		(xdrproc_t) xdr_ace_reponse, (caddr_t) &clnt_res,
		TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&clnt_res);
}
