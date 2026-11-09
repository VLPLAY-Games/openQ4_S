/*
  POSIX calls the engine and SDL use that newlib/libnx do not provide.

  Horizon has one process, no users and no directory file descriptors, so most
  of these are fixed answers. The *at() family only supports AT_FDCWD or absolute
  paths; code that holds a directory descriptor (framework/DurableFile.cpp) needs a
  Switch branch instead of this shim. Untested on hardware.
*/
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <switch.h>

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif

/*
  Directory descriptors. Horizon cannot open() a directory, but DurableFile
  addresses files relative to one. A "directory fd" here is a sentinel number
  that maps to the directory's path; the *at() calls below resolve through it.
*/
#define DIRFD_BASE	20000
#define DIRFD_MAX	16
static char dirfd_path[ DIRFD_MAX ][ 512 ];

int switch_dirfd_open( const char *path ) {
	struct stat st;
	if ( stat( path, &st ) != 0 ) {
		return -1;
	}
	if ( !S_ISDIR( st.st_mode ) ) {
		errno = ENOTDIR;
		return -1;
	}
	for ( int i = 0; i < DIRFD_MAX; i++ ) {
		if ( dirfd_path[ i ][ 0 ] == '\0' ) {
			strncpy( dirfd_path[ i ], path, sizeof( dirfd_path[ i ] ) - 1 );
			return DIRFD_BASE + i;
		}
	}
	errno = EMFILE;
	return -1;
}

int switch_close( int fd ) {
	if ( fd >= DIRFD_BASE && fd < DIRFD_BASE + DIRFD_MAX ) {
		dirfd_path[ fd - DIRFD_BASE ][ 0 ] = '\0';
		return 0;
	}
	return close( fd );
}

int switch_fsync( int fd ) {
	if ( fd >= DIRFD_BASE && fd < DIRFD_BASE + DIRFD_MAX ) {
		return 0;	// no directory sync on FAT/exFAT
	}
	return fsync( fd );
}

/* Resolve ( dirfd, path ) into a full path. Returns 1 on success. */
static int resolve( int dirfd, const char *path, char *out, size_t size ) {
	if ( dirfd == AT_FDCWD || ( path && ( path[ 0 ] == '/' || strchr( path, ':' ) ) ) ) {
		snprintf( out, size, "%s", path );
		return 1;
	}
	if ( dirfd >= DIRFD_BASE && dirfd < DIRFD_BASE + DIRFD_MAX && dirfd_path[ dirfd - DIRFD_BASE ][ 0 ] ) {
		snprintf( out, size, "%s/%s", dirfd_path[ dirfd - DIRFD_BASE ], path );
		return 1;
	}
	errno = EBADF;
	return 0;
}

int execvp( const char *file, char *const argv[] ) { (void)file; (void)argv; errno = ENOSYS; return -1; }
int fdatasync( int fd ) { return fsync( fd ); }
int flock( int fd, int op ) { (void)fd; (void)op; return 0; }
uid_t geteuid( void ) { return 1000; }
int getpagesize( void ) { return 4096; }
int pthread_sigmask( int how, const sigset_t *set, sigset_t *old ) { (void)how; (void)set; if ( old ) memset( old, 0, sizeof( *old ) ); return 0; }

int sigaction( int sig, const struct sigaction *act, struct sigaction *old ) {
	void ( *previous )( int ) = SIG_DFL;
	if ( act ) {
		previous = signal( sig, act->sa_handler );
	} else {
		previous = signal( sig, SIG_DFL );
		signal( sig, previous );
	}
	if ( old ) {
		memset( old, 0, sizeof( *old ) );
		old->sa_handler = previous;
	}
	return previous == SIG_ERR ? -1 : 0;
}

long sysconf( int name ) {
	switch ( name ) {
	case _SC_PAGESIZE:			return 4096;
	case _SC_NPROCESSORS_ONLN:
	case _SC_NPROCESSORS_CONF:	return 3;	// cores 0-2 belong to the application
	case _SC_CLK_TCK:			return 100;
	case _SC_PHYS_PAGES:
	case _SC_AVPHYS_PAGES: {
		u64 bytes = 0;
		svcGetInfo( &bytes, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0 );
		return (long)( bytes / 4096 );
	}
	default:					errno = EINVAL; return -1;
	}
}

