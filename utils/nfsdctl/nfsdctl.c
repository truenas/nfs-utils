#define _GNU_SOURCE 1

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <linux/module.h>
#include <linux/version.h>
#include <netlink/genl/genl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sched.h>
#include <sys/queue.h>

#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include <linux/netlink.h>

#include <readline/readline.h>
#include <readline/history.h>

#ifdef USE_SYSTEM_NFSD_NETLINK_H
#include <linux/nfsd_netlink.h>
#else
#include "nfsd_netlink.h"
#endif

#include "nfsdctl.h"
#include "conffile.h"
#include "xlog.h"

/* compile note:
 * gcc -I/usr/include/libnl3/ -o <prog-name> <prog-name>.c -lnl-3 -lnl-genl-3
 */

static int debug_level;

struct nfs_version {
	uint8_t	major;
	uint8_t	minor;
	bool enabled;
};

/*
 * The NFS server should only have around 5 versions or so, so we don't bother
 * with memory allocation here, and just use a global array.
 */
#define MAX_NFS_VERSIONS	16

struct nfs_version nfsd_versions[MAX_NFS_VERSIONS];

/*
 * All of the existing netids are short strings (3-4 chars), but let's allow
 * for up to 16.
 */
#define MAX_CLASS_NAME_LEN	16

struct server_socket {
	struct sockaddr_storage	ss;
	char name[MAX_CLASS_NAME_LEN];
	bool active;
};

#define MAX_NFSD_SOCKETS		256

int nfsd_socket_count;
struct server_socket nfsd_sockets[MAX_NFSD_SOCKETS];

const char *taskname;

static const struct option help_only_options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ },
};

#define NFSD4_OPS_MAX_LEN	sizeof(nfsd4_ops) / sizeof(nfsd4_ops[0])
static const char *nfsd4_ops[] = {
	[OP_ACCESS]		= "OP_ACCESS",
	[OP_CLOSE]		= "OP_CLOSE",
	[OP_COMMIT]		= "OP_COMMIT",
	[OP_CREATE]		= "OP_CREATE",
	[OP_DELEGRETURN]	= "OP_DELEGRETURN",
	[OP_GETATTR]		= "OP_GETATTR",
	[OP_GETFH]		= "OP_GETFH",
	[OP_LINK]		= "OP_LINK",
	[OP_LOCK]		= "OP_LOCK",
	[OP_LOCKT]		= "OP_LOCKT",
	[OP_LOCKU]		= "OP_LOCKU",
	[OP_LOOKUP]		= "OP_LOOKUP",
	[OP_LOOKUPP]		= "OP_LOOKUPP",
	[OP_NVERIFY]		= "OP_NVERIFY",
	[OP_OPEN]		= "OP_OPEN",
	[OP_OPEN_CONFIRM]	= "OP_OPEN_CONFIRM",
	[OP_OPEN_DOWNGRADE]	= "OP_OPEN_DOWNGRADE",
	[OP_PUTFH]		= "OP_PUTFH",
	[OP_PUTPUBFH]		= "OP_PUTPUBFH",
	[OP_PUTROOTFH]		= "OP_PUTROOTFH",
	[OP_READ]		= "OP_READ",
	[OP_READDIR]		= "OP_READDIR",
	[OP_READLINK]		= "OP_READLINK",
	[OP_REMOVE]		= "OP_REMOVE",
	[OP_RENAME]		= "OP_RENAME",
	[OP_RENEW]		= "OP_RENEW",
	[OP_RESTOREFH]		= "OP_RESTOREFH",
	[OP_SAVEFH]		= "OP_SAVEFH",
	[OP_SECINFO]		= "OP_SECINFO",
	[OP_SETATTR]		= "OP_SETATTR",
	[OP_SETCLIENTID]	= "OP_SETCLIENTID",
	[OP_SETCLIENTID_CONFIRM] = "OP_SETCLIENTID_CONFIRM",
	[OP_VERIFY]		= "OP_VERIFY",
	[OP_WRITE]		= "OP_WRITE",
	[OP_RELEASE_LOCKOWNER]	= "OP_RELEASE_LOCKOWNER",
	/* NFSv4.1 operations */
	[OP_EXCHANGE_ID]	= "OP_EXCHANGE_ID",
	[OP_BACKCHANNEL_CTL]	= "OP_BACKCHANNEL_CTL",
	[OP_BIND_CONN_TO_SESSION] = "OP_BIND_CONN_TO_SESSION",
	[OP_CREATE_SESSION]	= "OP_CREATE_SESSION",
	[OP_DESTROY_SESSION]	= "OP_DESTROY_SESSION",
	[OP_SEQUENCE]		= "OP_SEQUENCE",
	[OP_DESTROY_CLIENTID]	= "OP_DESTROY_CLIENTID",
	[OP_RECLAIM_COMPLETE]	= "OP_RECLAIM_COMPLETE",
	[OP_SECINFO_NO_NAME]	= "OP_SECINFO_NO_NAME",
	[OP_TEST_STATEID]	= "OP_TEST_STATEID",
	[OP_FREE_STATEID]	= "OP_FREE_STATEID",
	[OP_GETDEVICEINFO]	= "OP_GETDEVICEINFO",
	[OP_LAYOUTGET]		= "OP_LAYOUTGET",
	[OP_LAYOUTCOMMIT]	= "OP_LAYOUTCOMMIT",
	[OP_LAYOUTRETURN]	= "OP_LAYOUTRETURN",
	/* NFSv4.2 operations */
	[OP_ALLOCATE]		= "OP_ALLOCATE",
	[OP_DEALLOCATE]		= "OP_DEALLOCATE",
	[OP_CLONE]		= "OP_CLONE",
	[OP_COPY]		= "OP_COPY",
	[OP_READ_PLUS]		= "OP_READ_PLUS",
	[OP_SEEK]		= "OP_SEEK",
	[OP_OFFLOAD_STATUS]	= "OP_OFFLOAD_STATUS",
	[OP_OFFLOAD_CANCEL]	= "OP_OFFLOAD_CANCEL",
	[OP_COPY_NOTIFY]	= "OP_COPY_NOTIFY",
	[OP_GETXATTR]		= "OP_GETXATTR",
	[OP_SETXATTR]		= "OP_SETXATTR",
	[OP_LISTXATTRS]		= "OP_LISTXATTRS",
	[OP_REMOVEXATTR]	= "OP_REMOVEXATTR",
};

