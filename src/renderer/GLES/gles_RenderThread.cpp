// Copyright (C) 2026 DarkMatter Productions
#ifdef OPENQ4_RENDERER_GLES_MODULE
#include "../../idlib/precompiled.h"
#pragma hdrstop
#include "../tr_local.h"
#include "../RendererMetrics.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#if defined( __SWITCH__ )
#include <pthread.h>
#endif

/*
===============================================================================

	Render thread for the OpenGL ES module (mode 1 of the scheme the Vulkan
	module already uses, see Vulkan/vk_RenderThread.cpp).

	EndFrame hands the finished command list to this thread, which runs
	RB_ExecuteBackEndCommands while the main thread returns to the next frame's
	events, console commands, sound and game tic. The frame is joined by the
	first producer-side entry that needs it (R_RenderThread_Sync, called by every
	resource mutation and by the front end's first command buffer).

	An OpenGL context is current on one thread at a time, so the context is
	handed over with the frame: the main thread releases it before waking the
	worker, the worker makes it current for the frame and releases it again, and
	the main thread takes it back in R_RenderThread_Sync. Between handoff and
	Sync the main thread must not issue GL calls; those are exactly the
	resource mutations that already Sync first.

	Off unless r_glRenderThread is 1.

===============================================================================
*/

idCVar r_glRenderThread( "r_glRenderThread", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"run the OpenGL ES back end on its own thread, overlapping the next game tic (0 = inline)" );

// Releases the current context without the glFinish GLimp_DeactivateContext does:
// the GPU may keep working while the next thread picks the context up.
void GLimp_ReleaseContextNoFinish( void );

class renderThreadException_t : public idException {
public:
	renderThreadException_t( const char *message, bool isFatal ) : idException( message ), fatal( isFatal ) {}
	bool fatal;
};

void R_RenderThread_Error( const char *message, bool fatal ) {
	if ( R_RenderThread_OnRenderThread() ) {
		throw renderThreadException_t( message, fatal );
	}
	if ( fatal ) {
		common->FatalError( "%s", message );
	} else {
		common->Error( "%s", message );
	}
}

static frameData_t *rt_frame = NULL;
static std::mutex					rt_mutex;
static std::condition_variable		rt_wake;
static std::condition_variable		rt_done;
static const emptyCommand_t *		rt_commands = NULL;	// owned by the render thread while rt_busy
static bool							rt_busy = false;
static bool							rt_quit = false;
static bool							rt_failed = false;
static bool							rt_fatal = false;
static char							rt_failure[1024];
static int							rt_backEndMsec = 0;
static std::atomic<bool>			rt_inFlight( false );
static std::atomic<bool>			rt_started( false );
static std::atomic<bool>			rt_workerIdSet( false );

#if defined( __SWITCH__ )
static pthread_t					rt_pthread;
static pthread_t					rt_workerId;
#else
static std::thread					rt_thread;
static std::thread::id				rt_threadId;
#endif

static int							rt_framesThreaded = 0;
static int							rt_framesInline = 0;
static int							rt_framesCompleted = 0;
static int							rt_syncWaits = 0;
static long long					rt_syncWaitUsec = 0;


#if defined( __SWITCH__ )
/*
===============================================================================

	GL call profiling (Switch bring-up). The module's link wraps these entry
	points (-Wl,--wrap=..., tools/switch/gen_ninja.py); each wrapper times the
	real call with the ARM generic timer and files it under a category. The
	totals of the frame the render thread just finished are printed with
	com_speeds 1.

===============================================================================
*/
enum { GLW_DRAW, GLW_PROGRAM, GLW_TEXTURE, GLW_UNIFORM, GLW_ATTRIB, GLW_STATE, GLW_BUFFER, GLW_FBO, GLW_COUNT };
static const char *glw_names[GLW_COUNT] = { "draw", "prog", "tex", "uni", "attr", "state", "buf", "fbo" };
static unsigned long long glw_ticks[GLW_COUNT];
static unsigned int glw_calls[GLW_COUNT];

