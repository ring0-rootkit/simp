#include <linux/kernel.h>
#include <net/inet_sock.h>
#include <linux/in.h>

#define AF_GRTP 5

struct grtp_sock {
	struct inet_sock isk;
	// more params to be done
};

struct sockaddr_g {
	sa_family_t sg_family;
	__be16 sg_port;
	struct in_addr sg_addr;
};