static int error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err,
			 void *arg)
{
	int *ret = arg;

	*ret = err->error;
	return NL_SKIP;
}

static int finish_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;

	*ret = 0;
	return NL_SKIP;
}

static int ack_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;

	*ret = 0;
	return NL_STOP;
}

static void parse_rpc_status_get(struct genlmsghdr *gnlh)
{
	struct nlattr *attr;
	int rem;

	nla_for_each_attr(attr, genlmsg_attrdata(gnlh, 0),
			  genlmsg_attrlen(gnlh, 0), rem) {
		switch (nla_type(attr)) {
		case NFSD_A_RPC_STATUS_XID:
		case NFSD_A_RPC_STATUS_FLAGS:
			printf(" 0x%08x", nla_get_u32(attr));
			break;
		case NFSD_A_RPC_STATUS_PROC:
		case NFSD_A_RPC_STATUS_PROG:
			printf(" %d", nla_get_u32(attr));
			break;
		case NFSD_A_RPC_STATUS_VERSION:
			printf(" NFS%d", nla_get_u8(attr));
			break;
		case NFSD_A_RPC_STATUS_SERVICE_TIME:
			printf(" %ld", nla_get_u64(attr));
			break;
		case NFSD_A_RPC_STATUS_DADDR4:
		case NFSD_A_RPC_STATUS_SADDR4: {
			struct in_addr addr = {
				.s_addr = nla_get_u32(attr),
			};

			printf(" %s", inet_ntoa(addr));
			break;
		}
		case NFSD_A_RPC_STATUS_DPORT:
		case NFSD_A_RPC_STATUS_SPORT:
			printf(" %hu", nla_get_u16(attr));
			break;
		case NFSD_A_RPC_STATUS_COMPOUND_OPS: {
			unsigned int op = nla_get_u32(attr);

			if (op < NFSD4_OPS_MAX_LEN)
				printf(" %s", nfsd4_ops[op]);
			break;
		}
		default:
			break;
		}
	}
	printf("\n");
}

static void parse_version_get(struct genlmsghdr *gnlh)
{
	struct nlattr *attr;
	int rem, idx = 0;

	/* clear the nfsd_versions array */
	memset(nfsd_versions, '\0', sizeof(*nfsd_versions) * MAX_NFS_VERSIONS);

	nla_for_each_attr(attr, genlmsg_attrdata(gnlh, 0),
			  genlmsg_attrlen(gnlh, 0), rem) {
		struct nlattr *a;
		int i;

		nla_for_each_nested(a, attr, i) {
			switch (nla_type(a)) {
			case NFSD_A_VERSION_MAJOR:
				nfsd_versions[idx].major = nla_get_u32(a);
				break;
			case NFSD_A_VERSION_MINOR:
				nfsd_versions[idx].minor = nla_get_u32(a);
				break;
			case NFSD_A_VERSION_ENABLED:
				nfsd_versions[idx].enabled = nla_get_flag(a);
				break;
			default:
				break;
			}
		}
		++idx;
	}
}

static void parse_listener_get(struct genlmsghdr *gnlh)
{
	struct nlattr *attr;
	int rem, idx = 0;

	/* clear the nfsd_sockets array */
	memset(nfsd_sockets, '\0', sizeof(*nfsd_sockets) * MAX_NFSD_SOCKETS);

	nla_for_each_attr(attr, genlmsg_attrdata(gnlh, 0),
			  genlmsg_attrlen(gnlh, 0), rem) {
		struct nlattr *a;
		char *res;
		int i;

		nla_for_each_nested(a, attr, i) {
			switch (nla_type(a)) {
			case NFSD_A_SOCK_TRANSPORT_NAME:
				res = strncpy(nfsd_sockets[idx].name, nla_data(a),
					      MAX_CLASS_NAME_LEN);
				res[MAX_CLASS_NAME_LEN - 1] = '\0'; // just to be sure
				break;
			case NFSD_A_SOCK_ADDR:
				memcpy(&nfsd_sockets[idx].ss, nla_data(a),
					sizeof(nfsd_sockets[idx].ss));
				break;
			}
			nfsd_sockets[idx].active = true;
		}
		++idx;
	}
	nfsd_socket_count = idx;
}

static void parse_threads_get(struct genlmsghdr *gnlh)
{
	struct nlattr *attr;
	int rem, pools = 0, i = 0;
	uint32_t *pool_threads = NULL;

	nla_for_each_attr(attr, genlmsg_attrdata(gnlh, 0),
			  genlmsg_attrlen(gnlh, 0), rem)
		if (nla_type(attr) == NFSD_A_SERVER_THREADS)
			++pools;

	pool_threads = alloca(pools * sizeof(*pool_threads));

	nla_for_each_attr(attr, genlmsg_attrdata(gnlh, 0),
			  genlmsg_attrlen(gnlh, 0), rem) {
		switch (nla_type(attr)) {
		case NFSD_A_SERVER_GRACETIME:
			printf("gracetime: %u\n", nla_get_u32(attr));
			break;
		case NFSD_A_SERVER_LEASETIME:
			printf("leasetime: %u\n", nla_get_u32(attr));
			break;
		case NFSD_A_SERVER_SCOPE:
			printf("scope: %s\n", (const char *)nla_data(attr));
			break;
		case NFSD_A_SERVER_THREADS:
			pool_threads[i++] = nla_get_u32(attr);
			break;
		default:
			break;
		}
	}

	printf("pool-threads:");
	for (i = 0; i < pools; ++i)
		printf(" %d", pool_threads[i]);
	putchar('\n');
}

