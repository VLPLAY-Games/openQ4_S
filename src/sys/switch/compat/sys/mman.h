// Nintendo Switch (newlib/libnx) has no <sys/mman.h>. The engine only pins memory
// with mlock(); the Horizon kernel owns paging, so locking is a successful no-op.
#pragma once
#include <stddef.h>
static inline int mlock( const void *addr, size_t len ) { (void)addr; (void)len; return 0; }
static inline int munlock( const void *addr, size_t len ) { (void)addr; (void)len; return 0; }
