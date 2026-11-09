// Force-included before every Switch translation unit. newlib's <sys/types.h>
// defines the BSD macro `quad` (as quad_t), which collides with member names.
#pragma once
#include <sys/types.h>
#include <errno.h>
#include <netinet/in.h>
#undef quad

// glibc retry helper used by the POSIX socket and file code.
#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY( expression ) \
	( __extension__ ( { long int _res; do _res = (long int)( expression ); while ( _res == -1L && errno == EINTR ); _res; } ) )
#endif

// libnx's socket stack is IPv4-only: it has no IPv6 multicast membership.
// The structure exists so the discovery code compiles; the join fails at runtime.
struct ipv6_mreq { struct in6_addr ipv6mr_multiaddr; unsigned int ipv6mr_interface; };
#ifndef IPV6_JOIN_GROUP
#define IPV6_JOIN_GROUP 20
#endif

// newlib's pthread lacks the scheduler-priority calls SDL's thread layer uses.
// Horizon threads take their priority at creation, so these are inert.
#include <pthread.h>
#include <sched.h>
static inline int sched_get_priority_min( int policy ) { (void)policy; return 0; }
static inline int sched_get_priority_max( int policy ) { (void)policy; return 63; }
static inline int pthread_setschedparam( pthread_t t, int policy, const struct sched_param *p ) { (void)t; (void)policy; (void)p; return 0; }
static inline int pthread_getschedparam( pthread_t t, int *policy, struct sched_param *p ) { (void)t; if ( policy ) *policy = 0; if ( p ) p->sched_priority = 0; return 0; }