static inline unsigned long long glw_now( void ) {
	unsigned long long v;
	__asm__ volatile( "mrs %0, cntpct_el0" : "=r"( v ) );
	return v;
}

#define GLW( cat, name, params, args ) \
	extern "C" void __real_##name params; \
	extern "C" void __wrap_##name params { \
		const unsigned long long t0 = glw_now(); \
		__real_##name args; \
		glw_ticks[cat] += glw_now() - t0; \
		glw_calls[cat]++; \
	}


/*
	Redundant uniform elimination. Uniform values live in the program object, so a call that writes
	what the program already holds is pure driver overhead (Mesa validates and uploads it anyway). The
	interaction and material passes set 15+ uniforms per surface, most of them unchanged from the
	previous draw. Values are remembered per (program, location) and identical writes are dropped.
*/
static GLuint glw_currentProgram = 0;
static unsigned int glw_uniformSkipped = 0;
struct glwUniformEntry_t {
	GLuint			program;
	GLint			location;
	unsigned int	words;
	unsigned int	value[16];
};
static const unsigned int GLW_UNIFORM_CACHE_SIZE = 4096;
static glwUniformEntry_t glw_uniformCache[GLW_UNIFORM_CACHE_SIZE];

static inline glwUniformEntry_t &glw_uniformEntry( GLint location ) {
	const unsigned int h = ( glw_currentProgram * 131u + (unsigned int)location * 2654435761u ) >> 7;
	return glw_uniformCache[h & ( GLW_UNIFORM_CACHE_SIZE - 1 )];
}

// true when the program already holds exactly these words at this location
static inline bool glw_uniformUnchanged( GLint location, const void *data, unsigned int words ) {
	if ( location < 0 || words > 16 || glw_currentProgram == 0 ) {
		return false;
	}
	glwUniformEntry_t &e = glw_uniformEntry( location );
	if ( e.program == glw_currentProgram && e.location == location && e.words == words
			&& memcmp( e.value, data, words * sizeof( unsigned int ) ) == 0 ) {
		return true;
	}
	e.program = glw_currentProgram;
	e.location = location;
	e.words = words;
	memcpy( e.value, data, words * sizeof( unsigned int ) );
	return false;
}

static inline void glw_uniformForget( GLint location ) {
	if ( location >= 0 && glw_currentProgram != 0 ) {
		glw_uniformEntry( location ).program = 0;
	}
}

static void glw_forgetProgram( GLuint program ) {
	for ( unsigned int i = 0; i < GLW_UNIFORM_CACHE_SIZE; i++ ) {
		if ( glw_uniformCache[i].program == program ) {
			glw_uniformCache[i].program = 0;
		}
	}
}

extern "C" void __real_glUseProgram( GLuint a );
extern "C" void __wrap_glUseProgram( GLuint a ) {
	const unsigned long long t0 = glw_now();
	glw_currentProgram = a;
	__real_glUseProgram( a );
	glw_ticks[GLW_PROGRAM] += glw_now() - t0;
	glw_calls[GLW_PROGRAM]++;
}
extern "C" void __real_glLinkProgram( GLuint a );
extern "C" void __wrap_glLinkProgram( GLuint a ) {
	glw_forgetProgram( a );
	__real_glLinkProgram( a );
}
extern "C" void __real_glDeleteProgram( GLuint a );
extern "C" void __wrap_glDeleteProgram( GLuint a ) {
	glw_forgetProgram( a );
	__real_glDeleteProgram( a );
}

#define GLW_UNIFORM_SCALAR( name, params, args, words, ... ) \
	extern "C" void __real_##name params; \
	extern "C" void __wrap_##name params { \
		const unsigned long long t0 = glw_now(); \
		const unsigned int data[4] = { __VA_ARGS__ }; \
		if ( glw_uniformUnchanged( a, data, words ) ) { \
			glw_uniformSkipped++; \
		} else { \
			__real_##name args; \
		} \
		glw_ticks[GLW_UNIFORM] += glw_now() - t0; \
		glw_calls[GLW_UNIFORM]++; \
	}

