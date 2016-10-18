/* $Id: rpcace.h,v 1.1 1994-11-21 07:49:23 mieg Exp $ */
/*  Last edited: Nov 21 01:47 1994 (mieg) */
/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#ifndef _RPCACE_H_RPCGEN
#define	_RPCACE_H_RPCGEN

#include <rpc/rpc.h>

struct ace_data {
	char *question;
	struct {
		u_int reponse_len;
		char *reponse_val;
	} reponse;
	int clientId;
};
typedef struct ace_data ace_data;

struct ace_reponse {
	int errno;
	union {
		ace_data res_data;
	} ace_reponse_u;
};
typedef struct ace_reponse ace_reponse;

#define	RPC_ACE ((unsigned long)(0x20000101))
#define	RPC_ACE_VERS ((unsigned long)(1))
#define	ACE_SERVER ((unsigned long)(1))
extern  ace_reponse * ace_server_1();
extern int rpc_ace_1_freeresult();

/* the xdr functions */
extern bool_t xdr_ace_data();
extern bool_t xdr_ace_reponse();

#endif /* !_RPCACE_H_RPCGEN */
