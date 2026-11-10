/*
===============================================================================

	Audio diagnostics (bring-up). Two views of the same stream are watched from the audio thread:

	  f32  what OpenAL hands the SDL stream (float, 48 kHz after the rate fix, stereo)
	  s16  what the audout driver finally submits to the hardware

	Every 5 s a summary line goes to the log:

	  audio: pulls=N frames=N mix_us(avg/max)=A/M budget_us=B peak=P clip=C nan=N clicks=K
	         zeroRun=Z under=U | s16 peak=P clip=C clicks=K

	  mix_us   time OpenAL took to render one chunk; if max approaches budget_us, the mixer
	           starves the output (underrun, heard as crackle)
	  clip     samples whose magnitude exceeded 1.0 (float) or hit full scale (s16): distortion
	  clicks   sample-to-sample steps larger than half of full scale: discontinuities
	  zeroRun  longest run of all-zero frames inside a chunk: gaps where nothing was mixed
	  under    audout buffers that came back late (the queue ran dry)

	The last 8 seconds of both streams are kept in memory. They are written to
	sdmc:/switch/openQ4/audio_N_f32.raw / audio_N_s16.raw (+ audio_N.txt) when a dump is requested:
	holding ZL + ZR + Minus for a second, or automatically when a summary interval shows
	underruns, many clicks or clipping (at most 4 dumps per run). tools/switch/analyze_audio.py
	reads them.

===============================================================================
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <switch.h>

#define DIAG_RATE			48000
#define DIAG_SECONDS		8
#define DIAG_FRAMES			( DIAG_RATE * DIAG_SECONDS )
#define DIAG_DIR			"sdmc:/switch/openQ4"
#define DIAG_MAX_DUMPS		4

static float *	ring_f32;		// DIAG_FRAMES stereo frames
static short *	ring_s16;
static unsigned int f32_pos, f32_fill, s16_pos, s16_fill;

static struct {
	unsigned long long	pulls, frames, mixTicksSum, mixTicksMax;
	float				peak;
	unsigned int		clip, nan, clicks, zeroRunMax;
	unsigned int		under;
	int					s16peak;
	unsigned int		s16clip, s16clicks;
	unsigned long long	start;
	unsigned int		budgetUs;
} stats;

static volatile int		dump_requested = 0;
static volatile int		dump_running = 0;
static int				dump_count = 0;
static int				dump_reason_auto = 0;
static int				dump_reason_user = 0;
static unsigned int		spec_freq, spec_channels, spec_samples;

static void diag_init( void ) {
	if ( !ring_f32 ) {
		ring_f32 = (float *)calloc( (size_t)DIAG_FRAMES * 2, sizeof( float ) );
		ring_s16 = (short *)calloc( (size_t)DIAG_FRAMES * 2, sizeof( short ) );
		stats.start = armGetSystemTick();
	}
}

/* what OpenAL asked for, and what was granted */
void switch_audio_diag_spec( int reqFreq, int reqChannels, int reqSamples, int format, int gotFreq ) {
	spec_freq = (unsigned int)gotFreq;
	spec_channels = (unsigned int)reqChannels;
	spec_samples = (unsigned int)reqSamples;
	printf( "audio: OpenAL requested freq=%d channels=%d samples=%d format=0x%04x; stream opened at %d Hz\n",
			reqFreq, reqChannels, reqSamples, format, gotFreq );
}

void switch_audio_diag_request_dump( int user ) {
	if ( dump_count < DIAG_MAX_DUMPS && !dump_running ) {
		dump_reason_user = user;
		dump_requested = 1;
	}
}

/* ZL + ZR + Minus held for a second (called from the joystick driver every poll) */
void switch_audio_diag_pad( int comboDown ) {
	static unsigned long long since = 0;
	static int fired = 0;
	if ( !comboDown ) {
		since = 0;
		fired = 0;
		return;
	}
	if ( since == 0 ) {
		since = armGetSystemTick();
	} else if ( !fired && armGetSystemTick() - since > 19200000ULL ) {
		fired = 1;
		printf( "audio: dump requested from the controller\n" );
		switch_audio_diag_request_dump( 1 );
	}
}

static void *dump_thread( void *arg ) {
	char path[256];
	const int n = dump_count;
	unsigned int f32n, s16n, f32start, s16start;
	float *f32copy;
	short *s16copy;
	(void)arg;

	f32n = f32_fill;
	s16n = s16_fill;
	f32start = ( f32_pos + DIAG_FRAMES - f32n ) % DIAG_FRAMES;
	s16start = ( s16_pos + DIAG_FRAMES - s16n ) % DIAG_FRAMES;
	f32copy = (float *)malloc( (size_t)f32n * 2 * sizeof( float ) );
	s16copy = (short *)malloc( (size_t)s16n * 2 * sizeof( short ) );
	if ( !f32copy || !s16copy ) {
		free( f32copy );
		free( s16copy );
		dump_running = 0;
		return NULL;
	}
	// linearize the rings (the audio thread keeps writing while this runs)
	for ( unsigned int i = 0; i < f32n; i++ ) {
		const unsigned int k = ( f32start + i ) % DIAG_FRAMES;
		f32copy[i * 2] = ring_f32[k * 2];
		f32copy[i * 2 + 1] = ring_f32[k * 2 + 1];
	}
	for ( unsigned int i = 0; i < s16n; i++ ) {
		const unsigned int k = ( s16start + i ) % DIAG_FRAMES;
		s16copy[i * 2] = ring_s16[k * 2];
		s16copy[i * 2 + 1] = ring_s16[k * 2 + 1];
	}

	snprintf( path, sizeof( path ), DIAG_DIR "/audio_%d_f32.raw", n );
	int fd = open( path, O_WRONLY | O_CREAT | O_TRUNC, 0644 );
	if ( fd >= 0 ) {
		write( fd, f32copy, (size_t)f32n * 2 * sizeof( float ) );
		close( fd );
	}
	snprintf( path, sizeof( path ), DIAG_DIR "/audio_%d_s16.raw", n );
	fd = open( path, O_WRONLY | O_CREAT | O_TRUNC, 0644 );
	if ( fd >= 0 ) {
		write( fd, s16copy, (size_t)s16n * 2 * sizeof( short ) );
		close( fd );
	}
	snprintf( path, sizeof( path ), DIAG_DIR "/audio_%d.txt", n );
	FILE *f = fopen( path, "w" );
	if ( f ) {
		fprintf( f, "reason=%s\nf32_frames=%u\ns16_frames=%u\nrate=%d\nopenal_freq=%u channels=%u samples=%u\n",
				dump_reason_user ? "controller" : "automatic", f32n, s16n, DIAG_RATE, spec_freq, spec_channels, spec_samples );
		fclose( f );
	}
	free( f32copy );
	free( s16copy );
	printf( "audio: dump %d written (%u f32 frames, %u s16 frames)\n", n, f32n, s16n );
	dump_running = 0;
	return NULL;
}