static inline unsigned int glw_bits( float f ) { unsigned int u; memcpy( &u, &f, sizeof( u ) ); return u; }

GLW_UNIFORM_SCALAR( glUniform1f, ( GLint a, GLfloat b ), ( a, b ), 1, glw_bits( b ) )
GLW_UNIFORM_SCALAR( glUniform1i, ( GLint a, GLint b ), ( a, b ), 1, (unsigned int)b )
GLW_UNIFORM_SCALAR( glUniform2f, ( GLint a, GLfloat b, GLfloat c ), ( a, b, c ), 2, glw_bits( b ), glw_bits( c ) )
GLW_UNIFORM_SCALAR( glUniform3f, ( GLint a, GLfloat b, GLfloat c, GLfloat d ), ( a, b, c, d ), 3, glw_bits( b ), glw_bits( c ), glw_bits( d ) )
GLW_UNIFORM_SCALAR( glUniform4f, ( GLint a, GLfloat b, GLfloat c, GLfloat d, GLfloat e ), ( a, b, c, d, e ), 4, glw_bits( b ), glw_bits( c ), glw_bits( d ), glw_bits( e ) )

#define GLW_UNIFORM_VECTOR( name, words ) \
	extern "C" void __real_##name( GLint a, GLsizei b, const GLfloat *c ); \
	extern "C" void __wrap_##name( GLint a, GLsizei b, const GLfloat *c ) { \
		const unsigned long long t0 = glw_now(); \
		if ( b == 1 && glw_uniformUnchanged( a, c, words ) ) { \
			glw_uniformSkipped++; \
		} else { \
			if ( b != 1 ) { glw_uniformForget( a ); } \
			__real_##name( a, b, c ); \
		} \
		glw_ticks[GLW_UNIFORM] += glw_now() - t0; \
		glw_calls[GLW_UNIFORM]++; \
	}

GLW_UNIFORM_VECTOR( glUniform1fv, 1 )
GLW_UNIFORM_VECTOR( glUniform2fv, 2 )
GLW_UNIFORM_VECTOR( glUniform3fv, 3 )
GLW_UNIFORM_VECTOR( glUniform4fv, 4 )

extern "C" void __real_glUniformMatrix4fv( GLint a, GLsizei b, GLboolean c, const GLfloat *d );
extern "C" void __wrap_glUniformMatrix4fv( GLint a, GLsizei b, GLboolean c, const GLfloat *d ) {
	const unsigned long long t0 = glw_now();
	if ( b == 1 && c == GL_FALSE && glw_uniformUnchanged( a, d, 16 ) ) {
		glw_uniformSkipped++;
	} else {
		if ( b != 1 || c != GL_FALSE ) { glw_uniformForget( a ); }
		__real_glUniformMatrix4fv( a, b, c, d );
	}
	glw_ticks[GLW_UNIFORM] += glw_now() - t0;
	glw_calls[GLW_UNIFORM]++;
}

