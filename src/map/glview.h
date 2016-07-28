/******************************************************************************
*  Project: NextGIS GL Viewer
*  Purpose: GUI viewer for spatial data.
*  Author:  Dmitry Baryshnikov, bishop.dev@gmail.com
*******************************************************************************
*  Copyright (C) 2016 NextGIS, <info@nextgis.com>
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 2 of the License, or
*   (at your option) any later version.
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#ifndef GLVIEW_H
#define GLVIEW_H

#include "api_priv.h"
#include "matrix.h"

#if __APPLE__
    #include "TargetConditionals.h"
    #if TARGET_OS_IPHONE
        #include <OpenGLES/ES2/gl.h>
        #include <OpenGLES/ES2/glext.h>
    #elif TARGET_IPHONE_SIMULATOR
        #include <OpenGLES/ES2/gl.h>
        #include <OpenGLES/ES2/glext.h>
    #elif TARGET_OS_MAC
        #include <OpenGL/OpenGL.h>
        #include <OpenGL/gl.h>
    #else
        #error Unsupported Apple platform
    #endif
#elif __ANDROID__
    #define GL_GLEXT_PROTOTYPES
    #include <GLES2/gl2.h>
    #include <GLES2/gl2ext.h>
#else
    #define GL_GLEXT_PROTOTYPES
    #include <GLES2/gl2.h>
    #include <GLES2/gl2ext.h>
//    #include <GL/gl.h>
//    #include <GL/glext.h>
#endif

#include "EGL/egl.h"

#include <vector>

namespace ngs {

using namespace std;

typedef struct _glrgb {
    float r;
    float g;
    float b;
    float a;
} GlColor;

/*typedef int (*ngsBindVertexArray)(GLuint array);
typedef int (*ngsDeleteVertexArrays)(GLsizei n, const GLuint* arrays);
typedef int (*ngsGenVertexArrays)(GLsizei n, GLuint* arrays);*/

#if defined(_DEBUG)
#define ngsCheckGLEerror(cmd) {cmd; checkGLError(#cmd);}
#define ngsCheckEGLEerror(cmd) {cmd; checkEGLError(#cmd);}
#else
#define ngsCheckGLEerror(cmd) (cmd)
#define ngsCheckEGLEerror(cmd) (cmd)
#endif

class GlView
{
public:
    GlView();
    ~GlView();
    bool init();
    void setSize(int width, int height);
    bool isOk() const;
    void setBackgroundColor(const ngsRGBA &color);
    void fillBuffer(void* buffer) const;
    void clearBackground();
    void prepare(const Matrix4& mat);
    // Draw functions
    void draw() const;
    void drawPolygons(const vector<GLfloat> &vertices,
                      const vector<GLushort> &indices) const;
protected:
    GLuint prepareProgram();
    bool checkProgramLinkStatus(GLuint obj) const;
    bool checkShaderCompileStatus(GLuint obj) const;
    void reportGlStatus(GLuint obj) const;
    bool checkGLError(const char *cmd) const;
    GLuint loadShader(GLenum type, const char *shaderSrc);
    bool checkEGLError(const char *cmd) const;
    bool createFBO(int width, int height);
    void destroyFBO();

protected:

    EGLDisplay m_eglDisplay;
    EGLContext m_eglCtx;
    EGLSurface m_eglSurface;
    EGLConfig m_eglConf;

    GlColor m_bkColor;
    int m_displayWidth, m_displayHeight;

    bool m_extensionLoad, m_programLoad;

/*    ngsBindVertexArray bindVertexArrayFn;
    ngsDeleteVertexArrays deleteVertexArraysFn;
    ngsGenVertexArrays genVertexArraysFn;*/

// shaders
protected:
    GLuint m_programId;

//FBO
protected:
    GLuint m_defaultFramebuffer;
    GLuint m_renderbuffers[2];
};

}

#endif // GLVIEW_H