int openat( int dirfd, const char *path, int flags, ... ) {
	char full[ 1024 ];
	int mode = 0;
	if ( flags & O_CREAT ) { va_list ap; va_start( ap, flags ); mode = va_arg( ap, int ); va_end( ap ); }
	return resolve( dirfd, path, full, sizeof( full ) ) ? open( full, flags, mode ) : -1;
}
int fstatat( int dirfd, const char *path, struct stat *st, int flags ) {
	char full[ 1024 ];
	(void)flags;
	return resolve( dirfd, path, full, sizeof( full ) ) ? stat( full, st ) : -1;
}
int unlinkat( int dirfd, const char *path, int flags ) {
	char full[ 1024 ];
	if ( !resolve( dirfd, path, full, sizeof( full ) ) ) return -1;
	return ( flags & 0x200 ) ? rmdir( full ) : unlink( full );
}
int renameat( int od, const char *op, int nd, const char *np ) {
	char from[ 1024 ], to[ 1024 ];
	if ( !resolve( od, op, from, sizeof( from ) ) || !resolve( nd, np, to, sizeof( to ) ) ) return -1;
	/* POSIX rename replaces an existing file; FAT's does not. */
	unlink( to );
	return rename( from, to );
}
/* A hard link is emulated by a copy: the source stays, as it would after link(). */
int linkat( int od, const char *op, int nd, const char *np, int flags ) {
	char from[ 1024 ], to[ 1024 ];
	struct stat st;
	char buf[ 4096 ];
	(void)flags;
	if ( !resolve( od, op, from, sizeof( from ) ) || !resolve( nd, np, to, sizeof( to ) ) ) return -1;
	if ( stat( to, &st ) == 0 ) { errno = EEXIST; return -1; }
	FILE *in = fopen( from, "rb" );
	if ( !in ) return -1;
	FILE *out = fopen( to, "wb" );
	if ( !out ) { fclose( in ); return -1; }
	size_t n;
	int rc = 0;
	while ( ( n = fread( buf, 1, sizeof( buf ), in ) ) > 0 ) {
		if ( fwrite( buf, 1, n, out ) != n ) { rc = -1; break; }
	}
	fclose( in );
	if ( fclose( out ) != 0 ) rc = -1;
	if ( rc != 0 ) unlink( to );
	return rc;
}

/*
  Application start-up hooks called by libnx's crt0 before main().
*/
#include <switch.h>

#define SWITCH_DATA_DIR "sdmc:/switch/openQ4"

/* Boot trace: one line per stage in boot.txt, so a crash can be placed even without a log. */
void switch_mark( const char *stage ) {
	FILE *f = fopen( SWITCH_DATA_DIR "/boot.txt", "a" );
	if ( f ) {
		fputs( stage, f );
		fputc( '\n', f );
		fclose( f );
	}
}

/* Process memory in use, for bring-up logging. */
unsigned int switch_mem_used_mb( void ) {
	u64 used = 0;
	svcGetInfo( &used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0 );
	return (unsigned int)( used >> 20 );
}

/* Flush denormals to zero (FPCR.FZ): subnormal floats are slow on Cortex-A57 and show up in
   physics/audio math. FPCR is per thread, so worker threads call this too if they matter. */
void switch_set_ftz( void ) {
	unsigned long fpcr;
	__asm__ volatile( "mrs %0, fpcr" : "=r"( fpcr ) );
	fpcr |= ( 1UL << 24 );
	__asm__ volatile( "msr fpcr, %0" : : "r"( fpcr ) );
}

static void __attribute__(( constructor( 101 ) )) switch_first_ctor( void ) {
	switch_set_ftz();
	switch_mark( "static constructors start" );
}

#include <pthread.h>
/* Horizon gives applications cores 0-2. Without a mask the kernel moves threads between them,
   and two busy threads on one core time-slice in 10 ms steps. Pin the busy ones apart:
   main on 0, render thread on 1, audio and light helpers on 2. */
void switch_pin_current_thread( int core ) {
	svcSetThreadCoreMask( CUR_THREAD_HANDLE, core, 1u << core );
}

static void *switch_log_flusher( void *arg ) {
	(void)arg;
	switch_pin_current_thread( 2 );
	for ( ;; ) {
		svcSleepThread( 2000000000ULL );	// 2 s
		fflush( stdout );
	}
	return NULL;
}


/*
===============================================================================

	Sampling profiler (bring-up). With sdmc:/switch/openQ4/profile.txt present, a helper thread
	pauses the main thread and the render thread 500 times a second, reads where each one is
	(pc, link register and the frame-pointer chain), resumes it and appends the sample to
	sdmc:/switch/openQ4/prof.bin. tools/switch/analyze_prof.py turns that into per-function self and
	inclusive time. While a thread is paused the sampler makes no libc calls (a paused thread may
	hold the allocator lock), only svc calls and plain memory reads.

===============================================================================
*/
#define PROF_MAX_THREADS	4
#define PROF_MAX_DEPTH		16
#define PROF_BUF_SIZE		( 1 << 18 )

static Handle			prof_handle[ PROF_MAX_THREADS ];
static volatile int		prof_threads = 0;
static volatile int		prof_phase = 0;
static volatile int		prof_enabled = 0;
static unsigned char	prof_buf[ PROF_BUF_SIZE ];
static unsigned int		prof_len = 0;
static int				prof_fd = -1;

extern int main( int argc, char **argv );

void switch_prof_set_phase( int phase ) {
	prof_phase = phase;
}