static void parse_pool_mode_get(struct genlmsghdr *gnlh)
{
	struct nlattr *attr;
	int rem;

	nla_for_each_attr(attr, genlmsg_attrdata(gnlh, 0),
			  genlmsg_attrlen(gnlh, 0), rem) {
		switch (nla_type(attr)) {
		case NFSD_A_POOL_MODE_MODE:
			printf("pool-mode: %s\n", (const char *)nla_data(attr));
			break;
		case NFSD_A_POOL_MODE_NPOOLS:
			printf("npools: %u\n", nla_get_u32(attr));
			break;
		default:
			break;
		}
	}
}

static int recv_handler(struct nl_msg *msg, void *arg)
{
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));

	switch (gnlh->cmd) {
	case NFSD_CMD_RPC_STATUS_GET:
		parse_rpc_status_get(gnlh);
		break;
	case NFSD_CMD_THREADS_GET:
		parse_threads_get(gnlh);
		break;
	case NFSD_CMD_VERSION_GET:
		parse_version_get(gnlh);
		break;
	case NFSD_CMD_LISTENER_GET:
		parse_listener_get(gnlh);
		break;
	case NFSD_CMD_POOL_MODE_GET:
		parse_pool_mode_get(gnlh);
		break;
	default:
		break;
	}

	return NL_SKIP;
}

#define BUFFER_SIZE	8192
static struct nl_sock *netlink_sock_alloc(void)
{
	struct nl_sock *sock;
	int ret;

	sock = nl_socket_alloc();
	if (!sock)
		return NULL;

	if (genl_connect(sock)) {
		fprintf(stderr, "Failed to connect to generic netlink\n");
		nl_socket_free(sock);
		return NULL;
	}

	nl_socket_set_buffer_size(sock, BUFFER_SIZE, BUFFER_SIZE);
	setsockopt(nl_socket_get_fd(sock), SOL_NETLINK, NETLINK_EXT_ACK,
		   &ret, sizeof(ret));

	return sock;
}

static struct nl_msg *netlink_msg_alloc(struct nl_sock *sock)
{
	struct nl_msg *msg;
	int id;

	id = genl_ctrl_resolve(sock, NFSD_FAMILY_NAME);
	if (id < 0) {
		fprintf(stderr, "%s not found\n", NFSD_FAMILY_NAME);
		return NULL;
	}

	msg = nlmsg_alloc();
	if (!msg) {
		fprintf(stderr, "failed to allocate netlink message\n");
		return NULL;
	}

	if (!genlmsg_put(msg, 0, 0, id, 0, 0, 0, 0)) {
		fprintf(stderr, "failed to allocate netlink message\n");
		nlmsg_free(msg);
		return NULL;
	}

	return msg;
}

static void status_usage(void)
{
	printf("Usage: %s status\n", taskname);
	printf("    Display RPC jobs currently in flight on the server.\n");
}

static int status_func(struct nl_sock *sock, int argc, char ** argv)
{
	struct genlmsghdr *ghdr;
	struct nlmsghdr *nlh;
	struct nl_msg *msg;
	struct nl_cb *cb;
	int opt, ret;

	optind = 1;
	while ((opt = getopt_long(argc, argv, "h", help_only_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			status_usage();
			return 0;
		}
	}

	msg = netlink_msg_alloc(sock);
	if (!msg)
		return 1;

	nlh = nlmsg_hdr(msg);
	nlh->nlmsg_flags |= NLM_F_DUMP;
	ghdr = nlmsg_data(nlh);
	ghdr->cmd = NFSD_CMD_RPC_STATUS_GET;

	cb = nl_cb_alloc(NL_CB_CUSTOM);
	if (!cb) {
		fprintf(stderr, "failed to allocate netlink callbacks\n");
		ret = 1;
		goto out;
	}

	ret = nl_send_auto(sock, msg);
	if (ret < 0)
		goto out_cb;

	ret = 1;
	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &ret);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ret);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, recv_handler, NULL);

	while (ret > 0)
		nl_recvmsgs(sock, cb);
	if (ret < 0) {
		fprintf(stderr, "Error: %s\n", strerror(-ret));
		ret = 1;
	}
out_cb:
	nl_cb_put(cb);
out:
	nlmsg_free(msg);
	return ret;
}

static int threads_doit(struct nl_sock *sock, int cmd, int grace, int lease,
			int pool_count, int *pool_threads, char *scope)
{
	struct genlmsghdr *ghdr;
	struct nlmsghdr *nlh;
	struct nl_msg *msg;
	struct nl_cb *cb;
	int ret;

	msg = netlink_msg_alloc(sock);
	if (!msg)
		return 1;

	nlh = nlmsg_hdr(msg);
	if (cmd == NFSD_CMD_THREADS_SET) {
		int i;

		if (grace)
			nla_put_u32(msg, NFSD_A_SERVER_GRACETIME, grace);
		if (lease)
			nla_put_u32(msg, NFSD_A_SERVER_LEASETIME, lease);
		if (scope)
			nla_put_string(msg, NFSD_A_SERVER_SCOPE, scope);
		for (i = 0; i < pool_count; ++i)
			nla_put_u32(msg, NFSD_A_SERVER_THREADS, pool_threads[i]);
	}
	ghdr = nlmsg_data(nlh);
	ghdr->cmd = cmd;

	cb = nl_cb_alloc(NL_CB_CUSTOM);
	if (!cb) {
		fprintf(stderr, "failed to allocate netlink callbacks\n");
		ret = 1;
		goto out;
	}

	ret = nl_send_auto(sock, msg);
	if (ret < 0) {
		fprintf(stderr, "send failed (%d)!\n", ret);
		goto out_cb;
	}

	ret = 1;
	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &ret);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ret);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, recv_handler, NULL);

	while (ret > 0)
		nl_recvmsgs(sock, cb);
	if (ret < 0) {
		fprintf(stderr, "Error: %s\n", strerror(-ret));
		ret = 1;
	}
