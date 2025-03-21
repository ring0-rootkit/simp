#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>	
#include <net/inet_sock.h>
#include <net/sock.h>

#include "grtp_mod.h"

static struct proto grtp_proto;
static const struct proto_ops grtp_proto_ops __read_mostly;
static const struct net_proto_family grtp_family;

static int grtp_create_socket(struct net *net, struct socket *sock, int protocol,
		      int kern)
{
	struct sock *sk;

	sk = sk_alloc(net, AF_GRTP, GFP_KERNEL, &grtp_proto, 1);
	if (!sk) {
		printk("failed to allocate socket.\n");
		return -ENOMEM;
	}

	sock_init_data(sock, sk);
	sk->sk_protocol = 0xfe;

	sock->ops = &grtp_proto_ops;
	sock->state = SS_UNCONNECTED;

	return 0;
};

static int grtp_release(struct socket *sock)
{
	printk(KERN_INFO "grtp_release\n");

	struct sock *sk = sock->sk;

	if(!sk)
		return 0;

	sk->sk_prot->close(sk, 0);
	sock->sk = NULL;

	return 0;
}

static int grtp_bind(struct socket *sock, struct sockaddr *gaddr, int addr_len)
{
	printk(KERN_INFO "grtp_bind\n");
	return 0;
}

static int grtp_connect(struct socket* sock, struct sockaddr* gaddr,
			int alen, int flags)
{
	printk(KERN_INFO "grtp_connect");
	return 0;
}

static int grtp_accept(struct socket *sock, struct socket *newsock,
		       struct proto_accept_arg *arg)
{
	printk(KERN_INFO "grtp_accept");
	return 0;
}

static int grtp_getname(struct socket *sock, struct sockaddr *gaddr, int peer)
{
	printk(KERN_INFO "grtp_getname");
	return 0;
}

static int grtp_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	printk(KERN_INFO "grtp_ioctl");
	return 0;
}

static int grtp_listen(struct socket *sock, int backlog)
{
	printk(KERN_INFO "grtp_listen");
	return 0;
}

static int grtp_shutdown(struct socket *sock, int mode)
{
	printk(KERN_INFO "grtp_shutdown");
	return 0;
}

static int grtp_sendmsg(struct socket *sock, struct msghdr *msg,
			       size_t len)
{
	printk(KERN_INFO "grtp_sendmsg");
	return 0;
}


static int grtp_recvmsg(struct socket *sock, struct msghdr *msg,
			       size_t size, int flags)
{
	printk(KERN_INFO "grtp_recvmsg");
	return 0;
}

static int grtp_read_skb(struct sock *sk, skb_read_actor_t recv_actor)
{
	printk(KERN_INFO "grtp_read_skb");
	return 0;
}

static void grtp_show_fdinfo(struct seq_file *m, struct socket *sock)
{
	printk(KERN_INFO "grtp_show_fdinfo");
	return;
}

static __poll_t grtp_poll(struct file *file, struct socket *sock, poll_table *wait)
{
	printk(KERN_INFO "grtp_poll");
	return 0;
}

static void grtp_close(struct sock *sk, long timeout)
{
	/* Nothing to do here, grtp does not need a ->unhash().
	 * This is merely for sockmap.
	 */
}

static int __init grtp_init(void)
{
	int err;

	err = proto_register(&grtp_proto, 1);
	if (err) {
		printk(KERN_WARNING "cannot register proto\n");
		return 1;
	}
	sock_unregister(AF_GRTP);
	err = sock_register(&grtp_family);
	if (err < 0) {
		printk(KERN_WARNING "cannot register grtp_family\n");
		return 1;
	}

	printk(KERN_INFO "GRTP initialized\n");
	return 0;
}

static void __exit grtp_exit(void)
{
  sock_unregister(AF_GRTP);
  printk(KERN_INFO "GRTP deinitialized\n");
}

static const struct proto_ops grtp_proto_ops __read_mostly = {
	.family =       AF_GRTP,
	.owner =        THIS_MODULE,
	.release =	grtp_release,
	.bind =		grtp_bind,
	.connect =	grtp_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	grtp_accept,
	.getname =	grtp_getname,
	.poll =		grtp_poll,
	.ioctl =	grtp_ioctl,
	.listen =	grtp_listen,
	.shutdown =	grtp_shutdown,
	.sendmsg =	grtp_sendmsg,
	.recvmsg =	grtp_recvmsg,
	.read_skb =	grtp_read_skb,
	.mmap =		sock_no_mmap,
	.set_peek_off =	sk_set_peek_off,
	.show_fdinfo =	grtp_show_fdinfo,
};

static struct proto grtp_proto = {
	.obj_size = sizeof(struct grtp_sock),
	.owner = THIS_MODULE,
	.name = "GRTP",
	.close = grtp_close,
};

static const struct net_proto_family grtp_family = {
	.family = AF_GRTP,
	.create = grtp_create_socket,
	.owner = THIS_MODULE,
};

module_init(grtp_init);
module_exit(grtp_exit);

MODULE_LICENSE("GPL");
