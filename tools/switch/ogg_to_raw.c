/*
 * Decode an Ogg Vorbis file to raw little-endian s16 (interleaved) with the same stb_vorbis the engine uses,
 * for comparing against the pcm_N.raw dumps the Switch build writes (see tools/switch/compare_pcm.py).
 *
 *   gcc -O2 -o ogg_to_raw tools/switch/ogg_to_raw.c -Isubprojects/stb_vorbis -lm
 *   ./ogg_to_raw in.ogg out.raw
 */
#include <stdio.h>
#include <stdlib.h>
#include "stb_vorbis.c"

int main( int argc, char **argv ) {
	int channels, rate;
	short *pcm;
	int frames;
	FILE *f;
	if ( argc < 3 ) {
		fprintf( stderr, "usage: ogg_to_raw in.ogg out.raw\n" );
		return 1;
	}
	frames = stb_vorbis_decode_filename( argv[1], &channels, &rate, &pcm );
	if ( frames < 0 ) {
		fprintf( stderr, "decode failed\n" );
		return 1;
	}
	f = fopen( argv[2], "wb" );
	fwrite( pcm, sizeof( short ) * channels, (size_t)frames, f );
	fclose( f );
	printf( "frames=%d channels=%d rate=%d\n", frames, channels, rate );
	return 0;
}
