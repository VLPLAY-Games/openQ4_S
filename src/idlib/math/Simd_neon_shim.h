#ifndef __MATH_SIMD_NEON_SHIM_H__
#define __MATH_SIMD_NEON_SHIM_H__

/*
===============================================================================

	The SSE2 intrinsics Simd_SSE2.cpp uses, expressed with AArch64 NEON, so the
	same routines (and the same floating point evaluation order) run on ARM.
	Only the operations that file needs are provided.

	Min/max follow SSE semantics, not NEON's: _mm_min_ps( a, b ) is
	( a < b ) ? a : b, so a NaN in either operand yields b (NEON's vminq would
	propagate the NaN).

===============================================================================
*/

#if defined( __aarch64__ )

#include <arm_neon.h>

typedef float32x4_t	__m128;
typedef int32x4_t	__m128i;

typedef float		idNeonV4sf __attribute__( ( vector_size( 16 ) ) );
typedef int			idNeonV4si __attribute__( ( vector_size( 16 ) ) );

#define _MM_SHUFFLE( z, y, x, w )	( ( ( z ) << 6 ) | ( ( y ) << 4 ) | ( ( x ) << 2 ) | ( w ) )

static inline __m128 _mm_loadu_ps( const float *p ) { return vld1q_f32( p ); }
static inline void _mm_storeu_ps( float *p, __m128 v ) { vst1q_f32( p, v ); }
static inline __m128 _mm_set1_ps( float f ) { return vdupq_n_f32( f ); }
static inline __m128 _mm_setzero_ps( void ) { return vdupq_n_f32( 0.0f ); }
static inline __m128 _mm_set_ps( float e3, float e2, float e1, float e0 ) {
	const float lanes[4] = { e0, e1, e2, e3 };
	return vld1q_f32( lanes );
}

static inline __m128 _mm_add_ps( __m128 a, __m128 b ) { return vaddq_f32( a, b ); }
static inline __m128 _mm_sub_ps( __m128 a, __m128 b ) { return vsubq_f32( a, b ); }
static inline __m128 _mm_mul_ps( __m128 a, __m128 b ) { return vmulq_f32( a, b ); }

static inline __m128 _mm_min_ps( __m128 a, __m128 b ) { return vbslq_f32( vcltq_f32( a, b ), a, b ); }
static inline __m128 _mm_max_ps( __m128 a, __m128 b ) { return vbslq_f32( vcgtq_f32( a, b ), a, b ); }

static inline __m128 _mm_and_ps( __m128 a, __m128 b ) { return vreinterpretq_f32_u32( vandq_u32( vreinterpretq_u32_f32( a ), vreinterpretq_u32_f32( b ) ) ); }
static inline __m128 _mm_or_ps( __m128 a, __m128 b ) { return vreinterpretq_f32_u32( vorrq_u32( vreinterpretq_u32_f32( a ), vreinterpretq_u32_f32( b ) ) ); }
static inline __m128 _mm_xor_ps( __m128 a, __m128 b ) { return vreinterpretq_f32_u32( veorq_u32( vreinterpretq_u32_f32( a ), vreinterpretq_u32_f32( b ) ) ); }

static inline __m128 _mm_cmpgt_ps( __m128 a, __m128 b ) { return vreinterpretq_f32_u32( vcgtq_f32( a, b ) ); }
static inline __m128 _mm_cmpge_ps( __m128 a, __m128 b ) { return vreinterpretq_f32_u32( vcgeq_f32( a, b ) ); }
static inline __m128 _mm_cmplt_ps( __m128 a, __m128 b ) { return vreinterpretq_f32_u32( vcltq_f32( a, b ) ); }
static inline __m128 _mm_cmple_ps( __m128 a, __m128 b ) { return vreinterpretq_f32_u32( vcleq_f32( a, b ) ); }

static inline __m128i _mm_castps_si128( __m128 v ) { return vreinterpretq_s32_f32( v ); }
static inline __m128 _mm_castsi128_ps( __m128i v ) { return vreinterpretq_f32_s32( v ); }

static inline __m128i _mm_set1_epi32( int v ) { return vdupq_n_s32( v ); }
static inline __m128i _mm_set1_epi8( char v ) { return vreinterpretq_s32_s8( vdupq_n_s8( v ) ); }
static inline __m128i _mm_set_epi32( int e3, int e2, int e1, int e0 ) {
	const int lanes[4] = { e0, e1, e2, e3 };
	return vld1q_s32( lanes );
}
static inline __m128i _mm_sub_epi32( __m128i a, __m128i b ) { return vsubq_s32( a, b ); }
static inline __m128i _mm_and_si128( __m128i a, __m128i b ) { return vandq_s32( a, b ); }
static inline __m128i _mm_or_si128( __m128i a, __m128i b ) { return vorrq_s32( a, b ); }
#define _mm_srli_epi32( a, n )	vreinterpretq_s32_u32( vshrq_n_u32( vreinterpretq_u32_s32( a ), ( n ) ) )

static inline __m128i _mm_loadu_si128( const __m128i *p ) { return vld1q_s32( (const int *)p ); }
static inline void _mm_storeu_si128( __m128i *p, __m128i v ) { vst1q_s32( (int *)p, v ); }

// signed saturating packs: 4 + 4 int32 -> 8 int16, 8 + 8 int16 -> 16 int8
static inline __m128i _mm_packs_epi32( __m128i a, __m128i b ) {
	return vreinterpretq_s32_s16( vcombine_s16( vqmovn_s32( a ), vqmovn_s32( b ) ) );
}
static inline __m128i _mm_packs_epi16( __m128i a, __m128i b ) {
	return vreinterpretq_s32_s8( vcombine_s8( vqmovn_s16( vreinterpretq_s16_s32( a ) ), vqmovn_s16( vreinterpretq_s16_s32( b ) ) ) );
}

// result = { a[imm & 3], a[( imm >> 2 ) & 3], b[( imm >> 4 ) & 3], b[( imm >> 6 ) & 3] }
#define _mm_shuffle_ps( a, b, imm ) \
	( (__m128)__builtin_shuffle( (idNeonV4sf)( a ), (idNeonV4sf)( b ), \
		(idNeonV4si){ ( imm ) & 3, ( ( imm ) >> 2 ) & 3, 4 + ( ( ( imm ) >> 4 ) & 3 ), 4 + ( ( ( imm ) >> 6 ) & 3 ) } ) )

// 4x4 transpose of the rows r0..r3
#define _MM_TRANSPOSE4_PS( r0, r1, r2, r3 ) \
	do { \
		const float32x4x2_t idT01 = vtrnq_f32( ( r0 ), ( r1 ) ); \
		const float32x4x2_t idT23 = vtrnq_f32( ( r2 ), ( r3 ) ); \
		( r0 ) = vcombine_f32( vget_low_f32( idT01.val[0] ), vget_low_f32( idT23.val[0] ) ); \
		( r1 ) = vcombine_f32( vget_low_f32( idT01.val[1] ), vget_low_f32( idT23.val[1] ) ); \
		( r2 ) = vcombine_f32( vget_high_f32( idT01.val[0] ), vget_high_f32( idT23.val[0] ) ); \
		( r3 ) = vcombine_f32( vget_high_f32( idT01.val[1] ), vget_high_f32( idT23.val[1] ) ); \
	} while ( 0 )

#endif /* __aarch64__ */

#endif /* !__MATH_SIMD_NEON_SHIM_H__ */
