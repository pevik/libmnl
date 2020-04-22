/* This example is placed in the public domain. */
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <net/if.h>
#include <netdb.h>

#include <libmnl/libmnl.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>

static void rtnl_route(int iface, struct addrinfo *dst, struct addrinfo *gw, uint32_t prefix, int type)
{
	struct mnl_socket *nl;
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	struct rtmsg *rtm;
	uint32_t seq, portid;
	int ret, family = dst->ai_family;

	struct in6_addr dst_in6, gw_in6;
	in_addr_t dst_ip, gw_ip;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= type; // RTM_NEWROUTE or RTM_DELROUTE

	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_ACK;
	nlh->nlmsg_seq = seq = time(NULL);

	rtm = mnl_nlmsg_put_extra_header(nlh, sizeof(struct rtmsg));
	rtm->rtm_family = family;
	rtm->rtm_dst_len = prefix;
	rtm->rtm_src_len = 0;
	rtm->rtm_tos = 0;
	rtm->rtm_protocol = RTPROT_STATIC;
	rtm->rtm_table = RT_TABLE_MAIN;
	rtm->rtm_type = RTN_UNICAST;
	/* is there any gateway? */
	rtm->rtm_scope = gw ? RT_SCOPE_UNIVERSE : RT_SCOPE_LINK;
	rtm->rtm_flags = 0;

	char dst_str[INET6_ADDRSTRLEN];
	if (family == AF_INET6) {
		dst_in6 = ((struct sockaddr_in6 *)dst->ai_addr)->sin6_addr;
		if (!inet_ntop(AF_INET6, &dst_in6, dst_str, sizeof(dst_str))) {
			perror("inet_ntop IPv6 dst failed");
			exit(EXIT_FAILURE);
		}
		mnl_attr_put(nlh, RTA_DST, sizeof(struct in6_addr), &dst_in6);
	} else {
		dst_ip = ((struct sockaddr_in *)dst->ai_addr)->sin_addr.s_addr;
		if (!inet_ntop(AF_INET, &dst_ip, dst_str, sizeof(dst_str))) {
			perror("inet_ntop IPv4 dst failed");
			exit(EXIT_FAILURE);
		}
		mnl_attr_put_u32(nlh, RTA_DST, dst_ip);
	}
	printf("%s:%d dst check: '%s'\n", __func__, __LINE__, dst_str);

	mnl_attr_put_u32(nlh, RTA_OIF, iface);

	if (gw) {
		char gw_str[INET6_ADDRSTRLEN];
		if (family == AF_INET6) {
			gw_in6 = ((struct sockaddr_in6 *)gw->ai_addr)->sin6_addr;
			if (!inet_ntop(AF_INET6, &gw_in6, gw_str, sizeof(gw_str))) {
				perror("inet_ntop IPv6 gw failed");
				exit(EXIT_FAILURE);
			}
			mnl_attr_put(nlh, RTA_GATEWAY, sizeof(struct in6_addr), &gw_in6);
		} else {
			gw_ip = ((struct sockaddr_in *)gw->ai_addr)->sin_addr.s_addr;
			mnl_attr_put_u32(nlh, RTA_GATEWAY, gw_ip);
			if (!inet_ntop(AF_INET, &gw_ip, gw_str, sizeof(gw_str))) {
				perror("inet_ntop IPv4 gw failed");
				exit(EXIT_FAILURE);
			}
		}
		printf("%s:%d gw check: '%s'\n", __func__, __LINE__, gw_str);
	}

	nl = mnl_socket_open(NETLINK_ROUTE);
	if (nl == NULL) {
		perror("mnl_socket_open");
		exit(EXIT_FAILURE);
	}

	if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
		perror("mnl_socket_bind");
		exit(EXIT_FAILURE);
	}
	portid = mnl_socket_get_portid(nl);

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_sendto");
		exit(EXIT_FAILURE);
	}

	ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	if (ret < 0) {
		perror("mnl_socket_recvfrom");
		exit(EXIT_FAILURE);
	}

	ret = mnl_cb_run(buf, ret, seq, portid, NULL, NULL);
	if (ret < 0) {
		perror("mnl_cb_run");
		exit(EXIT_FAILURE);
	}

	mnl_socket_close(nl);
}

int main(int argc, char *argv[])
{
	int family = AF_INET;
	int iface;
	uint32_t prefix;
	struct addrinfo hints, *dst, *gw;

	if (argc <= 3) {
		printf("Usage: %s iface destination cidr [gateway]\n", argv[0]);
		printf("Example: %s eth0 10.0.1.12 32 10.0.1.11\n", argv[0]);
		printf("	 %s eth0 ffff::10.0.1.12 128 fdff::1\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	iface = if_nametoindex(argv[1]);
	if (iface == 0) {
		perror("if_nametoindex");
		exit(EXIT_FAILURE);
	}

	if (sscanf(argv[3], "%u", &prefix) == 0) {
		perror("sscanf");
		exit(EXIT_FAILURE);
	}

	if (strchr(argv[2], ':'))
		family = AF_INET6;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;
	hints.ai_addr = INADDR_ANY;

	/* destination */
	if (getaddrinfo(argv[2], NULL, &hints, &dst)) {
		perror("getaddrinfo (dst)");
		exit(EXIT_FAILURE);
	}

	if (!dst) {
		perror("failed to get dst address");
		exit(EXIT_FAILURE);
	}

	char dst_str[INET6_ADDRSTRLEN];
	if (family == AF_INET6) {
		struct in6_addr dst_in6 = ((struct sockaddr_in6 *)dst->ai_addr)->sin6_addr;
		if (!inet_ntop(AF_INET6, &dst_in6, dst_str, sizeof(dst_str))) {
			perror("inet_ntop IPv6 dst failed");
			exit(EXIT_FAILURE);
		}
	} else {
		in_addr_t dst_ip = ((struct sockaddr_in *)dst->ai_addr)->sin_addr.s_addr;
		if (!inet_ntop(AF_INET, &dst_ip, dst_str, sizeof(dst_str))) {
			perror("inet_ntop IPv4 dst failed");
			exit(EXIT_FAILURE);
		}
	}
	printf("%s:%d dst check: '%s'\n", __func__, __LINE__, dst_str);

	/* gateway */
	if (argc == 5) {
		if (getaddrinfo(argv[4], NULL, &hints, &gw)) {
			perror("getaddrinfo (gw)");
			exit(EXIT_FAILURE);
		}

		if (!gw) {
			perror("failed to get gw address");
			exit(EXIT_FAILURE);
		}

		char gw_str[INET6_ADDRSTRLEN];
		if (family == AF_INET6) {
			struct in6_addr gw_in6 = ((struct sockaddr_in6 *)gw->ai_addr)->sin6_addr;
			if (!inet_ntop(AF_INET6, &gw_in6, gw_str, sizeof(gw_str))) {
				perror("inet_ntop IPv6 gw failed");
				exit(EXIT_FAILURE);
			}
		} else {
			in_addr_t gw_ip = ((struct sockaddr_in *)gw->ai_addr)->sin_addr.s_addr;
			if (!inet_ntop(AF_INET, &gw_ip, gw_str, sizeof(gw_str))) {
				perror("inet_ntop IPv4 gw failed");
				exit(EXIT_FAILURE);
			}
		}
		printf("%s:%d gw check: '%s'\n", __func__, __LINE__, gw_str);
	}

	rtnl_route(iface, dst, argc == 5 ? gw : NULL, prefix, RTM_NEWROUTE);
	rtnl_route(iface, dst, argc == 5 ? gw : NULL, prefix, RTM_DELROUTE);

	return 0;
}