GLW( GLW_DRAW, glDrawElements, ( GLenum a, GLsizei b, GLenum c, const void *d ), ( a, b, c, d ) )
GLW( GLW_DRAW, glDrawArrays, ( GLenum a, GLint b, GLsizei c ), ( a, b, c ) )
GLW( GLW_TEXTURE, glBindTexture, ( GLenum a, GLuint b ), ( a, b ) )
GLW( GLW_TEXTURE, glActiveTexture, ( GLenum a ), ( a ) )
GLW( GLW_TEXTURE, glTexParameteri, ( GLenum a, GLenum b, GLint c ), ( a, b, c ) )
GLW( GLW_ATTRIB, glVertexAttribPointer, ( GLuint a, GLint b, GLenum c, GLboolean d, GLsizei e, const void *f ), ( a, b, c, d, e, f ) )
GLW( GLW_ATTRIB, glEnableVertexAttribArray, ( GLuint a ), ( a ) )
GLW( GLW_ATTRIB, glDisableVertexAttribArray, ( GLuint a ), ( a ) )
GLW( GLW_STATE, glEnable, ( GLenum a ), ( a ) )
GLW( GLW_STATE, glDisable, ( GLenum a ), ( a ) )
GLW( GLW_STATE, glBlendFunc, ( GLenum a, GLenum b ), ( a, b ) )
GLW( GLW_STATE, glDepthFunc, ( GLenum a ), ( a ) )
GLW( GLW_STATE, glDepthMask, ( GLboolean a ), ( a ) )
GLW( GLW_STATE, glColorMask, ( GLboolean a, GLboolean b, GLboolean c, GLboolean d ), ( a, b, c, d ) )
GLW( GLW_STATE, glStencilFunc, ( GLenum a, GLint b, GLuint c ), ( a, b, c ) )
GLW( GLW_STATE, glStencilOp, ( GLenum a, GLenum b, GLenum c ), ( a, b, c ) )
GLW( GLW_STATE, glCullFace, ( GLenum a ), ( a ) )
GLW( GLW_STATE, glScissor, ( GLint a, GLint b, GLsizei c, GLsizei d ), ( a, b, c, d ) )
GLW( GLW_STATE, glViewport, ( GLint a, GLint b, GLsizei c, GLsizei d ), ( a, b, c, d ) )
GLW( GLW_STATE, glPolygonOffset, ( GLfloat a, GLfloat b ), ( a, b ) )
GLW( GLW_BUFFER, glBindBuffer, ( GLenum a, GLuint b ), ( a, b ) )
GLW( GLW_BUFFER, glBufferSubData, ( GLenum a, GLintptr b, GLsizeiptr c, const void *d ), ( a, b, c, d ) )
GLW( GLW_BUFFER, glBindVertexArray, ( GLuint a ), ( a ) )
GLW( GLW_FBO, glBindFramebuffer, ( GLenum a, GLuint b ), ( a, b ) )
GLW( GLW_FBO, glClear, ( GLbitfield a ), ( a ) )

// Prints and clears the totals; call with the render thread idle.
static void R_GLW_ReportAndReset( void ) {
	char line[256];
	int n = idStr::snPrintf( line, sizeof( line ), "gl:" );
	for ( int i = 0; i < GLW_COUNT; i++ ) {
		n += idStr::snPrintf( line + n, sizeof( line ) - n, " %s=%u/%lluus", glw_names[i], glw_calls[i], ( glw_ticks[i] * 10ULL ) / 192ULL );
		glw_ticks[i] = 0;
		glw_calls[i] = 0;
	}
	n += idStr::snPrintf( line + n, sizeof( line ) - n, " uniSkipped=%u", glw_uniformSkipped );
	glw_uniformSkipped = 0;
	printf( "%s\n", line );
}
#endif

#if defined( __SWITCH__ )
extern "C" void switch_prof_register_render( void );
extern "C" void switch_prof_set_phase( int phase );
extern "C" unsigned int svcSetThreadCoreMask( unsigned int handle, int preferredCore, unsigned int affinityMask );
#endif

static void R_RenderThread_Main( void ) {
#if defined( __SWITCH__ )
	{
		// core 1: the main thread is pinned to 0 and the audio mixer to 2
		svcSetThreadCoreMask( 0xFFFF8000u, 1, 1u << 1 );
		switch_prof_register_render();
	}
	{
		// Denormals are slow on Cortex-A57; FPCR is per thread.
		unsigned long fpcr;
		__asm__ volatile( "mrs %0, fpcr" : "=r"( fpcr ) );
		fpcr |= ( 1UL << 24 );
		__asm__ volatile( "msr fpcr, %0" : : "r"( fpcr ) );
	}
#endif
	for ( ;; ) {
		const emptyCommand_t *cmds;
		{
			std::unique_lock<std::mutex> lock( rt_mutex );
			rt_wake.wait( lock, [] { return rt_quit || rt_commands != NULL; } );
			if ( rt_quit && rt_commands == NULL ) {
				return;
			}
			cmds = rt_commands;
		}

		bool failed = false;
		bool fatal = false;
		const int start = Sys_Milliseconds();
		GLimp_ActivateContext();
		R_SetBackEndFrameData( rt_frame );
		try {
			RB_ExecuteBackEndCommands( cmds );
		} catch ( const renderThreadException_t &ex ) {
			failed = true;
			fatal = ex.fatal;
			idStr::Copynz( rt_failure, ex.error, sizeof( rt_failure ) );
		} catch ( idException &ex ) {
			failed = true;
			idStr::Copynz( rt_failure, ex.error, sizeof( rt_failure ) );
		}
		R_RendererMetrics_MarkBackEndFinished();
		R_SetBackEndFrameData( NULL );
		// Give the context back before the main thread is told the frame is done.
		GLimp_ReleaseContextNoFinish();
		const int elapsed = Sys_Milliseconds() - start;

		{
			std::lock_guard<std::mutex> lock( rt_mutex );
			rt_commands = NULL;
			rt_busy = false;
			rt_failed = rt_failed || failed;
			rt_fatal = rt_fatal || fatal;
			rt_backEndMsec = elapsed;
		}
		rt_done.notify_all();
	}
}

