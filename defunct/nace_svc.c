/*  $Id: nace_svc.c,v 1.1 1994-08-03 20:02:27 rd Exp $  */
/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#include <stdio.h>
#include <rpc/rpc.h>
#include "nace.h"

static void net_ace_1();

main()
{
	register SVCXPRT *transp;

	(void) pmap_unset(NET_ACE, NET_ACE_VERS);

	transp = svcudp_create(RPC_ANYSOCK);
	if (transp == NULL) {
		fprintf(stderr, "cannot create udp service.");
		exit(1);
	}
	if (!svc_register(transp, NET_ACE, NET_ACE_VERS, net_ace_1, IPPROTO_UDP)) {
		fprintf(stderr, "unable to register (NET_ACE, NET_ACE_VERS, udp).");
		exit(1);
	}

	transp = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (transp == NULL) {
		fprintf(stderr, "cannot create tcp service.");
		exit(1);
	}
	if (!svc_register(transp, NET_ACE, NET_ACE_VERS, net_ace_1, IPPROTO_TCP)) {
		fprintf(stderr, "unable to register (NET_ACE, NET_ACE_VERS, tcp).");
		exit(1);
	}

	svc_run();
	fprintf(stderr, "svc_run returned");
	exit(1);
	/* NOTREACHED */
}

static void
net_ace_1(rqstp, transp)
	struct svc_req *rqstp;
	register SVCXPRT *transp;
{
	union {
		net_data ace_server_1_arg;
	} argument;
	char *result;
	bool_t (*xdr_argument)(), (*xdr_result)();
	char *(*local)();

	switch (rqstp->rq_proc) {
	case NULLPROC:
		(void) svc_sendreply(transp, xdr_void, (char *)NULL);
		return;

	case ACE_SERVER:
		xdr_argument = xdr_net_data;
		xdr_result = xdr_ace_server_res;
		local = (char *(*)()) ace_server_1;
		break;

	default:
		svcerr_noproc(transp);
		return;
	}
	bzero((char *)&argument, sizeof(argument));
	if (!svc_getargs(transp, xdr_argument, &argument)) {
		svcerr_decode(transp);
		return;
	}
	result = (*local)(&argument, rqstp);
	if (result != NULL && !svc_sendreply(transp, xdr_result, result)) {
		svcerr_systemerr(transp);
	}
	if (!svc_freeargs(transp, xdr_argument, &argument)) {
		fprintf(stderr, "unable to free arguments");
		exit(1);
	}
	return;
}
