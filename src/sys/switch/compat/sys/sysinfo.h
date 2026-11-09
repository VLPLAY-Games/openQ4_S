// Nintendo Switch: report the memory budget Horizon grants this process, which is
// what the engine's cache sizing should follow (not the console's 4 GB total).
#pragma once
// libnx defines its own BIT(); keep the engine's while its headers parse.
#pragma push_macro("BIT")
#undef BIT
#include <switch.h>
#pragma pop_macro("BIT")
struct sysinfo {
	unsigned long totalram, freeram, sharedram, bufferram, totalswap, freeswap;
	unsigned int mem_unit;
};
static inline int sysinfo( struct sysinfo *info ) {
	u64 total = 0, used = 0;
	svcGetInfo( &total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0 );
	svcGetInfo( &used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0 );
	info->totalram = (unsigned long)total;
	info->freeram = (unsigned long)( total > used ? total - used : 0 );
	info->sharedram = info->bufferram = info->totalswap = info->freeswap = 0;
	info->mem_unit = 1;
	return 0;
}