#if defined( __SWITCH__ )
static void *R_RenderThread_Entry( void * ) {
	rt_workerId = pthread_self();
	rt_workerIdSet.store( true, std::memory_order_release );
	R_RenderThread_Main();
	return NULL;
}
#endif

static void R_RenderThread_Start( void ) {
	if ( rt_started.load() ) {
		return;
	}
	rt_quit = false;
#if defined( __SWITCH__ )
	// The default stack is small; the back end recurses through GUI and material code.
	pthread_attr_t attr;
	pthread_attr_init( &attr );
	pthread_attr_setstacksize( &attr, 4 * 1024 * 1024 );
	if ( pthread_create( &rt_pthread, &attr, R_RenderThread_Entry, NULL ) != 0 ) {
		common->Printf( "Render thread: pthread_create failed, rendering inline\n" );
		pthread_attr_destroy( &attr );
		return;
	}
	pthread_attr_destroy( &attr );
#else
	rt_thread = std::thread( R_RenderThread_Main );
	rt_threadId = rt_thread.get_id();
#endif
	rt_started.store( true );
	common->Printf( "Render thread started (r_glRenderThread %d)\n", r_glRenderThread.GetInteger() );
}

bool R_RenderThread_OnRenderThread( void ) {
	if ( !rt_started.load() ) {
		return false;
	}
#if defined( __SWITCH__ )
	return rt_workerIdSet.load( std::memory_order_acquire ) && pthread_equal( pthread_self(), rt_workerId );
#else
	return std::this_thread::get_id() == rt_threadId;
#endif
}

bool R_RenderThread_InFlight( void ) {
	return rt_inFlight.load( std::memory_order_acquire );
}

// This module only overlaps the back end with the game tic, never the front end.
bool R_RenderThread_OverlapRequested( void ) { return false; }
bool R_RenderThread_FrontEndMayOverlap( void ) { return false; }

void R_RenderThread_SyncFrontEnd( const char *reason ) {
	R_RenderThread_Sync( reason );
}

bool R_RenderThread_Begin( const emptyCommand_t *cmds ) {
	// Only gameplay frames run threaded. Start-up, loading and menu frames draw from client memory
	// the main thread is still rewriting (vertex cache temps, GUI quads): keep them inline.
#if defined( __SWITCH__ )
	switch_prof_set_phase( tr.primaryWorld != NULL ? 1 : 0 );
#endif
	if ( !r_glRenderThread.GetBool() || r_skipBackEnd.GetBool() || tr.primaryWorld == NULL || !R_RenderThread_FrameQualifies() ) {
		rt_framesInline++;
		return false;
	}
	R_RenderThread_Start();
	if ( !rt_started.load() ) {
		rt_framesInline++;
		return false;
	}
	R_CountFrameData();
	R_TemporalPresentation_SubmitFrame();
	rt_frame = frameData;
	backEnd.postProcessTexelSize = tr.postProcessTexelSize;
	backEnd.postProcessSourceColorSpace = tr.postProcessSourceColorSpace;
	backEnd.postProcessSMAAQuality = tr.postProcessSMAAQuality;

	// The context moves to the worker with the frame.
	GLimp_ReleaseContextNoFinish();
	{
		std::lock_guard<std::mutex> lock( rt_mutex );
		rt_commands = cmds;
		rt_busy = true;
		rt_inFlight.store( true, std::memory_order_release );
	}
	rt_wake.notify_one();
	rt_framesThreaded++;
	return true;
}

