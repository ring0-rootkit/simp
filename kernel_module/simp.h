#include <linux/kernel.h>
#include <net/inet_sock.h>
#include <linux/in.h>

#define AF_SIMP 5

struct simp_sock {
	struct inet_sock isk;
	// more params to be done
};

struct sockaddr_s {
	sa_family_t ss_family; /* Socket SIMP family */
	__be16 ss_port;
	struct in_addr ss_addr;
};
