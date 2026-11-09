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

static void __attribute__(( constructor( 101 ) )) switch_first_ctor( void ) {
	switch_mark( "static constructors start" );
}

#include <pthread.h>
static void *switch_log_flusher( void *arg ) {
	(void)arg;
	for ( ;; ) {
		svcSleepThread( 2000000000ULL );	// 2 s
		fflush( stdout );
	}
	return NULL;
}

void userAppInit( void ) {
	switch_mark( "userAppInit" );
	// Networking for multiplayer and for nxlink log streaming (`nxlink -s`).
	socketInitializeDefault();
	int nxlinkFd = nxlinkStdio();
	// Quake 4 data (q4base/) lives next to the NRO; relative paths resolve from here.
	chdir( SWITCH_DATA_DIR );
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