out_cb:
	nl_cb_put(cb);
out:
	nlmsg_free(msg);
	return ret;
}

static void threads_usage(void)
{
	printf("Usage: %s threads [ pool0_count ] [ pool1_count ] ...\n", taskname);
	printf("    pool0_count: thread count for pool0, etc...\n");
	printf("Omit any arguments to show current thread counts.\n");
}

static int threads_func(struct nl_sock *sock, int argc, char **argv)
{
	uint8_t cmd = NFSD_CMD_THREADS_GET;
	int *pool_threads = NULL;
	int opt, pools = 0;

	optind = 1;
	while ((opt = getopt_long(argc, argv, "h", help_only_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			threads_usage();
			return 0;
		}
	}

	if (optind < argc) {
		char **targv = &argv[optind];
		int i;

		pools = argc - optind;
		pool_threads = alloca(pools * sizeof(*pool_threads));
		cmd = NFSD_CMD_THREADS_SET;

		for (i = 0; i < pools; ++i) {
			char *endptr = NULL;

			/* empty string? */
			if (targv[i][0] == '\0') {
				fprintf(stderr, "Invalid threads value %s.\n", targv[i]);
				return 1;
			}

			pool_threads[i] = strtol(targv[i], &endptr, 0);
			if (!endptr || *endptr != '\0') {
				fprintf(stderr, "Invalid threads value %s.\n", argv[1]);
				return 1;
			}
		}
	}
	return threads_doit(sock, cmd, 0, 0, pools, pool_threads, NULL);
}

/*
 * Update the nfsd_versions array with the latest info from the kernel
 */
static int fetch_nfsd_versions(struct nl_sock *sock)
{
	struct genlmsghdr *ghdr;
	struct nlmsghdr *nlh;
	struct nl_msg *msg;
	struct nl_cb *cb;
	int ret;

	msg = netlink_msg_alloc(sock);
	if (!msg)
		return 1;

	nlh = nlmsg_hdr(msg);
	ghdr = nlmsg_data(nlh);
	ghdr->cmd = NFSD_CMD_VERSION_GET;

	cb = nl_cb_alloc(NL_CB_CUSTOM);
	if (!cb) {
		fprintf(stderr, "failed to allocate netlink callbacks\n");
		ret = 1;
		goto out;
	}

	ret = nl_send_auto(sock, msg);
	if (ret < 0) {
		fprintf(stderr, "send failed: %d\n", ret);
		goto out_cb;
	}

	ret = 1;
	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &ret);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ret);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, recv_handler, NULL);

	while (ret > 0)
		nl_recvmsgs(sock, cb);
	if (ret < 0) {
		fprintf(stderr, "Error: %s\n", strerror(-ret));
		ret = 1;
	}
out_cb:
	nl_cb_put(cb);
out:
	nlmsg_free(msg);
	return ret;
}

static void print_versions_array(void)
{
	int i;

	for (i = 0; i < MAX_NFS_VERSIONS; ++i) {
		/* A major of zero indicates the end of the array */
		if (nfsd_versions[i].major == 0)
			break;
		if (i != 0)
			printf(" ");
		printf("%c%hhd.%hhd",
			nfsd_versions[i].enabled ? '+' : '-',
			nfsd_versions[i].major, nfsd_versions[i].minor);
	}
	putchar('\n');
}

static int set_nfsd_versions(struct nl_sock *sock)
{
	struct genlmsghdr *ghdr;
	struct nlmsghdr *nlh;
	struct nl_msg *msg;
	struct nl_cb *cb;
	int i, ret;

	msg = netlink_msg_alloc(sock);
	if (!msg)
		return 1;

	nlh = nlmsg_hdr(msg);

	for (i = 0; i < MAX_NFS_VERSIONS; ++i) {
		struct nlattr *a;

		if (nfsd_versions[i].major == 0)
			break;

		a = nla_nest_start(msg, NLA_F_NESTED | NFSD_A_SERVER_PROTO_VERSION);
		if (!a) {
			fprintf(stderr, "Unable to allocate version nest!\n");
			ret = 1;
			goto out;
		}

		nla_put_u32(msg, NFSD_A_VERSION_MAJOR, nfsd_versions[i].major);
		nla_put_u32(msg, NFSD_A_VERSION_MINOR, nfsd_versions[i].minor);
		if (nfsd_versions[i].enabled)
			nla_put_flag(msg, NFSD_A_VERSION_ENABLED);
		nla_nest_end(msg, a);
	}

	ghdr = nlmsg_data(nlh);
	ghdr->cmd = NFSD_CMD_VERSION_SET;

	cb = nl_cb_alloc(NL_CB_CUSTOM);
	if (!cb) {
		fprintf(stderr, "Failed to allocate netlink callbacks\n");
		ret = 1;
		goto out;
	}

	ret = nl_send_auto(sock, msg);
	if (ret < 0) {
		fprintf(stderr, "Send failed: %d\n", ret);
		goto out_cb;
	}

	ret = 1;
	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &ret);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ret);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, recv_handler, NULL);

	while (ret > 0)
		nl_recvmsgs(sock, cb);
	if (ret < 0) {
		fprintf(stderr, "Error: %s\n", strerror(-ret));
		ret = 1;
	}
out_cb:
	nl_cb_put(cb);
out:
	nlmsg_free(msg);
	return ret;
}

static int update_nfsd_version(int major, int minor, bool enabled)
{
	int i;

	for (i = 0; i < MAX_NFS_VERSIONS; ++i) {
		if (nfsd_versions[i].major == 0)
			break;
		if (nfsd_versions[i].major == major && nfsd_versions[i].minor == minor) {
			nfsd_versions[i].enabled = enabled;
			return 0;
		}
	}
	/* the kernel doesn't support this version */
	if (!enabled)
		return 0;
	fprintf(stderr, "This kernel does not support NFS version %d.%d\n", major, minor);
	return -EINVAL;
}