/* Called by the render thread on itself (slot 1). */
void switch_prof_register_render( void ) {
	if ( prof_enabled ) {
		Thread *t = threadGetSelf();
		if ( t && t->handle ) {
			prof_handle[ 1 ] = t->handle;
			if ( prof_threads < 2 ) {
				prof_threads = 2;
			}
		}
	}
}

static void prof_sample( int slot ) {
	Handle h = prof_handle[ slot ];
	ThreadContext ctx;
	unsigned long long pcs[ PROF_MAX_DEPTH ];
	int n = 0;
	if ( h == 0 || R_FAILED( svcSetThreadActivity( h, ThreadActivity_Paused ) ) ) {
		return;
	}
	if ( R_SUCCEEDED( svcGetThreadContext3( &ctx, h ) ) ) {
		unsigned long long fp = ctx.fp;
		const unsigned long long sp = ctx.sp;
		pcs[ n++ ] = ctx.pc.x;
		pcs[ n++ ] = ctx.lr;
		// walk the AArch64 frame records: [fp] = previous fp, [fp + 8] = return address
		while ( n < PROF_MAX_DEPTH && fp != 0 && ( fp & 15 ) == 0 && fp >= sp && fp - sp < 0x20000ULL ) {
			const unsigned long long prev = ( (const unsigned long long *)fp )[ 0 ];
			const unsigned long long ret = ( (const unsigned long long *)fp )[ 1 ];
			if ( ret == 0 ) {
				break;
			}
			pcs[ n++ ] = ret;
			if ( prev <= fp || prev - fp > 0x20000ULL ) {
				break;
			}
			fp = prev;
		}
	}
	svcSetThreadActivity( h, ThreadActivity_Runnable );
	if ( n > 0 && prof_len + 3 + (unsigned int)n * 8 <= PROF_BUF_SIZE ) {
		prof_buf[ prof_len++ ] = (unsigned char)slot;
		prof_buf[ prof_len++ ] = (unsigned char)prof_phase;
		prof_buf[ prof_len++ ] = (unsigned char)n;
		memcpy( prof_buf + prof_len, pcs, (size_t)n * 8 );
		prof_len += (unsigned int)n * 8;
	}
}

static void *switch_prof_thread( void *arg ) {
	unsigned long long lastFlush = armGetSystemTick();
	(void)arg;
	switch_pin_current_thread( 2 );
	prof_fd = open( SWITCH_DATA_DIR "/prof.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644 );
	if ( prof_fd >= 0 ) {
		// header: magic and the address of main(), so offsets can be recovered from the ELF symbols
		const unsigned long long header[ 2 ] = { 0x31464f5251344f51ULL, (unsigned long long)(uintptr_t)&main };
		write( prof_fd, header, sizeof( header ) );
	}
	for ( ;; ) {
		svcSleepThread( 2000000ULL );	// 2 ms
		for ( int t = 0; t < prof_threads; t++ ) {
			prof_sample( t );
		}
		if ( prof_fd >= 0 && ( prof_len > PROF_BUF_SIZE - 1024 || armGetSystemTick() - lastFlush > 4ULL * 19200000ULL ) ) {
			write( prof_fd, prof_buf, prof_len );
			prof_len = 0;
			lastFlush = armGetSystemTick();
		}
	}
	return NULL;
}

static void switch_prof_start( void ) {
	FILE *f = fopen( SWITCH_DATA_DIR "/profile.txt", "r" );
	pthread_t thread;
	if ( !f ) {
		return;
	}
	fclose( f );
	prof_handle[ 0 ] = envGetMainThreadHandle();
	prof_threads = 1;
	prof_enabled = 1;
	pthread_create( &thread, NULL, switch_prof_thread, NULL );
}

void userAppInit( void ) {
	switch_mark( "userAppInit" );
	switch_pin_current_thread( 0 );
	// OpenAL Soft settings (output format, buffering) live next to the game.
	setenv( "ALSOFT_CONF", SWITCH_DATA_DIR "/alsoft.conf", 1 );
	// Networking for multiplayer and for nxlink log streaming (`nxlink -s`).
	socketInitializeDefault();
	int nxlinkFd = nxlinkStdio();
	// Quake 4 data (q4base/) lives next to the NRO; relative paths resolve from here.
	chdir( SWITCH_DATA_DIR );
	switch_prof_start();
	switch_mark( "userAppInit done" );
	// Without a nxlink listener, keep the log on the SD card instead (unbuffered, so it survives a crash).
	if ( nxlinkFd < 0 ) {
		if ( freopen( "log.txt", "w", stdout ) ) {
			// One SD write per line starves the fs service; buffer and flush on a timer instead.
			static char logBuffer[ 65536 ];
			setvbuf( stdout, logBuffer, _IOFBF, sizeof( logBuffer ) );
			dup2( fileno( stdout ), fileno( stderr ) );
			setvbuf( stderr, NULL, _IONBF, 0 );
			printf( "openQ4 Switch log start\n" );
			pthread_t flusher;
			pthread_create( &flusher, NULL, switch_log_flusher, NULL );
		}
	}
}

void userAppExit( void ) {
	socketExit();
}
