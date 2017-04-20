/******************************************************************************
 * Project:  libngstore
 * Purpose:  NextGIS store and visualisation support library
 * Author: Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
 * Author: NikitaFeodonit, nfeodonit@yandex.com
 ******************************************************************************
 *   Copyright (c) 2016 NextGIS, <info@nextgis.com>
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

#include "style.h"

#include <iostream>

namespace ngs {

//------------------------------------------------------------------------------
// Style
//------------------------------------------------------------------------------

Style::Style()
        : m_vertexShaderSource(nullptr),
          m_fragmentShaderSource(nullptr)
{
}

const GLchar* Style::getShaderSource(ShaderType type)
{
    switch (type) {
        case SH_VERTEX:
            return m_vertexShaderSource;
        case SH_FRAGMENT:
            return m_fragmentShaderSource;
    }
    return nullptr;
}

bool Style::prepare(const Matrix4& msMatrix, const Matrix4& vsMatrix)
{
    if (!m_program.isLoad()) {
        bool result = m_program.load(getShaderSource(Style::SH_VERTEX),
                                     getShaderSource(Style::SH_FRAGMENT));
        if(!result) {
            return false;
        }
    }

    m_program.use();

    m_program.setMatrix("u_msMatrix", msMatrix.dataF());
    m_program.setMatrix("u_vsMatrix", vsMatrix.dataF());

    return true;
}

void Style::draw(const GlBuffer& buffer) const
{
    if (!buffer.bound())
        return;

    ngsCheckGLError(glBindBuffer(
            GL_ARRAY_BUFFER, buffer.getGlBufferId(GlBuffer::BF_VERTICES)));
    ngsCheckGLError(glBindBuffer(
            GL_ELEMENT_ARRAY_BUFFER, buffer.getGlBufferId(GlBuffer::BF_INDICES)));
}


//------------------------------------------------------------------------------
// SimpleFillBorderStyle
//------------------------------------------------------------------------------

SimpleVectorStyle::SimpleVectorStyle() :
    Style(),
    m_color({1.0, 1.0, 1.0, 1.0})
{

}

void SimpleVectorStyle::setColor(const ngsRGBA &color)
{
    m_color.r = float(color.R) / 255;
    m_color.g = float(color.G) / 255;
    m_color.b = float(color.B) / 255;
    m_color.a = float(color.A) / 255;
}

bool SimpleVectorStyle::prepare(const Matrix4 &msMatrix, const Matrix4 &vsMatrix)
{
    if(!Style::prepare(msMatrix, vsMatrix))
        return false;
    m_program.setColor("u_color", m_color);

    return true;
}

//------------------------------------------------------------------------------
// SimplePointStyle
//------------------------------------------------------------------------------

constexpr const GLchar* const pointVertexShaderSource = R"(
    attribute vec3 a_mPosition;

    uniform mat4 u_msMatrix;
    uniform float u_vSize;

    void main()
    {
        gl_Position = u_msMatrix * vec4(a_mPosition, 1);
        gl_PointSize = u_vSize;
    }
)";

// Circle: http://stackoverflow.com/a/17275113
// Sphere symbol (http://stackoverflow.com/a/25783231)
// https://www.raywenderlich.com/37600/opengl-es-particle-system-tutorial-part-1
// http://stackoverflow.com/a/10506172
// https://www.cs.uaf.edu/2009/spring/cs480/lecture/02_03_pretty.html
// http://stackoverflow.com/q/18659332
constexpr const GLchar* const pointFragmentShaderSource = R"(
    uniform vec4 u_color;
    uniform int u_type;

    bool isInTriangle(vec2 point, vec2 p1, vec2 p2, vec2 p3)
    {
      float a = (p1.x - point.x) * (p2.y - p1.y)
              - (p2.x - p1.x) * (p1.y - point.y);
      float b = (p2.x - point.x) * (p3.y - p2.y)
              - (p3.x - p2.x) * (p2.y - point.y);
      float c = (p3.x - point.x) * (p1.y - p3.y)
              - (p1.x - p3.x) * (p3.y - point.y);

      if ((a >= 0.0 && b >= 0.0 && c >= 0.0)
            || (a <= 0.0 && b <= 0.0 && c <= 0.0))
        return true;
      else
        return false;
    }

    void drawSquare()
    {
        gl_FragColor = u_color;
    }

    void drawRectangle()
    {
        if(0.4 < gl_PointCoord.x && gl_PointCoord.x > 0.6)
            discard;
        else
            gl_FragColor = u_color;
    }

    void drawCircle()
    {
        vec2 coord = gl_PointCoord - vec2(0.5);
        if(length(coord) > 0.5)
           discard;
        else
           gl_FragColor = u_color;
    }

    void drawTriangle()
    {
        if(!isInTriangle(vec2(gl_PointCoord),
                vec2(0.0, 0.933), vec2(1.0, 0.933), vec2(0.5, 0.066)))
           discard;
        else
           gl_FragColor = u_color;
    }

    void drawDiamond()
    {
        if(!(isInTriangle(vec2(gl_PointCoord),
                vec2(0.2, 0.5), vec2(0.8, 0.5), vec2(0.5, 0.0))
            || isInTriangle(vec2(gl_PointCoord),
                vec2(0.2, 0.5), vec2(0.8, 0.5), vec2(0.5, 1.0))))
           discard;
        else
           gl_FragColor = u_color;
    }

    void drawStar()
    {
        float d1 = 0.4;
        float d2 = 0.6;

        bool a1 = isInTriangle(vec2(gl_PointCoord),
                vec2(d1, d1), vec2(d2, d1), vec2(0.5, 0.0));
        bool a2 = isInTriangle(vec2(gl_PointCoord),
                vec2(d2, d1), vec2(d2, d2), vec2(1.0, 0.5));
        bool a3 = isInTriangle(vec2(gl_PointCoord),
                vec2(d1, d2), vec2(d2, d2), vec2(0.5, 1.0));
        bool a4 = isInTriangle(vec2(gl_PointCoord),
                vec2(d1, d1), vec2(d1, d2), vec2(0.0, 0.5));
        bool a5 = isInTriangle(vec2(gl_PointCoord),
                vec2(d1, d1), vec2(d2, d2), vec2(d2, d1));
        bool a6 = isInTriangle(vec2(gl_PointCoord),
                vec2(d1, d1), vec2(d2, d2), vec2(d1, d2));

        if(!(a1 || a2 || a3 || a4 || a5 || a6))
           discard;
        else
           gl_FragColor = u_color;
    }

    void main()
    {
        if(1 == u_type)      // Square
            drawSquare();
        else if(2 == u_type) // Rectangle
            drawRectangle();
        else if(3 == u_type) // Circle
            drawCircle();
        else if(4 == u_type) // Triangle
            drawTriangle();
        else if(5 == u_type) // Diamond
            drawDiamond();
        else if(6 == u_type) // Star
            drawStar();
    }
)";

SimplePointStyle::SimplePointStyle(enum PointType type)
        : SimpleVectorStyle()
        , m_type(type)
        , m_size(6.0)
{
    m_vertexShaderSource = pointVertexShaderSource;
    m_fragmentShaderSource = pointFragmentShaderSource;
}

bool SimplePointStyle::prepare(const Matrix4& msMatrix, const Matrix4& vsMatrix)
{
    if (!SimpleVectorStyle::prepare(msMatrix, vsMatrix))
        return false;

    m_program.setInt("u_type", m_type);
    m_program.setFloat("u_vSize", m_size);
    m_program.setVertexAttribPointer("a_mPosition", 3, 0, 0);

    return true;
}

void SimplePointStyle::draw(const GlBuffer& buffer) const
{
    SimpleVectorStyle::draw(buffer);

    ngsCheckGLError(glDrawElements(GL_POINTS, buffer.getIndexSize(),
            GL_UNSIGNED_SHORT, nullptr));
}

//------------------------------------------------------------------------------
// SimpleLineStyle
//------------------------------------------------------------------------------
constexpr const GLchar* const lineVertexShaderSource = R"(
    attribute vec3 a_mPosition;
    attribute vec2 a_normal;

    uniform float u_vLineWidth;
    uniform mat4 u_msMatrix;
    uniform mat4 u_vsMatrix;

    void main()
    {
        vec4 vDelta = vec4(a_normal * u_vLineWidth, 0, 0);
        vec4 sDelta = u_vsMatrix * vDelta;
        vec4 sPosition = u_msMatrix * vec4(a_mPosition, 1);
        gl_Position = sPosition + sDelta;
    }
)";

constexpr const GLchar* const lineFragmentShaderSource = R"(
    uniform vec4 u_color;

    void main()
    {
      gl_FragColor = u_color;
    }
)";


SimpleLineStyle::SimpleLineStyle()
        : SimpleVectorStyle(),
          m_lineWidth(1.0)
{
    m_vertexShaderSource = lineVertexShaderSource;
    m_fragmentShaderSource = lineFragmentShaderSource;
}

bool SimpleLineStyle::prepare(const Matrix4& msMatrix, const Matrix4& vsMatrix)
{
    if (!Style::prepare(msMatrix, vsMatrix))
        return false;

    m_program.setFloat("u_vLineWidth", m_lineWidth);
    m_program.setVertexAttribPointer("a_mPosition", 3, 5 * sizeof(float), 0);
    m_program.setVertexAttribPointer("a_normal", 2, 5 * sizeof(float),
                            reinterpret_cast<const GLvoid*>(3 * sizeof(float)));
    return true;
}

void SimpleLineStyle::draw(const GlBuffer& buffer) const
{
    SimpleVectorStyle::draw(buffer);

    ngsCheckGLError(glDrawElements(GL_TRIANGLES, buffer.getIndexSize(),
            GL_UNSIGNED_SHORT, nullptr));
}

//------------------------------------------------------------------------------
// SimpleFillStyle
//------------------------------------------------------------------------------
constexpr const GLchar* const fillVertexShaderSource = R"(
    attribute vec3 a_mPosition;

    uniform mat4 u_msMatrix;

    void main()
    {
        gl_Position = u_msMatrix * vec4(a_mPosition, 1);
    }
)";

constexpr const GLchar* const fillFragmentShaderSource = R"(
    uniform vec4 u_color;

    void main()
    {
      gl_FragColor = u_color;
    }
)";

SimpleFillStyle::SimpleFillStyle()
        : SimpleVectorStyle()
{
    m_vertexShaderSource = fillVertexShaderSource;
    m_fragmentShaderSource = fillFragmentShaderSource;
}

bool SimpleFillStyle::prepare(const Matrix4 &msMatrix, const Matrix4 &vsMatrix)
{
    SimpleVectorStyle::prepare(msMatrix, vsMatrix);
    m_program.setVertexAttribPointer("a_mPosition", 3, 0, 0);

    return true;
}

void SimpleFillStyle::draw(const GlBuffer& buffer) const
{
    SimpleVectorStyle::draw(buffer);
    ngsCheckGLError(glDrawElements(GL_TRIANGLES, buffer.getIndexSize(),
            GL_UNSIGNED_SHORT, nullptr));
}

//------------------------------------------------------------------------------
// SimpleFillBorderStyle
//------------------------------------------------------------------------------
constexpr const GLchar* const fillBorderVertexShaderSource = R"(
    attribute vec3 a_mPosition;
    attribute vec2 a_normal;

    uniform bool u_isBorder;
    uniform float u_vBorderWidth;
    uniform mat4 u_msMatrix;
    uniform mat4 u_vsMatrix;

    void main()
    {
        if (u_isBorder) {
            vec4 vDelta = vec4(a_normal * u_vBorderWidth, 0, 0);
            vec4 sDelta = u_vsMatrix * vDelta;
            vec4 sPosition = u_msMatrix * vec4(a_mPosition, 1);
            gl_Position = sPosition + sDelta;
        } else {
            gl_Position = u_msMatrix * vec4(a_mPosition, 1);
        }
    }
)";

constexpr const GLchar* const fillBorderFragmentShaderSource = R"(
    uniform bool u_isBorder;
    uniform vec4 u_color;
    uniform vec4 u_borderColor;

    void main()
    {
        if (u_isBorder) {
            gl_FragColor = u_borderColor;
        } else {
            gl_FragColor = u_color;
        }
    }
)";

SimpleFillBorderedStyle::SimpleFillBorderedStyle()
        : SimpleVectorStyle(),
          m_borderWidth(1.0)
{
    m_vertexShaderSource = fillBorderVertexShaderSource;
    m_fragmentShaderSource = fillBorderFragmentShaderSource;
}

void SimpleFillBorderedStyle::setBorderColor(const ngsRGBA& color)
{
    m_borderColor.r = float(color.R) / 255;
    m_borderColor.g = float(color.G) / 255;
    m_borderColor.b = float(color.B) / 255;
    m_borderColor.a = float(color.A) / 255;
}

bool SimpleFillBorderedStyle::prepare(const Matrix4& msMatrix,
                                      const Matrix4& vsMatrix)
{
    if (!Style::prepare(msMatrix, vsMatrix))
        return false;

    m_program.setFloat("u_vBorderWidth", m_borderWidth);
    m_program.setColor("u_borderColor", m_borderColor);
    m_program.setVertexAttribPointer("a_mPosition", 3, 5 * sizeof(float), 0);
    m_program.setVertexAttribPointer("a_normal", 2, 5 * sizeof(float),
                            reinterpret_cast<const GLvoid*>(3 * sizeof(float)));

    m_program.setInt("u_isBorder", false);

    return true;
}

void SimpleFillBorderedStyle::draw(const GlBuffer& buffer) const
{
    SimpleVectorStyle::draw(buffer);

    ngsCheckGLError(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                                 buffer.getGlBufferId(GlBuffer::BF_INDICES)));
    ngsCheckGLError(glDrawElements(GL_TRIANGLES, buffer.getIndexSize(),
                                   GL_UNSIGNED_SHORT, 0));

    ngsCheckGLError(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                                 buffer.getGlBufferId(GlBuffer::BF_BORDER_INDICES)));
    m_program.setInt("u_isBorder", true);

    ngsCheckGLError(glDrawElements(GL_TRIANGLES,
                                   buffer.getIndexSize(GlBuffer::BF_BORDER_INDICES),
                                   GL_UNSIGNED_SHORT, 0));
}


} // namespace ngs