static void version_usage(void)
{
	printf("Usage: %s version { {+,-}major.minor } ...\n", taskname);
	printf("    + to enable a version, - to disable it\n");
	printf("    @major: major version number\n");
	printf("    @minor: minor version number\n");
	printf("Examples:\n");
	printf("    Display currently enabled and disabled versions:\n");
	printf("        version\n");
	printf("    Disable NFSv4.0:\n");
	printf("        version -4.0\n");
	printf("    Enable v4.1, v4.2, disable v2, v3 and v4.0:\n");
	printf("        version -2 -3 -4.0 +4.1 +4.2\n");
}

static int version_func(struct nl_sock *sock, int argc, char ** argv)
{
	int ret, i;

	/* help is only valid as first argument after command */
	if (argc > 1 &&
	    (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		version_usage();
		return 0;
	}

	ret = fetch_nfsd_versions(sock);
	if (ret)
		return ret;

	if (argc > 1) {
		for (i = 1; i < argc; ++i) {
			int ret, major, minor = 0;
			char sign = '\0', *str = argv[i];
			bool enabled;

			ret = sscanf(str, "%c%d.%d\n", &sign, &major, &minor);
			if (ret < 2) {
				fprintf(stderr, "Invalid version string (%d) %s\n", ret, str);
				return -EINVAL;
			}

			switch(sign) {
			case '+':
				enabled = true;
				break;
			case '-':
				enabled = false;
				break;
			default:
				fprintf(stderr, "Invalid version string %s\n", str);
				return -EINVAL;
			}

			ret = update_nfsd_version(major, minor, enabled);
			if (ret)
				return ret;
		}
		return set_nfsd_versions(sock);
	}

	print_versions_array();
	return 0;
}

static int fetch_current_listeners(struct nl_sock *sock)
{
	struct genlmsghdr *ghdr;
	struct nlmsghdr *nlh;
	struct nl_msg *msg;
	struct nl_cb *cb;
	int ret;

	msg = netlink_msg_alloc(sock);
	if (!msg)
		return 1;

	nlh = nlmsg_hdr(msg);
	ghdr = nlmsg_data(nlh);
	ghdr->cmd = NFSD_CMD_LISTENER_GET;

	cb = nl_cb_alloc(NL_CB_CUSTOM);
	if (!cb) {
		fprintf(stderr, "failed to allocate netlink callbacks\n");
		ret = 1;
		goto out;
	}

	ret = nl_send_auto(sock, msg);
	if (ret < 0) {
		fprintf(stderr, "send failed: %d\n", ret);
		goto out_cb;
	}

	ret = 1;
	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &ret);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ret);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, recv_handler, NULL);

	while (ret > 0)
		nl_recvmsgs(sock, cb);
	if (ret < 0) {
		fprintf(stderr, "Error: %s\n", strerror(-ret));
		ret = 1;
	}
out_cb:
	nl_cb_put(cb);
out:
	nlmsg_free(msg);
	return ret;
}

static void print_listeners(void)
{
	int i;
	const char *res;

	for (i = 0; i < MAX_NFSD_SOCKETS; ++i) {
		struct server_socket *sock = &nfsd_sockets[i];
		char addr[INET6_ADDRSTRLEN + 1];
		in_port_t port = 0;

		if (*sock->name == '\0')
			break;

		if (!sock->active)
			continue;

		switch(sock->ss.ss_family) {
		case AF_INET:
			res = inet_ntop(AF_INET, &((struct sockaddr_in *)(&sock->ss))->sin_addr,
					addr, INET6_ADDRSTRLEN);
			port = ((struct sockaddr_in *)(&sock->ss))->sin_port;
			if (res == NULL)
				perror("inet_ntop");
			else
				printf("%s:%s:%hu\n", sock->name, addr, ntohs(port));
			break;
		case AF_INET6:
			res = inet_ntop(AF_INET6, &((struct sockaddr_in6 *)(&sock->ss))->sin6_addr,
					addr, INET6_ADDRSTRLEN);
			port = ((struct sockaddr_in6 *)(&sock->ss))->sin6_port;
			if (res == NULL)
				perror("inet_ntop");
			else
				printf("%s:[%s]:%hu\n", sock->name, addr, ntohs(port));
			break;
		default:
			snprintf(addr, INET6_ADDRSTRLEN, "Unknown address family: %d\n",
					sock->ss.ss_family);
			addr[INET6_ADDRSTRLEN - 1] = '\0';
		}
	}
}

#define BUFLEN (INET6_ADDRSTRLEN + 16)

/*
 * Format is <+/-><netid>:<address>:port
 *
 * + or -: denotes whether we're adding or removing a socket
 * netid: tcp, udp, rdma (something else in the future?(
 * address: IPv4 or IPv6 address. IPv6 addr should be in square brackets
 * port: decimal port value
 */
