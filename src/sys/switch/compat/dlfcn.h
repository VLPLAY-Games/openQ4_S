// Nintendo Switch homebrew has no dynamic loader. Modules are linked into the NRO,
// so every lookup fails cleanly (see Sys_DLL_Load in the Switch platform layer).
#pragma once
#include <stddef.h>
#define RTLD_LAZY	1
#define RTLD_NOW	2
#define RTLD_NOLOAD	4
#define RTLD_LOCAL	0
#define RTLD_GLOBAL	8
static inline void *dlopen( const char *path, int flags ) { (void)path; (void)flags; return NULL; }
static inline void *dlsym( void *handle, const char *name ) { (void)handle; (void)name; return NULL; }
static inline int dlclose( void *handle ) { (void)handle; return 0; }
static inline char *dlerror( void ) { return (char *)"dynamic loading is not available on Switch"; }
