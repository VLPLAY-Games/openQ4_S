// Copyright (C) 2004 Id Software, Inc.
//
/*
** QGL.H
*/

#ifndef __QGL_H__
#define __QGL_H__

#if defined(OPENQ4_RENDERER_GLES_MODULE)
// The GLES module resolves GL through the Khronos ES headers plus its own
// optional-entry-point table; GLEW's loader is desktop GL only. See
// GLES/qgl_gles.h for why the above-ES-3.0 entry points must stay NULL.
#include "GLES/qgl_gles.h"
#else
#include "../external/glew/glew.h"
#endif


#endif