static void maybe_dump( void ) {
	if ( dump_requested && !dump_running && dump_count < DIAG_MAX_DUMPS ) {
		pthread_t t;
		dump_requested = 0;
		dump_running = 1;
		dump_count++;
		if ( pthread_create( &t, NULL, dump_thread, NULL ) == 0 ) {
			pthread_detach( t );
		} else {
			dump_running = 0;
		}
	}
}

static void report( void ) {
	const unsigned long long now = armGetSystemTick();
	if ( now - stats.start < 5ULL * 19200000ULL ) {
		return;
	}
	if ( stats.pulls > 0 ) {
		printf( "audio: pulls=%llu frames=%llu mix_us(avg/max)=%llu/%llu budget_us=%u peak=%.3f clip=%u nan=%u clicks=%u zeroRun=%u under=%u | s16 peak=%d clip=%u clicks=%u\n",
				stats.pulls, stats.frames,
				( stats.mixTicksSum * 10ULL / 192ULL ) / stats.pulls, stats.mixTicksMax * 10ULL / 192ULL, stats.budgetUs,
				stats.peak, stats.clip, stats.nan, stats.clicks, stats.zeroRunMax, stats.under,
				stats.s16peak, stats.s16clip, stats.s16clicks );
		// automatic dump: the interval was audibly bad
		if ( stats.under > 0 || stats.clicks > 40 || stats.clip > 200 ) {
			dump_reason_user = 0;
			dump_requested = 1;
		}
	}
	memset( &stats, 0, sizeof( stats ) );
	stats.start = now;
}

/* one chunk of float output from OpenAL; mixTicks is the time its callback took */
void switch_audio_diag_f32( const float *x, int frames, int channels, unsigned long long mixTicks ) {
	unsigned int zeros = 0;
	diag_init();
	stats.pulls++;
	stats.frames += (unsigned long long)frames;
	stats.mixTicksSum += mixTicks;
	if ( mixTicks > stats.mixTicksMax ) {
		stats.mixTicksMax = mixTicks;
	}
	stats.budgetUs = (unsigned int)( (unsigned long long)frames * 1000000ULL / DIAG_RATE );
	for ( int i = 0; i < frames; i++ ) {
		float l = x[i * channels];
		float r = channels > 1 ? x[i * channels + 1] : l;
		const float al = fabsf( l ), ar = fabsf( r );
		if ( l != l || r != r || al > 1.0e6f || ar > 1.0e6f ) {
			stats.nan++;
			l = r = 0.0f;
		} else {
			if ( al > stats.peak ) stats.peak = al;
			if ( ar > stats.peak ) stats.peak = ar;
			if ( al > 1.0f ) stats.clip++;
			if ( ar > 1.0f ) stats.clip++;
		}
		if ( i > 0 ) {
			const float pl = x[( i - 1 ) * channels];
			if ( fabsf( l - pl ) > 0.5f ) {
				stats.clicks++;
			}
		}
		if ( l == 0.0f && r == 0.0f ) {
			zeros++;
			if ( zeros > stats.zeroRunMax ) stats.zeroRunMax = zeros;
		} else {
			zeros = 0;
		}
		ring_f32[f32_pos * 2] = l;
		ring_f32[f32_pos * 2 + 1] = r;
		f32_pos = ( f32_pos + 1 ) % DIAG_FRAMES;
		if ( f32_fill < DIAG_FRAMES ) f32_fill++;
	}
	maybe_dump();
}

/* the interleaved s16 stereo block the driver submits */
void switch_audio_diag_s16( const short *x, int frames ) {
	diag_init();
	for ( int i = 0; i < frames; i++ ) {
		const int l = x[i * 2], r = x[i * 2 + 1];
		const int al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
		if ( al > stats.s16peak ) stats.s16peak = al;
		if ( ar > stats.s16peak ) stats.s16peak = ar;
		if ( al >= 32767 ) stats.s16clip++;
		if ( ar >= 32767 ) stats.s16clip++;
		if ( i > 0 ) {
			const int d = l - x[( i - 1 ) * 2];
			if ( d > 16384 || d < -16384 ) stats.s16clicks++;
		}
		ring_s16[s16_pos * 2] = (short)l;
		ring_s16[s16_pos * 2 + 1] = (short)r;
		s16_pos = ( s16_pos + 1 ) % DIAG_FRAMES;
		if ( s16_fill < DIAG_FRAMES ) s16_fill++;
	}
	report();
}

void switch_audio_diag_underrun( int count ) {
	if ( count > 0 ) {
		stats.under += (unsigned int)count;
	}
}
