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

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif

static int usable( int dirfd, const char *path ) {
	if ( dirfd == AT_FDCWD || ( path && path[0] == '/' ) ) {
		return 1;
	}
	errno = ENOSYS;
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
	default:					errno = EINVAL; return -1;
	}
}

int openat( int dirfd, const char *path, int flags, ... ) {
	int mode = 0;
	if ( flags & O_CREAT ) { va_list ap; va_start( ap, flags ); mode = va_arg( ap, int ); va_end( ap ); }
	return usable( dirfd, path ) ? open( path, flags, mode ) : -1;
}
int fstatat( int dirfd, const char *path, struct stat *st, int flags ) { (void)flags; return usable( dirfd, path ) ? stat( path, st ) : -1; }
int unlinkat( int dirfd, const char *path, int flags ) { return usable( dirfd, path ) ? ( ( flags & 0x200 ) ? rmdir( path ) : unlink( path ) ) : -1; }
int renameat( int od, const char *op, int nd, const char *np ) { return ( usable( od, op ) && usable( nd, np ) ) ? rename( op, np ) : -1; }
int linkat( int od, const char *op, int nd, const char *np, int flags ) { (void)od; (void)op; (void)nd; (void)np; (void)flags; errno = ENOSYS; return -1; }

/*
  Application start-up hooks called by libnx's crt0 before main().
*/
#include <switch.h>

#define SWITCH_DATA_DIR "sdmc:/switch/openQ4"

void userAppInit( void ) {
	// Networking for multiplayer and for nxlink log streaming (`nxlink -s`).
	socketInitializeDefault();
	nxlinkStdio();
	// Quake 4 data (q4base/) lives next to the NRO; relative paths resolve from here.
	chdir( SWITCH_DATA_DIR );
}

void userAppExit( void ) {
	socketExit();
}
