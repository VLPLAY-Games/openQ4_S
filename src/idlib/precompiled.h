/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifndef __PRECOMPILED_H__
#define __PRECOMPILED_H__

#ifdef __cplusplus

typedef unsigned char		byte;		// 8 bits
typedef unsigned short		word;		// 16 bits
typedef unsigned int		dword;		// 32 bits
typedef unsigned int		uint;
// typedef unsigned long		ulong; // DG: long should be avoided.

typedef signed char			int8;
typedef unsigned char		uint8;
typedef short int			int16;
typedef unsigned short int	uint16;
typedef int					int32;
typedef unsigned int		uint32;
typedef long long			int64;
typedef unsigned long long	uint64;

class ThreadedAlloc;		// class that is only used to expand the AutoCrit template to tag allocs/frees called from inside the R_AddModelSurfaces call graph
#define PC_CVAR_ARCHIVE CVAR_ARCHIVE



#ifdef __linux__

// for offsetof
#include <stddef.h>
// FLT_MAX and such
#include <limits.h>
#include <float.h>

	#define __WITH_PB__
	#undef WIN32
	#undef _XBOX
	#undef _CONSOLE
	#define _OPENGL
	#if defined( __SWITCH__ )
		// newlib defines _LITTLE_ENDIAN as 1234 in <machine/endian.h>; an empty
		// redefinition breaks every later system header.
		#include <sys/types.h>
		#undef quad
#else
	#define _LITTLE_ENDIAN
	#endif
	#define _CASE_SENSITIVE_FILESYSTEM
	#define _USE_OPENAL
	#ifndef Q4SDK
		#define Q4SDK_MD5R
	#endif

	#define NEWLINE				"\n"

	#define _GLVAS_SUPPPORT

	#ifndef ID_ALIGNMENTCHECKER_DEFINED
#define ID_ALIGNMENTCHECKER_DEFINED
class AlignmentChecker
{
public:
	static void UpdateCount(void const * const ptr) {}
	static void ClearCount() {}
	static void Print() {}
};
#endif

	#define RESTRICT
	#define TIME_THIS_SCOPE(x)

	// Enables the batching of vertex cache request in SMP mode.
	// Note (TTimo): is tied to ENABLE_INTEL_SMP
	#ifdef ENABLE_INTEL_SMP
	#define ENABLE_INTEL_VERTEXCACHE_OPT
	#endif

#endif


#ifndef RESTRICT
	#define RESTRICT
#endif

#ifndef TIME_THIS_SCOPE
	#define TIME_THIS_SCOPE(x)
#endif

#ifndef BIT
#define BIT( num )				BITT< num >::VALUE
#endif

template< unsigned int B >
class BITT {
public:
	typedef enum bitValue_e {
		VALUE = 1 << B,
	} bitValue_t;
};

#include <stdint.h>

//-----------------------------------------------------

#define ID_TIME_T time_t


//-----------------------------------------------------

#if !defined( _DEBUG ) && !defined( NDEBUG )
	// don't generate asserts
#define NDEBUG
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <ctype.h>
#include <typeinfo>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <errno.h>
#include <math.h>

//-----------------------------------------------------

// non-portable system services
#include "../sys/sys_public.h"

// id lib
#include "../idlib/Lib.h"

// framework
#include "../framework/BuildVersion.h"
#include "../framework/BuildDefines.h"
#include "../framework/licensee.h"
#include "../framework/CmdSystem.h"
#include "../framework/CVarSystem.h"
#include "../framework/Common.h"
#include "../framework/File.h"
#include "../framework/FileSystem.h"
#include "../framework/UsercmdGen.h"

// decls
#include "../framework/declManager.h"
#include "../framework/declTable.h"
#include "../framework/declSkin.h"
#include "../framework/declEntityDef.h"
#include "../framework/declAF.h"
#include "../framework/DeclPDA.h"
// RAVEN BEGIN
// jscott: new decl types
#include "../framework/DeclPlayerModel.h"
#include "../framework/declMatType.h"
#include "../framework/declLipSync.h"
#include "../framework/declPlayback.h"
// RAVEN END

// We have expression parsing and evaluation code in multiple places:
// materials, sound shaders, and guis. We should unify them.
const int MAX_EXPRESSION_OPS = 4096;
const int MAX_EXPRESSION_REGISTERS = 4096;

// Sanity check for any axis in bounds
const float MAX_BOUND_SIZE = 65536.0f;

// renderer
#include "../renderer/qgl.h"
#include "../renderer/Cinematic.h"
#include "../renderer/Material.h"
#include "../renderer/Model.h"
#include "../renderer/ModelManager.h"
#include "../renderer/RenderSystem.h"
#include "../renderer/RenderWorld.h"
#include "../framework/VRSystem.h"

// sound engine
#include "../sound/sound.h"

// RAVEN BEGIN
// jscott: Effects system interface
#include "../bse/BSEInterface.h"
// RAVEN END

// asynchronous networking
#include "../framework/async/NetworkSystem.h"

// user interfaces
#include "../ui/ListGUI.h"
#include "../ui/UserInterface.h"

// collision detection system
#include "../cm/CollisionModel.h"

// AAS files and manager
#include "../aas/AASFile.h"
#include "../aas/AASFileManager.h"

// game
#include "../game/Game.h"

//-----------------------------------------------------

#ifdef GAME_DLL
#ifdef GAME_MPAPI
#include "../mpgame/Game_local.h"
#else
#include "../game/Game_local.h"
#endif
#endif

#ifndef Q4SDK

#include "../framework/DemoChecksum.h"

// framework
#include "../framework/Compressor.h"
#include "../framework/EventLoop.h"
#include "../framework/KeyInput.h"
#include "../framework/EditField.h"
#include "../framework/Console.h"
#include "../framework/DemoFile.h"
#include "../framework/Session.h"

// asynchronous networking
#include "../framework/async/AsyncNetwork.h"

// The editor entry points are always declared, but may just be
// stubbed out on non-windows platforms.
#include "../tools/edit_public.h"

// Compilers for map, model, video etc. processing.
#include "../tools/compilers/compiler_public.h"

#endif /* !Q4SDK */

//-----------------------------------------------------

// RAVEN BEGIN
// jsinger: add AutoPtr and text-to-binary compiler support
#include "AutoPtr.h"
#include "LexerFactory.h"
#include "TextCompiler.h"
// jsinger: AutoCrit.h contains classes which aid in code synchronization
//          AutoAcquire.h contains a class that aids in thread acquisition of the direct3D device for xenon
//          Both compile out completely if the #define's above are not present
#include "threads/AutoCrit.h"
// RAVEN END

#endif	/* __cplusplus */

#endif /* !__PRECOMPILED_H__ */

