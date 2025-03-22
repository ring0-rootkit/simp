#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>	
#include <net/inet_sock.h>
#include <net/sock.h>
#include <linux/rhashtable.h>

#include "simp.h"

struct simp_port {
	__be16 port;
	struct rhash_head node; /* for hash table */
};
static struct rhashtable simp_port_table;
static DEFINE_MUTEX(simp_port_mutex);

static struct proto simp_proto;
static const struct proto_ops simp_proto_ops __read_mostly;
static const struct net_proto_family simp_family;

static int simp_port_table_init(void)
{
	struct rhashtable_params params = {
		.key_len = sizeof(__be16),
		.key_offset = offsetof(struct simp_port, port),
		.head_offset = offsetof(struct simp_port, node),
		.automatic_shrinking = true,
	};
	
	return rhashtable_init(&simp_port_table, &params);
}

static int simp_allocate_port(__be16 port)
{
    struct simp_port *new_port;
    int ret;

    new_port = kmalloc(sizeof(struct simp_port), GFP_KERNEL);
    if (!new_port)
        return -ENOMEM;

    new_port->port = port;

    mutex_lock(&simp_port_mutex);
    ret = rhashtable_lookup_insert_fast(&simp_port_table, &new_port->node,
                                        simp_port_table.p);
    mutex_unlock(&simp_port_mutex);

    if (ret) {
        kfree(new_port);
        return -EADDRINUSE;
    }

    return 0;
}

static void simp_release_port(__be16 port)
{
    struct simp_port *port_entry;
    __be16 search_port = port;

    mutex_lock(&simp_port_mutex);
    port_entry = rhashtable_lookup_fast(&simp_port_table, &search_port,
                                       simp_port_table.p);
    if (port_entry) {
        rhashtable_remove_fast(&simp_port_table, &port_entry->node,
                               simp_port_table.p);
        kfree(port_entry);
    }
    mutex_unlock(&simp_port_mutex);
}

static int simp_create_socket(struct net *net, struct socket *sock, int protocol,
		      int kern)
{
	struct sock *sk;

	sk = sk_alloc(net, AF_SIMP, GFP_KERNEL, &simp_proto, 1);
	if (!sk) {
		printk(KERN_ERR "simp:failed to allocate socket.\n");
		return -ENOMEM;
	}

	sock_init_data(sock, sk);
	sk->sk_protocol = 0xfe;

	sock->ops = &simp_proto_ops;
	sock->state = SS_UNCONNECTED;

	return 0;
};

static int simp_release(struct socket *sock)
{
	struct sockaddr_s *saddr;
	
	printk(KERN_INFO "simp: simp_release\n");

	struct sock *sk = sock->sk;

	if(!sk)
		return 0;

	saddr = (struct sockaddr_s *)sk->sk_user_data;
	if (saddr) {
		printk(KERN_INFO "simp: releasing address %pI4, port %d\n",
			       &saddr->ss_addr, ntohs(saddr->ss_port));
		simp_release_port(saddr->ss_port);
		kfree(saddr);
		sk->sk_user_data = NULL;
	}

	sock->sk = NULL;
	sock_put(sk);

	return 0;
}

static int simp_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in *addr = (struct sockaddr_in *) uaddr;
	struct sockaddr_s *saddr;
	int ret;

	printk(KERN_INFO "simp: simp_bind\n");

	if (addr_len < sizeof(struct sockaddr_in)) {
		printk(KERN_ERR "simp: invalid address length\n");
		return -EINVAL;
	}

	if (addr->sin_family != AF_SIMP) {
		printk(KERN_ERR "simp: invalid address family provided\n");
		return -EAFNOSUPPORT;
	}

	ret = simp_allocate_port(addr->sin_port);
	if (ret) {
		printk(KERN_ERR "simp: port allocation failed\n");
		return ret;
	}

	saddr = kmalloc(sizeof(struct sockaddr_s), GFP_KERNEL);
	if (!saddr) {
		printk(KERN_ERR "simp: failed to allocate memory for address\n");
		return -ENOMEM;
	}

	saddr->ss_family = addr->sin_family;
	saddr->ss_port = addr->sin_port;
	saddr->ss_addr = addr->sin_addr;

	sock->sk->sk_user_data = saddr;

	printk(KERN_INFO "simp: bound to address %pI4, port %d\n",
			&saddr->ss_addr, ntohs(saddr->ss_port));

	return 0;
}

