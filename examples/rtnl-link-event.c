#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <libmnl/libmnl.h>
#include <linux/if.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>

static int data_cb(const struct nlmsghdr *nlh, void *data)
{
	struct nlattr *tb[IFLA_MAX+1];
	struct ifinfomsg *ifm = mnl_nlmsg_get_data(nlh);
	int len = mnl_nlmsg_get_len(nlh);
	struct nlattr *attr;

	printf("index=%d type=%d flags=%d family=%d ", 
		ifm->ifi_index, ifm->ifi_type,
		ifm->ifi_flags, ifm->ifi_family);

	if (ifm->ifi_flags & IFF_RUNNING)
		printf("[RUNNING] ");
	else
		printf("[NOT RUNNING] ");

	mnl_attr_parse_at_offset(nlh, sizeof(*ifm), tb, IFLA_MAX);
	if (tb[IFLA_MTU]) {
		printf("mtu=%d ", mnl_attr_get_u32(tb[IFLA_MTU]));
	}
	if (tb[IFLA_IFNAME]) {
		printf("name=%s", mnl_attr_get_str(tb[IFLA_IFNAME]));
	}
	printf("\n");
	return MNL_CB_OK;
}

int main()
{
	struct mnl_socket *nl;
	char buf[getpagesize()];
	struct nlmsghdr *nlh = (struct nlmsghdr *) buf;
	int ret;

	nl = mnl_socket_open(NETLINK_ROUTE);
	if (nl == NULL) {
		perror("mnl_socket_open");
		exit(EXIT_FAILURE);
	}

	if (mnl_socket_bind(nl, RTMGRP_LINK, MNL_SOCKET_AUTOPID) < 0) {
		perror("mnl_socket_bind");
		exit(EXIT_FAILURE);
	}

	ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	while (ret > 0) {
		ret = mnl_cb_run(buf, ret, 0, data_cb, NULL);
		if (ret <= 0)
			break;
		ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	}
	if (ret == -1) {
		perror("error");
		exit(EXIT_FAILURE);
	}

	mnl_socket_close(nl);

	return 0;
}