static int update_listeners(const char *str)
{
	char buf[INET6_ADDRSTRLEN + 16];
	char sign = *str;
	char *netid, *addr, *port, *end;
	struct addrinfo *res;
	int i, ret;
	struct addrinfo hints = { .ai_flags = AI_PASSIVE,
				  .ai_family = AF_INET,
				  .ai_socktype = SOCK_STREAM,
				  .ai_protocol = IPPROTO_TCP };

	if (sign != '+' && sign != '-')
		goto out_inval;

	strcpy(buf, str + 1);

	/* netid is start */
	netid = buf;

	/* find first ':' */
	addr = strchr(buf, ':');
	if (!addr)
		goto out_inval;

	if (addr == buf) {
		/* empty netid */
		goto out_inval;
	}
	*addr = '\0';
	++addr;

	port = strrchr(addr, ':');
	if (!port)
		goto out_inval;
	if (port == addr) {
		/* empty address, give gai a NULL ptr */
		addr = NULL;
	}
	*port = '\0';
	port++;

	if (*port == '\0') {
		/* empty port */
		goto out_inval;
	}

	/* IPv6 addrs must be in square brackets */
	if (addr && *addr == '[') {
		hints.ai_family = AF_INET6;
		++addr;
		end = strchr(addr, ']');
		if (!end)
			goto out_inval;
		if (end == addr)
			addr = NULL;
		*end = '\0';
	}

	/*
	 * If we're looking for wildcard address, look for both
	 * families.
	 */
	if (!addr)
		hints.ai_family = AF_UNSPEC;

	/*
	 * Note that we hint for a stream/tcp socket just to limit the number of
	 * entries that come back. We're only interested in the sockaddrs.
	 */
	ret = getaddrinfo(addr, port, &hints, &res);
	if (ret) {
		fprintf(stderr, "getaddrinfo of \"%s\" failed: %s\n",
			addr, gai_strerror(ret));
		return -EINVAL;
	}

	for ( ; res; res = res->ai_next) {
		struct sockaddr_in6 *r6 = (struct sockaddr_in6 *)res->ai_addr;
		struct sockaddr_in *r4 = (struct sockaddr_in *)res->ai_addr;
		bool found = false;

		for (i = 0; i < MAX_NFSD_SOCKETS; ++i) {
			struct server_socket *sock = &nfsd_sockets[i];
			struct sockaddr_in6 *l6 = (struct sockaddr_in6 *)&sock->ss;
			struct sockaddr_in *l4 = (struct sockaddr_in *)&sock->ss;

			if (sock->ss.ss_family == AF_UNSPEC)
				break;

			if (sock->ss.ss_family != res->ai_addr->sa_family)
				continue;

			if (strcmp(sock->name, netid))
				continue;

			switch(sock->ss.ss_family) {
			case AF_INET:
				if (r4->sin_port != l4->sin_port ||
				    memcmp(&r4->sin_addr, &l4->sin_addr, sizeof(l4->sin_addr)))
					continue;
			case AF_INET6:
				if (r6->sin6_port != l6->sin6_port ||
				    memcmp(&r6->sin6_addr, &l6->sin6_addr, sizeof(l6->sin6_addr)))
					continue;
			default:

			}
			sock->active = (sign == '+');
			found = true;
			break;
		}
		if (!found && sign == '+') {
			struct server_socket *sock = &nfsd_sockets[nfsd_socket_count];

			memcpy(&sock->ss, res->ai_addr, res->ai_addrlen);
			strncpy(sock->name, netid, MAX_CLASS_NAME_LEN);
			sock->name[MAX_CLASS_NAME_LEN - 1] = '\0';
			sock->active = true;
			++nfsd_socket_count;
		}
	}
	return 0;
out_inval:
	fprintf(stderr, "Invalid listener update string: %s", str);
	return -EINVAL;
}

static int set_listeners(struct nl_sock *sock)
{
	struct genlmsghdr *ghdr;
	struct nlmsghdr *nlh;
	struct nl_msg *msg;
	struct nl_cb *cb;
	int i, ret;

	msg = netlink_msg_alloc(sock);
	if (!msg)
		return 1;

	nlh = nlmsg_hdr(msg);

	for (i = 0; i < MAX_NFSD_SOCKETS; ++i) {
		struct server_socket *sock = &nfsd_sockets[i];
		struct nlattr *a;

		if (sock->ss.ss_family == 0)
			break;

		if (!sock->active)
			continue;

		a = nla_nest_start(msg, NLA_F_NESTED | NFSD_A_SERVER_SOCK_ADDR);
		if (!a) {
			fprintf(stderr, "Unable to allocate listener nest!\n");
			ret = 1;
			goto out;
		}

		nla_put(msg, NFSD_A_SOCK_ADDR, sizeof(sock->ss), &sock->ss);
		nla_put_string(msg, NFSD_A_SOCK_TRANSPORT_NAME, sock->name);
		nla_nest_end(msg, a);
	}

	ghdr = nlmsg_data(nlh);
	ghdr->cmd = NFSD_CMD_LISTENER_SET;

	cb = nl_cb_alloc(NL_CB_CUSTOM);
	if (!cb) {
		fprintf(stderr, "Failed to allocate netlink callbacks\n");
		ret = 1;
		goto out;
	}

	ret = nl_send_auto(sock, msg);
	if (ret < 0) {
		fprintf(stderr, "Send failed: %d\n", ret);
		goto out_cb;
	}

	ret = 1;
	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &ret);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ret);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, recv_handler, NULL);

	while (ret > 0)
		nl_recvmsgs(sock, cb);
	if (ret < 0) {
		fprintf(stderr, "Error: %s\n", strerror(-ret));
		ret = 1;
	}
out_cb:
	nl_cb_put(cb);
out:
	nlmsg_free(msg);
	return ret;
}

static void listener_usage(void)
{
	printf("Usage: %s listener { {+,-}proto:addr:port } ...\n", taskname);
	printf("    + to add a listener, - to remove one\n");
	printf("    @proto: protocol (e.g. tcp, udp, rdma)\n");
	printf("    @addr: hostname or address to listen on (blank string == wildcard addresses)\n");
	printf("    @port: port number or service name to listen on\n\n");
	printf("Examples:\n");
	printf("    Display currently configured listeners:\n");
	printf("        listener\n");
	printf("    Add TCP listener on all addresses (both v4 and v6), port 2049:\n");
	printf("        listener +tcp::2049\n");
	printf("    Add RDMA listener on 1.2.3.4 port 20049:\n");
	printf("        listener +rdma:1.2.3.4:20049\n");
	printf("    Add same listener on IPv6 address f00::ba4 port 20050:\n");
	printf("        listener +rdma:[f00::ba4]:20050\n");
	printf("    Remove UDP listener from nfsserver.example.org, nfs port:\n");
	printf("        listener -udp:nfsserver.example.org:nfs\n\n");
}

