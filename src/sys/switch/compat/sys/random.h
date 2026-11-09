// Nintendo Switch: getrandom() maps to the kernel entropy source via libnx.
#pragma once
// libnx defines its own BIT(); keep the engine's while its headers parse.
#pragma push_macro("BIT")
#undef BIT
#include <switch.h>
#pragma pop_macro("BIT")
#include <sys/types.h>
#define GRND_NONBLOCK 1
#define GRND_RANDOM 2
static inline ssize_t getrandom( void *buf, size_t len, unsigned int flags ) {
	(void)flags;
	randomGet( buf, len );
	return (ssize_t)len;
}
