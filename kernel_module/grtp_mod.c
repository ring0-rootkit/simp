#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>	
#include <net/inet_sock.h>
#include <net/sock.h>
#include <linux/rhashtable.h>

#include "grtp_mod.h"

struct grtp_port {
	__be16 port;
	struct rhash_head node; /* for hash table */
};
static struct rhashtable grtp_port_table;
static DEFINE_MUTEX(grtp_port_mutex);

static struct proto grtp_proto;
static const struct proto_ops grtp_proto_ops __read_mostly;
static const struct net_proto_family grtp_family;

static int grtp_port_table_init(void)
{
	struct rhashtable_params params = {
		.key_len = sizeof(__be16),
		.key_offset = offsetof(struct grtp_port, port),
		.head_offset = offsetof(struct grtp_port, node),
		.automatic_shrinking = true,
	};
	
	return rhashtable_init(&grtp_port_table, &params);
}

static int grtp_allocate_port(__be16 port)
{
    struct grtp_port *new_port;
    int ret;

    new_port = kmalloc(sizeof(struct grtp_port), GFP_KERNEL);
    if (!new_port)
        return -ENOMEM;

    new_port->port = port;

    mutex_lock(&grtp_port_mutex);
    ret = rhashtable_lookup_insert_fast(&grtp_port_table, &new_port->node,
                                        grtp_port_table.p);
    mutex_unlock(&grtp_port_mutex);

    if (ret) {
        kfree(new_port);
        return -EADDRINUSE;
    }

    return 0;
}

static void grtp_release_port(__be16 port)
{
    struct grtp_port *port_entry;
    __be16 search_port = port;

    mutex_lock(&grtp_port_mutex);
    port_entry = rhashtable_lookup_fast(&grtp_port_table, &search_port,
                                       grtp_port_table.p);
    if (port_entry) {
        rhashtable_remove_fast(&grtp_port_table, &port_entry->node,
                               grtp_port_table.p);
        kfree(port_entry);
    }
    mutex_unlock(&grtp_port_mutex);
}

static int grtp_create_socket(struct net *net, struct socket *sock, int protocol,
		      int kern)
{
	struct sock *sk;

	sk = sk_alloc(net, AF_GRTP, GFP_KERNEL, &grtp_proto, 1);
	if (!sk) {
		printk(KERN_ERR "grtp:failed to allocate socket.\n");
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
	struct sockaddr_g *gaddr;
	
	printk(KERN_INFO "grtp: grtp_release\n");

	struct sock *sk = sock->sk;

	if(!sk)
		return 0;

	gaddr = (struct sockaddr_g *)sk->sk_user_data;
	if (gaddr) {
		printk(KERN_INFO "grtp: releasing address %pI4, port %d\n",
			       &gaddr->sg_addr, ntohs(gaddr->sg_port));
		grtp_release_port(gaddr->sg_port);
		kfree(gaddr);
		sk->sk_user_data = NULL;
	}

	sock->sk = NULL;
	sock_put(sk);

	return 0;
}

static int grtp_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in *addr = (struct sockaddr_in *) uaddr;
	struct sockaddr_g *gaddr;
	int ret;

	printk(KERN_INFO "grtp: grtp_bind\n");

	if (addr_len < sizeof(struct sockaddr_in)) {
		printk(KERN_ERR "grtp: invalid address length\n");
		return -EINVAL;
	}

	if (addr->sin_family != AF_GRTP) {
		printk(KERN_ERR "grtp: invalid address family provided\n");
		return -EAFNOSUPPORT;
	}

	ret = grtp_allocate_port(addr->sin_port);
	if (ret) {
		printk(KERN_ERR "grtp: port allocation failed\n");
		return ret;
	}

	gaddr = kmalloc(sizeof(struct sockaddr_g), GFP_KERNEL);
	if (!gaddr) {
		printk(KERN_ERR "grtp: failed to allocate memory for address\n");
		return -ENOMEM;
	}

	gaddr->sg_family = addr->sin_family;
	gaddr->sg_port = addr->sin_port;
	gaddr->sg_addr = addr->sin_addr;

	sock->sk->sk_user_data = gaddr;

	printk(KERN_INFO "grtp: bound to address %pI4, port %d\n",
			&gaddr->sg_addr, ntohs(gaddr->sg_port));

	return 0;
}