void R_RenderThread_Sync( const char *reason ) {
	( void )reason;
	if ( !rt_inFlight.load( std::memory_order_acquire ) || R_RenderThread_OnRenderThread() ) {
		return;
	}
	const unsigned long long waitStart = R_RendererMetrics_CpuClock();
	bool failed;
	bool fatal;
	char failure[sizeof( rt_failure )];
	int backEndMsec;
	{
		std::unique_lock<std::mutex> lock( rt_mutex );
		if ( rt_busy ) {
			rt_syncWaits++;
		}
		rt_done.wait( lock, [] { return !rt_busy; } );
		failed = rt_failed;
		fatal = rt_fatal;
		rt_failed = rt_fatal = false;
		idStr::Copynz( failure, rt_failure, sizeof( failure ) );
		backEndMsec = rt_backEndMsec;
	}
	rt_inFlight.store( false, std::memory_order_release );
	rt_framesCompleted++;
	const long long waitedUsec = static_cast<long long>( R_RendererMetrics_CpuClock() - waitStart );
	rt_syncWaitUsec += waitedUsec;
	if ( cvarSystem->GetCVarBool( "com_speeds" ) ) {
#if defined( __SWITCH__ )
		// straight to the log: the console buffer copies itself on every print
		printf( "rt: backend=%dms mainWaited=%dus (%s)\n", backEndMsec, (int)waitedUsec, reason );
#else
		common->Printf( "rt: backend=%dms mainWaited=%dus (%s)\n", backEndMsec, (int)waitedUsec, reason );
#endif
#if defined( __SWITCH__ )
		R_GLW_ReportAndReset();
#endif
	}

	// Take the context back before anything on this thread touches GL.
	GLimp_ActivateContext();

	R_SetBackEndFrameData( rt_frame );
	R_RendererMetrics_RecordSubmitMsec( backEndMsec );
	tr.CompleteBackEndFrame();
	R_SetBackEndFrameData( NULL );
	rt_frame = NULL;

	if ( fatal ) {
		common->FatalError( "render thread: %s", failure );
	}
	if ( failed ) {
		common->Error( "render thread: %s", failure );
	}
}

void R_RenderThread_Shutdown( void ) {
	if ( R_RenderThread_OnRenderThread() ) {
		return;
	}
	R_RenderThread_Sync( "shutdown" );
	if ( !rt_started.load() ) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock( rt_mutex );
		rt_quit = true;
	}
	rt_wake.notify_all();
#if defined( __SWITCH__ )
	pthread_join( rt_pthread, NULL );
	rt_workerIdSet.store( false );
#else
	rt_thread.join();
	rt_threadId = std::thread::id();
#endif
	rt_started.store( false );
	common->Printf( "Render thread stopped\n" );
}

static void R_RenderThreadStats_f( const idCmdArgs &args ) {
	( void )args;
	R_RenderThread_Sync( "stats" );
	common->Printf( "Render thread: enabled=%d running=%d threaded=%d inline=%d waits=%d waitMs=%.2f completed=%d\n",
		r_glRenderThread.GetBool() ? 1 : 0, rt_started.load() ? 1 : 0, rt_framesThreaded, rt_framesInline,
		rt_syncWaits, rt_syncWaitUsec * 0.001f, rt_framesCompleted );
}

void R_RenderThread_Init( void ) {
	cmdSystem->AddCommand( "renderThreadStats", R_RenderThreadStats_f, CMD_FL_RENDERER,
		"prints render-thread frame counts and how often the main thread waited for it" );
}

#endif // OPENQ4_RENDERER_GLES_MODULE