// static int simp_connect(struct socket* sock, struct sockaddr* uaddr,
// 			int alen, int flags)
// {
// 	printk(KERN_INFO "simp: simp_connect");
// 	return 0;
// }
//
// static int simp_accept(struct socket *sock, struct socket *newsock,
// 		       struct proto_accept_arg *arg)
// {
// 	printk(KERN_INFO "simp: simp_accept");
// 	return 0;
// }
//
// static int simp_getname(struct socket *sock, struct sockaddr *uaddr, int peer)
// {
// 	printk(KERN_INFO "simp: simp_getname");
// 	return 0;
// }
//
// static int simp_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
// {
// 	printk(KERN_INFO "simp: simp_ioctl");
// 	return 0;
// }
//
// static int simp_listen(struct socket *sock, int backlog)
// {
// 	printk(KERN_INFO "simp: simp_listen");
// 	return 0;
// }

static int simp_shutdown(struct socket *sock, int mode)
{
	printk(KERN_INFO "simp: simp_shutdown");
	return 0;
}

// static int simp_sendmsg(struct socket *sock, struct msghdr *msg,
// 			       size_t len)
// {
// 	printk(KERN_INFO "simp: simp_sendmsg");
// 	return 0;
// }
//
//
// static int simp_recvmsg(struct socket *sock, struct msghdr *msg,
// 			       size_t size, int flags)
// {
// 	printk(KERN_INFO "simp: simp_recvmsg");
// 	return 0;
// }
//
// static int simp_read_skb(struct sock *sk, skb_read_actor_t recv_actor)
// {
// 	printk(KERN_INFO "simp: simp_read_skb");
// 	return 0;
// }

static void simp_show_fdinfo(struct seq_file *m, struct socket *sock)
{
    struct sock *sk = sock->sk;
    struct sockaddr_s *saddr;

    printk(KERN_INFO "simp: simp_show_fdinfo called\n");

    if (!sk)
        return;

    saddr = (struct sockaddr_s *)sk->sk_user_data;
    if (saddr) {
        seq_printf(m, "simp Address: %pI4\n", &saddr->ss_addr);
        seq_printf(m, "simp Port: %d\n", ntohs(saddr->ss_port));
    } else {
        seq_printf(m, "simp Address: Not bound\n");
    }

    seq_printf(m, "simp State: %s\n", sk->sk_state == TCP_ESTABLISHED ? "Connected" : "Not Connected");
}

static __poll_t simp_poll(struct file *file, struct socket *sock, poll_table *wait)
{
    struct sock *sk = sock->sk;
    __poll_t mask = 0;

    printk(KERN_INFO "simp: simp_poll called\n");

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

static void simp_close(struct sock *sk, long timeout)
{
	/* Nothing to do here, simp does not need a ->unhash().
	 * This is merely for sockmap.
	 */
}

static int __init simp_init(void)
{
	int err;

	err = proto_register(&simp_proto, 1);
	if (err) {
		printk(KERN_WARNING "simp: cannot register proto\n");
		return 1;
	}

	err = sock_register(&simp_family);
	if (err < 0) {
		printk(KERN_WARNING "simp: cannot register simp_family\n");
		goto proto_unreg;
	}

	simp_port_table_init();

	printk(KERN_INFO "simp: simp initialized\n");
	return 0;

proto_unreg:
	proto_unregister(&simp_proto);
	return err;

}

static void __exit simp_exit(void)
{
  printk(KERN_INFO "simp: simp deinitialized\n");
  sock_unregister(AF_SIMP);
  proto_unregister(&simp_proto);
  rhashtable_destroy(&simp_port_table);
}

static const struct proto_ops simp_proto_ops __read_mostly = {
	.family =       AF_SIMP,
	.owner =        THIS_MODULE,
	.release =	simp_release,
	.bind =		simp_bind,
	.connect =	sock_no_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	sock_no_accept,
	.getname =	sock_no_getname,
	.poll =		simp_poll,
	.ioctl =	sock_no_ioctl,
	.listen =	sock_no_listen,
	.shutdown =	simp_shutdown,
	.sendmsg =	sock_no_sendmsg,
	.recvmsg =	sock_no_recvmsg,
	.mmap =		sock_no_mmap,
	.set_peek_off =	sk_set_peek_off,
	.show_fdinfo =	simp_show_fdinfo,
};

static struct proto simp_proto = {
	.obj_size = sizeof(struct simp_sock),
	.owner = THIS_MODULE,
	.name = "simp",
	.close = simp_close,
};

static const struct net_proto_family simp_family = {
	.family = AF_SIMP,
	.create = simp_create_socket,
	.owner = THIS_MODULE,
};

module_init(simp_init);
module_exit(simp_exit);

MODULE_LICENSE("GPL");