// static int grtp_connect(struct socket* sock, struct sockaddr* uaddr,
// 			int alen, int flags)
// {
// 	printk(KERN_INFO "grtp: grtp_connect");
// 	return 0;
// }
//
// static int grtp_accept(struct socket *sock, struct socket *newsock,
// 		       struct proto_accept_arg *arg)
// {
// 	printk(KERN_INFO "grtp: grtp_accept");
// 	return 0;
// }
//
// static int grtp_getname(struct socket *sock, struct sockaddr *uaddr, int peer)
// {
// 	printk(KERN_INFO "grtp: grtp_getname");
// 	return 0;
// }
//
// static int grtp_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
// {
// 	printk(KERN_INFO "grtp: grtp_ioctl");
// 	return 0;
// }
//
// static int grtp_listen(struct socket *sock, int backlog)
// {
// 	printk(KERN_INFO "grtp: grtp_listen");
// 	return 0;
// }

static int grtp_shutdown(struct socket *sock, int mode)
{
	printk(KERN_INFO "grtp: grtp_shutdown");
	return 0;
}

// static int grtp_sendmsg(struct socket *sock, struct msghdr *msg,
// 			       size_t len)
// {
// 	printk(KERN_INFO "grtp: grtp_sendmsg");
// 	return 0;
// }
//
//
// static int grtp_recvmsg(struct socket *sock, struct msghdr *msg,
// 			       size_t size, int flags)
// {
// 	printk(KERN_INFO "grtp: grtp_recvmsg");
// 	return 0;
// }
//
// static int grtp_read_skb(struct sock *sk, skb_read_actor_t recv_actor)
// {
// 	printk(KERN_INFO "grtp: grtp_read_skb");
// 	return 0;
// }

static void grtp_show_fdinfo(struct seq_file *m, struct socket *sock)
{
    struct sock *sk = sock->sk;
    struct sockaddr_g *gaddr;

    printk(KERN_INFO "grtp: grtp_show_fdinfo called\n");

    if (!sk)
        return;

    gaddr = (struct sockaddr_g *)sk->sk_user_data;
    if (gaddr) {
        seq_printf(m, "GRTP Address: %pI4\n", &gaddr->sg_addr);
        seq_printf(m, "GRTP Port: %d\n", ntohs(gaddr->sg_port));
    } else {
        seq_printf(m, "GRTP Address: Not bound\n");
    }

    seq_printf(m, "GRTP State: %s\n", sk->sk_state == TCP_ESTABLISHED ? "Connected" : "Not Connected");
}

static __poll_t grtp_poll(struct file *file, struct socket *sock, poll_table *wait)
{
    struct sock *sk = sock->sk;
    __poll_t mask = 0;

    printk(KERN_INFO "grtp: grtp_poll called\n");

    if (!sk)
        return 0;

    poll_wait(file, sk_sleep(sk), wait);

    if (!skb_queue_empty(&sk->sk_receive_queue))
        mask |= EPOLLIN | EPOLLRDNORM;

    if (sock_writeable(sk))
        mask |= EPOLLOUT | EPOLLWRNORM;

    if (sk->sk_err || !skb_queue_empty(&sk->sk_error_queue))
        mask |= EPOLLERR; // Socket has an error

    return mask;
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
		printk(KERN_WARNING "grtp: cannot register proto\n");
		return 1;
	}

	err = sock_register(&grtp_family);
	if (err < 0) {
		printk(KERN_WARNING "grtp: cannot register grtp_family\n");
		goto proto_unreg;
	}

	grtp_port_table_init();

	printk(KERN_INFO "grtp: GRTP initialized\n");
	return 0;

proto_unreg:
	proto_unregister(&grtp_proto);
	return err;

}

static void __exit grtp_exit(void)
{
  printk(KERN_INFO "grtp: GRTP deinitialized\n");
  sock_unregister(AF_GRTP);
  proto_unregister(&grtp_proto);
  rhashtable_destroy(&grtp_port_table);
}

static const struct proto_ops grtp_proto_ops __read_mostly = {
	.family =       AF_GRTP,
	.owner =        THIS_MODULE,
	.release =	grtp_release,
	.bind =		grtp_bind,
	.connect =	sock_no_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	sock_no_accept,
	.getname =	sock_no_getname,
	.poll =		grtp_poll,
	.ioctl =	sock_no_ioctl,
	.listen =	sock_no_listen,
	.shutdown =	grtp_shutdown,
	.sendmsg =	sock_no_sendmsg,
	.recvmsg =	sock_no_recvmsg,
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
