// libnx has no getifaddrs. The engine uses it only to list IPv6 addresses, so
// reporting "no interfaces" is correct for Switch's IPv4-only socket stack.
#pragma once
#include <errno.h>
#include <sys/socket.h>
struct ifaddrs {
	struct ifaddrs	*ifa_next;
	char			*ifa_name;
	unsigned int	ifa_flags;
	struct sockaddr	*ifa_addr;
	struct sockaddr	*ifa_netmask;
};
#ifndef IFF_UP
#define IFF_UP 0x1
#endif
static inline int getifaddrs( struct ifaddrs **ifap ) { *ifap = 0; errno = ENOSYS; return -1; }
static inline void freeifaddrs( struct ifaddrs *ifa ) { (void)ifa; }
