#ifndef __CLISRV_H__
#define __CLISRV_H__

#define CLI_SRV_SOCK "/tmp/clisrv.sock"

struct cli_service_priv {
	void *libev_magic;
	int cli_service_sock;
	int cli_client;
};

#endif
