// Switch has no terminal line discipline; tcgetattr fails so the engine disables tty input.
#pragma once
#include <errno.h>
struct termios { unsigned c_iflag, c_oflag, c_cflag, c_lflag; unsigned char c_cc[32]; };
#define ECHO	0x1
#define ICANON	0x2
#define ISTRIP	0x20
#define INPCK	0x10
#define VMIN	6
#define VTIME	5
#define TCSADRAIN	1
static inline int tcgetattr( int fd, struct termios *t ) { (void)fd; (void)t; errno = ENOTTY; return -1; }
static inline int tcsetattr( int fd, int a, const struct termios *t ) { (void)fd; (void)a; (void)t; errno = ENOTTY; return -1; }