static int listener_func(struct nl_sock *sock, int argc, char ** argv)
{
	int ret, i;

	/* help is only valid as first argument after command */
	if (argc > 1 &&
	    (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		listener_usage();
		return 0;
	}

	ret = fetch_current_listeners(sock);
	if (ret)
		return ret;

	if (argc > 1) {
		for (i = 1; i < argc; ++i)
			update_listeners(argv[i]);
		return set_listeners(sock);
	}

	print_listeners();
	return 0;
}

static int pool_mode_doit(struct nl_sock *sock, int cmd, const char *pool_mode)
{
	struct genlmsghdr *ghdr;
	struct nlmsghdr *nlh;
	struct nl_msg *msg;
	struct nl_cb *cb;
	int ret;

	msg = netlink_msg_alloc(sock);
	if (!msg)
		return 1;

	nlh = nlmsg_hdr(msg);
	if (cmd == NFSD_CMD_POOL_MODE_SET) {
		if (pool_mode)
			nla_put_string(msg, NFSD_A_POOL_MODE_MODE, pool_mode);
	}
	ghdr = nlmsg_data(nlh);
	ghdr->cmd = cmd;

	cb = nl_cb_alloc(NL_CB_CUSTOM);
	if (!cb) {
		fprintf(stderr, "failed to allocate netlink callbacks\n");
		ret = 1;
		goto out;
	}

	ret = nl_send_auto(sock, msg);
	if (ret < 0) {
		fprintf(stderr, "send failed (%d)!\n", ret);
		goto out_cb;
	}

	ret = 1;
	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &ret);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ret);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, recv_handler, NULL);

	while (ret > 0)
		nl_recvmsgs(sock, cb);
	if (ret < 0) {
		fprintf(stderr, "Error: %s\n", strerror(-ret));
		ret = 1;
	}
out_cb:
	nl_cb_put(cb);
out:
	nlmsg_free(msg);
	return ret;
}

static void pool_mode_usage(void)
{
	printf("Usage: %s pool-mode { global | auto | percpu | pernode } ...\n", taskname);
}

static int pool_mode_func(struct nl_sock *sock, int argc, char **argv)
{
	uint8_t cmd = NFSD_CMD_POOL_MODE_GET;
	const char *pool_mode = NULL;
	int opt;

	optind = 1;
	while ((opt = getopt_long(argc, argv, "h", help_only_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			pool_mode_usage();
			return 0;
		}
	}

	if (optind < argc) {
		char **targv = &argv[optind];

		cmd = NFSD_CMD_POOL_MODE_SET;

		/* empty string? */
		if (*targv[0] == '\0') {
			fprintf(stderr, "Invalid threads value %s.\n", targv[0]);
			return 1;
		}
		pool_mode = targv[0];
	}
	return pool_mode_doit(sock, cmd, pool_mode);
}

#define MAX_LISTENER_LEN (64 * 2 + 16)

static void
add_listener(const char *netid, const char *addr, const char *port)
{
		char buf[MAX_LISTENER_LEN];

		if (strchr(addr, ':'))
			snprintf(buf, MAX_LISTENER_LEN, "+%s:[%s]:%s",
				 netid, addr, port);
		else
			snprintf(buf, MAX_LISTENER_LEN, "+%s:%s:%s",
				 netid, addr, port);
		buf[MAX_LISTENER_LEN - 1] = '\0';
		update_listeners(buf);
}

static void
read_nfsd_conf(void)
{
	conf_init_file(NFS_CONFFILE);
	xlog_set_debug("nfsd");
}

static void configure_versions(void)
{
	bool v4 = conf_get_bool("nfsd", "vers4", true);

	update_nfsd_version(2, 0, conf_get_bool("nfsd", "vers2", false));
	update_nfsd_version(3, 0, conf_get_bool("nfsd", "vers3", true));
	update_nfsd_version(4, 0, v4 && conf_get_bool("nfsd", "vers4.0", true));
	update_nfsd_version(4, 1, v4 && conf_get_bool("nfsd", "vers4.1", true));
	update_nfsd_version(4, 2, v4 && conf_get_bool("nfsd", "vers4.2", true));
}

static void configure_listeners(void)
{
	char *port, *rdma_port;
	bool rdma, udp, tcp;
	struct conf_list *hosts;

	udp = conf_get_bool("nfsd", "udp", false);
	tcp = conf_get_bool("nfsd", "tcp", true);
	port = conf_get_str("nfsd", "port");
	if (!port)
		port = "nfs";

	rdma = conf_get_bool("nfsd", "rdma", false);
	if (rdma) {
		rdma_port = conf_get_str("nfsd", "rdma-port");
		if (!rdma_port)
			rdma_port = "nfsrdma";
	}

	/* backward compatibility - nfs.conf used to set rdma port directly */
	if (!rdma_port)
		rdma_port = conf_get_str("nfsd", "rdma");

	hosts = conf_get_list("nfsd", "host");
	if (hosts && hosts->cnt) {
		struct conf_list_node *n;
		TAILQ_FOREACH(n, &(hosts->fields), link) {
			if (udp)
				add_listener("udp", n->field, port);
			if (tcp)
				add_listener("tcp", n->field, port);
			if (rdma)
				add_listener("rdma", n->field, rdma_port);
		}
	} else {
		if (udp)
			add_listener("udp", "", port);
		if (tcp)
			add_listener("tcp", "", port);
		if (rdma)
			add_listener("rdma", "", rdma_port);
	}
}

static void autostart_usage(void)
{
	printf("Usage: %s autostart\n", taskname);
	printf("    Start the server with the settings in /etc/nfs.conf.\n");
}

/* default number of nfsd threads when not specified in nfs.conf */
#define DEFAULT_AUTOSTART_THREADS	16

static int autostart_func(struct nl_sock *sock, int argc, char ** argv)
{
	int *threads, grace, lease, idx, ret, opt, pools;
	struct conf_list *thread_str;
	struct conf_list_node *n;
	char *scope, *pool_mode;

	optind = 1;
	while ((opt = getopt_long(argc, argv, "h", help_only_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			autostart_usage();
			return 0;
		}
	}

	read_nfsd_conf();

	pool_mode = conf_get_str("nfsd", "pool-mode");
	if (pool_mode) {
		ret = pool_mode_doit(sock, NFSD_CMD_POOL_MODE_SET, pool_mode);
		if (ret)
			return ret;
	}

	ret = fetch_nfsd_versions(sock);
	if (ret)
		return ret;
	configure_versions();
	ret = set_nfsd_versions(sock);
	if (ret)
		return ret;

	configure_listeners();
	ret = set_listeners(sock);
	if (ret)
		return ret;

	grace = conf_get_num("nfsd", "grace-time", 0);
	lease = conf_get_num("nfsd", "lease-time", 0);
	scope = conf_get_str("nfsd", "scope");

	thread_str = conf_get_list("nfsd", "threads");
	pools = thread_str ? thread_str->cnt : 1;

	threads = calloc(pools, sizeof(int));
	if (!threads)
		return -ENOMEM;

	if (thread_str) {
		idx = 0;
		TAILQ_FOREACH(n, &(thread_str->fields), link) {
			char *endptr = NULL;

			threads[idx++] = strtol(n->field, &endptr, 0);
			if (!endptr || *endptr != '\0') {
				fprintf(stderr, "Invalid threads value %s.\n", n->field);
				ret = -EINVAL;
				goto out;
			}
		}
	} else {
		threads[0] = DEFAULT_AUTOSTART_THREADS;
	}

	ret = threads_doit(sock, NFSD_CMD_THREADS_SET, grace, lease, pools,
			   threads, scope);
out:
	free(threads);
	return ret;
}

enum nfsdctl_commands {
	NFSDCTL_STATUS,
	NFSDCTL_THREADS,
	NFSDCTL_VERSION,
	NFSDCTL_LISTENER,
	NFSDCTL_AUTOSTART,
	NFSDCTL_POOL_MODE,
};

static int parse_command(char *str)
{
	if (!strcmp(str, "status"))
		return NFSDCTL_STATUS;
	if (!strcmp(str, "threads"))
		return NFSDCTL_THREADS;
	if (!strcmp(str, "version"))
		return NFSDCTL_VERSION;
	if (!strcmp(str, "listener"))
		return NFSDCTL_LISTENER;
	if (!strcmp(str, "autostart"))
		return NFSDCTL_AUTOSTART;
	if (!strcmp(str, "pool-mode"))
		return NFSDCTL_POOL_MODE;
	return -1;
}

typedef int (*nfsdctl_func)(struct nl_sock *sock, int argc, char **argv);

static nfsdctl_func func[] = {
	[NFSDCTL_STATUS] = status_func,
	[NFSDCTL_THREADS] = threads_func,
	[NFSDCTL_VERSION] = version_func,
	[NFSDCTL_LISTENER] = listener_func,
	[NFSDCTL_AUTOSTART] = autostart_func,
	[NFSDCTL_POOL_MODE] = pool_mode_func,
};

static void usage(void)
{
	printf("Usage:\n");
	printf("%s [-hv] [COMMAND] [ARGS]\n", taskname);
	printf("  options:\n");
	printf("    -h | --help          usage info\n");
	printf("    -d | --debug=NUM     enable debugging\n");
	printf("    -V | --version       print version info\n");
	printf("  commands:\n");
	printf("    pool-mode            get/set host pool mode setting\n");
	printf("    listener             get/set listener info\n");
	printf("    version              get/set supported NFS versions\n");
	printf("    threads              get/set nfsd thread settings\n");
	printf("    status               get current RPC processing info\n");
	printf("    autostart            start server with settings from /etc/nfs.conf\n");
}

/* Options given before the command string */
static const struct option pre_options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "debug", required_argument, NULL, 'd' },
	{ "version", no_argument, NULL, 'V' },
	{ },
};

