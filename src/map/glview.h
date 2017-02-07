/******************************************************************************
*  Project: NextGIS GL Viewer
*  Purpose: GUI viewer for spatial data.
*  Author:  Dmitry Baryshnikov, bishop.dev@gmail.com
 * Author: NikitaFeodonit, nfeodonit@yandex.com
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

#ifndef NGSGLVIEW_H
#define NGSGLVIEW_H

#include "api_priv.h"
#include "vector.h"

// CGAL
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Point_2.h>
#include <CGAL/Polygon_2.h>
#include <CGAL/Triangulation_face_base_with_info_2.h>
#include <CGAL/Triangulation_vertex_base_with_info_2.h>

// stl
#include <atomic>
#include <list>
#include <memory>
#include <set>
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

class Style;

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

#define GL_BUFFER_UNKNOWN 0

enum ngsShaderType {
    SH_VERTEX,
    SH_FRAGMENT
};

enum ngsBufferType {
    BF_VERTICES = 0,
    BF_INDICES
};

enum class LineCapType : uint8_t {
    Butt,
    Square,
    Round,
    // the following type is for internal use only
    FakeRound // TODO: remove it and switch to Round
};

enum class LineJoinType : uint8_t {
    Miter,
    Bevel,
    Round,
    // the following two types are for internal use only
    FlipBevel,
    FakeRound
};

class GlBuffer
{
public:
    explicit GlBuffer();
    explicit GlBuffer(const GlBuffer& other) = delete;
    explicit GlBuffer(GlBuffer&& otherr);
    ~GlBuffer();

    GlBuffer& operator=(const GlBuffer& other) = delete;
    GlBuffer& operator=(GlBuffer&& other);

    void bind();
    bool bound() const;

    bool canStoreVertices(size_t amount) const;
    bool canStoreVerticesWithNormals(size_t amount) const;
    bool canStoreIndices(size_t amount) const;

    static bool canGlobalStoreVertices(size_t amount);
    static bool canGlobalStoreVerticesWithNormals(size_t amount);
    static bool canGlobalStoreIndices(size_t amount);

    void addVertex(float x, float y, float z);
    void addVertexWithNormal(float vX, float vY, float vZ, float nX, float nY);
    void addIndex(unsigned short index);
    void addTriangleIndices(
            unsigned short one, unsigned short two, unsigned short three);

    size_t getVertexBufferSize() const;
    size_t getIndexBufferSize() const;

    static std::int_fast32_t getGlobalVertexBufferSize();
    static std::int_fast32_t getGlobalIndexBufferSize();
    static std::int_fast32_t getGlobalHardBuffersCount();

    GLuint getBuffer(enum ngsBufferType type) const;

protected:
    bool m_bound;
    size_t m_finalVertexBufferSize;
    size_t m_finalIndexBufferSize;

    std::vector<GLfloat> m_vertices;
    std::vector<GLushort> m_indices;
    GLuint m_glHardBufferIds[2];

    static std::atomic_int_fast32_t m_globalVertexBufferSize;
    static std::atomic_int_fast32_t m_globalIndexBufferSize;
    static std::atomic_int_fast32_t m_globalHardBuffersCount;
};

using GlBufferSharedPtr = std::shared_ptr<GlBuffer>;

// http://stackoverflow.com/a/13196986
template <typename... Args>
GlBufferSharedPtr makeSharedGlBuffer(Args&&... args)
{
    return std::make_shared<GlBuffer>(std::forward<Args>(args)...);
}

class GlBufferBucket
{
public:
    explicit GlBufferBucket(int x,
            int y,
            unsigned char z,
            const OGREnvelope& env,
            char crossExtent);
    explicit GlBufferBucket(const GlBufferBucket& bucket) = delete;
    explicit GlBufferBucket(GlBufferBucket&& bucket);
    ~GlBufferBucket();

    GlBufferBucket& operator=(const GlBufferBucket& bucket) = delete;
    GlBufferBucket& operator=(GlBufferBucket&& bucket);

    void bind();
    bool filled() const;
    void setFilled(bool filled);
    bool bound() const;
    void fill(GIntBig fid, OGRGeometry* geom, float level);
    void draw(const Style& style);
    int X() const;
    int Y() const;
    unsigned char zoom() const;
    void free();
    bool hasFid(GIntBig fid) const;
    int getFidCount() const;

    OGREnvelope extent() const;
    bool intersects(const GlBufferBucket& other) const;
    bool intersects(const OGREnvelope& ext) const;
    char crossExtent() const;

    size_t getVertexBufferSize() const;
    size_t getIndexBufferSize() const;

protected:
    void fill(const OGRGeometry* geom, float level);
    void fillPoint(const OGRPoint* point, float level);
    void fillLineString(const OGRLineString* line, float level);
    void fillPolygon(const OGRPolygon* polygon, float level);

    void addCurrentLineVertex(const Vector2& currPt,
            float level,
            const Vector2& normal,
            double endLeft,
            double endRight,
            bool round,
            GlBufferSharedPtr currBuffer);

    void addPieSliceLineVertex(const Vector2& currPt,
            float level,
            const Vector2& extrude,
            bool lineTurnsLeft,
            bool fakeRound,
            GlBufferSharedPtr currBuffer);

protected:
    std::vector<GlBufferSharedPtr> m_buffers;
    std::set<GIntBig> m_fids;
    int m_X;
    int m_Y;
    unsigned char m_zoom;
    OGREnvelope m_extent;
    bool m_filled;
    char m_crossExtent;
    int m_e1;
    int m_e2;
    int m_e3;
};

using GlBufferBucketSharedPtr = std::shared_ptr<GlBufferBucket>;

template <typename... Args>
GlBufferBucketSharedPtr makeSharedGlBufferBucket(Args&&... args)
{
    return std::make_shared<GlBufferBucket>(std::forward<Args>(args)...);
}

class GlProgram
{
public:
    GlProgram();
    ~GlProgram();
    bool load(const GLchar * const vertexShader,
              const GLchar * const fragmentShader);

    GLuint id() const;
    void use() const;

protected:
    bool checkLinkStatus(GLuint obj) const;
    bool checkShaderCompileStatus(GLuint obj) const;
    GLuint loadShader(GLenum type, const char *shaderSrc);
protected:
    GLuint m_id;
};

typedef std::unique_ptr<GlProgram> GlProgramUPtr;

class GlFuctions
{
public:
    GlFuctions();
    ~GlFuctions();

    bool init();
    bool isOk() const;
    void setBackgroundColor(const ngsRGBA &color);
    void clearBackground();

    // Draw functions
    void testDraw(int colorId) const;
    void testDrawPreserved(int colorId) const;
    void drawPolygons(const std::vector<GLfloat> &vertices,
                      const std::vector<GLushort> &indices) const;
protected:
    bool loadExtensions();

protected:
    GlColor m_bkColor;
    bool m_extensionLoad, m_pBkChanged;
};

}

// Based on the
// http://doc.cgal.org/latest/Triangulation_2/index.html#title29
// "8.4 Example: Triangulating a Polygonal Domain".
// The following code inserts nested polygons into
// a constrained Delaunay triangulation and counts the number of facets
// that are inside the domain delimited by these polygons.
// Note that the following code does not work
// if the boundaries of the polygons intersect.
class PolygonTriangulator
{
protected:
    struct VertexInfo2
    {
        VertexInfo2()
                : m_index(-1)
        {
        }

        bool in_domain()
        {
            return m_index != -1;
        }

        int m_index;
    };

    struct FaceInfo2
    {
        FaceInfo2()
                : m_nestingLevel(-1)
        {
        }

        bool in_domain()
        {
            return m_nestingLevel % 2 == 1;
        }

        int m_nestingLevel;
    };

    typedef CGAL::Exact_predicates_inexact_constructions_kernel Kern;
    typedef CGAL::Triangulation_vertex_base_with_info_2<VertexInfo2, Kern> Vbi;
    typedef CGAL::Triangulation_vertex_base_2<Kern, Vbi> Vb;
    typedef CGAL::Triangulation_face_base_with_info_2<FaceInfo2, Kern> Fbi;
    typedef CGAL::Constrained_triangulation_face_base_2<Kern, Fbi> Fb;
    typedef CGAL::Triangulation_data_structure_2<Vb, Fb> TDS;
    typedef CGAL::Exact_predicates_tag Itag;
    typedef CGAL::Polygon_2<Kern> Polygon_2;

public:
    typedef CGAL::Constrained_Delaunay_triangulation_2<Kern, TDS, Itag> CDT;

public:
    explicit PolygonTriangulator();
    virtual ~PolygonTriangulator();

    void triangulate(const OGRPolygon* polygon);

    CDT& getCdt()
    {
        return m_cdt;
    }
    size_t getNumTriangles()
    {
        return m_numTriangles;
    }
    size_t getNumVertices()
    {
        return m_numVertices;
    }

protected:
    void markDomains(CDT& ct,
            CDT::Face_handle start,
            int index,
            std::list<CDT::Edge>& border);
    void markDomains(CDT& cdt);

protected:
    CDT m_cdt;  // TODO: place in heap memory
    size_t m_numTriangles;
    size_t m_numVertices;
};

#endif // NGSGLVIEW_H
