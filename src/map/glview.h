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

#include <vector>

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

#if defined(_DEBUG)
#define ngsCheckGLEerror(cmd) {cmd; checkGLError(#cmd);}
#define ngsCheckEGLEerror(cmd) {cmd; checkEGLError(#cmd);}
#else
#define ngsCheckGLEerror(cmd) (cmd)
#define ngsCheckEGLEerror(cmd) (cmd)
#endif

#ifdef OFFSCREEN_GL // need for headless desktop drawing (i.e. some preview gerneartion)
#include "EGL/egl.h"
#endif // OFFSCREEN_GL

namespace ngs {

typedef struct _glrgb {
    float r;
    float g;
    float b;
    float a;
} GlColor;

bool checkGLError(const char *cmd);
void reportGlStatus(GLuint obj);


/*typedef int (*ngsBindVertexArray)(GLuint array);
typedef int (*ngsDeleteVertexArrays)(GLsizei n, const GLuint* arrays);
typedef int (*ngsGenVertexArrays)(GLsizei n, GLuint* arrays);*/

#ifdef OFFSCREEN_GL

bool checkEGLError(const char *cmd);
using namespace std;
class GlDisplay
{
public:
    GlDisplay();
    ~GlDisplay();
    bool init();
    EGLDisplay eglDisplay() const;

    EGLConfig eglConf() const;

protected:

    EGLDisplay m_eglDisplay;
    EGLConfig m_eglConf;
};

typedef shared_ptr<GlDisplay> GlDisplayPtr;
GlDisplayPtr getGlDisplay();

class GlView
{
public:
    GlView();
    virtual ~GlView();
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
    virtual void loadProgram();
    virtual GLuint prepareProgram();
    bool checkProgramLinkStatus(GLuint obj) const;
    bool checkShaderCompileStatus(GLuint obj) const;
    GLuint loadShader(GLenum type, const char *shaderSrc);
    virtual void loadExtensions();
    virtual bool createSurface() = 0;

protected:
    GlDisplayPtr m_glDisplay;
    EGLContext m_eglCtx;
    EGLSurface m_eglSurface;

    GlColor m_bkColor;
    int m_displayWidth, m_displayHeight;

    bool m_extensionLoad, m_programLoad;

/*    ngsBindVertexArray bindVertexArrayFn;
    ngsDeleteVertexArrays deleteVertexArraysFn;
    ngsGenVertexArrays genVertexArraysFn;*/

// shaders
protected:
    GLuint m_programId;


};

class GlOffScreenView : public GlView
{
public:
    GlOffScreenView();
    virtual ~GlOffScreenView();

protected:
    virtual bool createSurface() override;
    bool createFBO(int width, int height);
    void destroyFBO();
//FBO
protected:
    GLuint m_defaultFramebuffer;
    GLuint m_renderbuffers[2];
};

#endif // OFFSCREEN_GL

class GlFuctions
{
public:
    GlFuctions();
    bool init();
    bool isOk() const;
    void setBackgroundColor(const ngsRGBA &color);
    void clearBackground();
    void prepare(const Matrix4& mat);

    // Draw functions
    void testDraw() const;
    void testDrawPreserved() const;
    void drawPolygons(const vector<GLfloat> &vertices,
                      const vector<GLushort> &indices) const;
protected:
    virtual bool loadProgram();
    virtual GLuint prepareProgram();
    bool checkProgramLinkStatus(GLuint obj) const;
    bool checkShaderCompileStatus(GLuint obj) const;
    GLuint loadShader(GLenum type, const char *shaderSrc);
    virtual bool loadExtensions();

// shaders
protected:
    GLuint m_programId;
    GlColor m_bkColor;
    bool m_extensionLoad, m_programLoad, m_pBkChanged;
};

}


#endif // GLVIEW_H
