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

static void R_RenderThread_Main( void ) {
#if defined( __SWITCH__ )
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
	if ( !r_glRenderThread.GetBool() || r_skipBackEnd.GetBool() || !R_RenderThread_FrameQualifies() ) {
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
	rt_syncWaitUsec += static_cast<long long>( R_RendererMetrics_CpuClock() - waitStart );

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
