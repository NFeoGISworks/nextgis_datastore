/******************************************************************************
 * Project:  libngstore
 * Purpose:  NextGIS store and visualisation support library
 * Author: Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
 ******************************************************************************
 *   Copyright (c) 2016-2017 NextGIS, <info@nextgis.com>
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
#ifndef NGSMAPTRANSFORM_H
#define NGSMAPTRANSFORM_H

#include "ogr_core.h"

#include "matrix.h"
#include "ds/geometry.h"
#include "ngstore/api.h"

namespace ngs {

class MapTransform
{
public:
    explicit MapTransform(int width, int height);
    virtual ~MapTransform() = default;

    int getDisplayWidht() const { return m_displayHeight; }
    int getDisplayHeight() const { return m_displayWidht; }
    double getRotate(enum ngsDirection dir) const { return m_rotate[dir]; }
    bool setRotate(enum ngsDirection dir, double rotate);

    Envelope getExtent() const { return m_rotateExtent; }
    OGRRawPoint getCenter() const { return m_center; }
    OGRRawPoint worldToDisplay(const OGRRawPoint &pt) const {
        OGRRawPoint newPt(m_worldToDisplayMatrix.project(pt));
        if(m_YAxisInverted) {
            newPt.y = m_displayHeight - newPt.y;
        }
        return newPt;
    }
    OGRRawPoint displayToWorld(const OGRRawPoint &pt) const {
        OGRRawPoint newPt(pt);
        if(m_YAxisInverted) {
            newPt.y = m_displayHeight - newPt.y;
        }
        return m_invWorldToDisplayMatrix.project(newPt);
    }
    void setDisplaySize(int width, int height, bool isYAxisInverted);
    bool setScale(double scale);
    bool setCenter(double x, double y);
    bool setScaleAndCenter(double scale, double x, double y);
    bool setExtent(const Envelope &env);
    unsigned char getZoom() const;
    double getScale() const { return m_scale; }
    Matrix4 getSceneMatrix() const { return m_sceneMatrix; }
    Matrix4 getInvViewMatrix() const { return m_invViewMatrix; }

    bool getXAxisLooped() const { return m_XAxisLooped; }
    bool getYAxisInverted() const { return m_YAxisInverted; }
    std::vector<TileItem> getTilesForExtent() const {
        return getTilesForExtent(getExtent(), getZoom(), getYAxisInverted(),
                                 getXAxisLooped());
    }
    void setExtentLimits(const Envelope& extentLimit);
    void setZoomIncrement(char increment) { m_extraZoom = increment; }

    // static
public:
    static std::vector<TileItem> getTilesForExtent(const Envelope &extent,
                                                   unsigned char zoom,
                                                   bool reverseY,
                                                   bool unlimitX);

protected:
    bool updateExtent();
    void initMatrices();
    void setRotateExtent();
    double fixScale(double scale);
    OGRRawPoint fixCenter(double x, double y);
    void fixExtent();

protected:
    inline static double lg(double x) { return log(x) / M_LN2; }

protected:
    int m_displayWidht, m_displayHeight;
    OGRRawPoint m_center;
    double m_rotate[3];
    double m_scale, m_scaleWorld; //m_scaleScene, m_scaleView,
    Envelope m_extent, m_rotateExtent;
    double m_ratio;
    bool m_YAxisInverted, m_XAxisLooped;
    /* NOTE: sceneMatrix transform from world coordinates to GL coordinates -1 x 1
     * viewMatrix transform from GL coordinates to display coordinates 640 x 480
     */
    Matrix4 m_sceneMatrix, m_viewMatrix, m_worldToDisplayMatrix;
    Matrix4 m_invSceneMatrix, m_invViewMatrix, m_invWorldToDisplayMatrix;

    // Limits
    char m_extraZoom;
    double m_scaleMax, m_scaleMin;
    Envelope m_extentLimit;
};

}

#endif // NGSMAPTRANSFORM_H
