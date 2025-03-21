#include <linux/kernel.h>
#include <net/inet_sock.h>

#define AF_GRTP 5

struct grtp_sock {
	struct inet_sock isk;
	// more params to be done
};