static int run_one_command(struct nl_sock *sock, int argc, char **argv)
{
	int cmd = parse_command(argv[0]);

	if (cmd < 0) {
		usage();
		return 1;
	}
	return func[cmd](sock, argc, argv);
}

#define MAX_ARGUMENTS 256

static int tokenize_string(char *line, int *argc, char **argv)
{
	int idx = 0;
	char *arg, *save;

	memset(argv, '\0', sizeof(*argv) * MAX_ARGUMENTS);

	arg = strtok_r(line, " \t", &save);
	while(arg) {
		argv[idx] = arg;
		++idx;
		if (idx >= MAX_ARGUMENTS)
			return -E2BIG;
		arg = strtok_r(NULL, " \t", &save);
	}
	*argc = idx;
	return 0;
}

static int run_commandline(struct nl_sock *sock)
{
	char *argv[MAX_ARGUMENTS];
	char *line;
	int ret, argc;

	for (;;) {
		line = readline("nfsdctl> ");
		if (!line || !strcmp(line, "quit"))
			break;
		if (*line == '\0')
			continue;
		add_history(line);
		ret = tokenize_string(line, &argc, argv);
		if (!ret)
			ret = run_one_command(sock, argc, argv);
		if (ret)
			fprintf(stderr, "Error: %s\n", strerror(ret));
		free(line);
	}
	return 0;
}

int main(int argc, char **argv)
{
	int opt, ret;
	struct nl_sock *sock = netlink_sock_alloc();

	if (!sock) {
		fprintf(stderr, "Unable to allocate netlink socket!");
		return 1;
	}

	taskname = argv[0];

	/* Parse the preliminary options */
	while ((opt = getopt_long(argc, argv, "+hd:V", pre_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			usage();
			return 0;
		case 'd':
			debug_level = atoi(optarg);
			break;
		case 'V':
			// FIXME: print_version();
			return 0;
		}
	}

	if (optind > argc) {
		usage();
		return 1;
	}

	if (optind == argc)
		ret = run_commandline(sock);
	else
		ret = run_one_command(sock, argc - optind, &argv[optind]);

	nl_socket_free(sock);
	return ret;
}
