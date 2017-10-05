/******************************************************************************
 * Project:  libngstore
 * Purpose:  NextGIS store and visualization support library
 * Author: Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
 * Author: NikitaFeodonit, nfeodonit@yandex.com
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

#include "overlay.h"

// gdal
#include "ogr_core.h"

#include "ds/earcut.hpp"
#include "map/gl/view.h"
#include "map/mapview.h"


namespace ngs {

//------------------------------------------------------------------------------
// GlRenderOverlay
//------------------------------------------------------------------------------

GlRenderOverlay::GlRenderOverlay()
{
}

//------------------------------------------------------------------------------
// GlEditLayerOverlay
//------------------------------------------------------------------------------

GlEditLayerOverlay::GlEditLayerOverlay(MapView* map) : EditLayerOverlay(map),
    GlRenderOverlay()
{
    GlView* mapView = dynamic_cast<GlView*>(m_map);
    const TextureAtlas* atlas = mapView ? mapView->textureAtlas() : nullptr;

    PointStyle* pointStyle = static_cast<PointStyle*>(
                Style::createStyle("simpleEditPoint", atlas));
    if(pointStyle)
        m_pointStyle = PointStylePtr(pointStyle);

    EditLineStyle* lineStyle = static_cast<EditLineStyle*>(
                Style::createStyle("editLine", atlas));
    if(lineStyle)
        m_lineStyle = EditLineStylePtr(lineStyle);

    EditFillStyle* fillStyle = static_cast<EditFillStyle*>(
                Style::createStyle("editFill", atlas));
    if(fillStyle)
        m_fillStyle = EditFillStylePtr(fillStyle);

    PointStyle* crossStyle = static_cast<PointStyle*>(
                Style::createStyle("simpleEditCross", atlas));
    if(crossStyle)
        m_crossStyle = PointStylePtr(crossStyle);
}

bool GlEditLayerOverlay::setStyleName(enum ngsEditStyleType type,
                                      const char* name)
{
    GlView* mapView = dynamic_cast<GlView*>(m_map);
    Style* style = Style::createStyle(name, mapView ? mapView->textureAtlas() :
                                                      nullptr);
    if(!style) {
        return false;
    }

    if(EST_POINT == type) {
        PointStyle* pointStyle = dynamic_cast<PointStyle*>(style);
        if(pointStyle) {
            freeGlStyle(m_pointStyle);
            m_pointStyle = PointStylePtr(pointStyle);
            return true;
        }
    }

    if(EST_LINE == type) {
        EditLineStyle* lineStyle = dynamic_cast<EditLineStyle*>(style);
        if(lineStyle) {
            freeGlStyle(m_lineStyle);
            m_lineStyle = EditLineStylePtr(lineStyle);
            return true;
        }
    }

    if(EST_FILL == type) {
        EditFillStyle* fillStyle = dynamic_cast<EditFillStyle*>(style);
        if(fillStyle) {
            freeGlStyle(m_fillStyle);
            m_fillStyle = EditFillStylePtr(fillStyle);
            return true;
        }
    }

    if(EST_CROSS == type) {
        PointStyle* crossStyle = dynamic_cast<PointStyle*>(style);
        if(crossStyle) {
            freeGlStyle(m_crossStyle);
            m_crossStyle = PointStylePtr(crossStyle);
            return true;
        }
    }

    delete style; // Delete unused style.
    return false;
}

bool GlEditLayerOverlay::setStyle(enum ngsEditStyleType type,
                                  const CPLJSONObject& jsonStyle)
{
    if(EST_POINT == type) {
        return m_pointStyle->load(jsonStyle);
    }
    if(EST_LINE == type) {
        return m_lineStyle->load(jsonStyle);
    }
    if(EST_FILL == type) {
        return m_fillStyle->load(jsonStyle);
    }
    if(EST_CROSS == type) {
        return m_crossStyle->load(jsonStyle);
    }
    return false;
}

CPLJSONObject GlEditLayerOverlay::style(enum ngsEditStyleType type) const
{
    if(EST_POINT == type) {
        return m_pointStyle->save();
    }
    if(EST_LINE == type) {
        return m_lineStyle->save();
    }
    if(EST_FILL == type) {
        return m_fillStyle->save();
    }
    if(EST_CROSS == type) {
        return m_crossStyle->save();
    }
    return CPLJSONObject();
}

void GlEditLayerOverlay::setVisible(bool visible)
{
    EditLayerOverlay::setVisible(visible);
    if(!visible) {
        freeGlBuffers();
    }
}

bool GlEditLayerOverlay::undo()
{
    bool ret = EditLayerOverlay::undo();
    if(ret) {
        fill();
    }
    return ret;
}

bool GlEditLayerOverlay::redo()
{
    bool ret = EditLayerOverlay::redo();
    if(ret) {
        fill();
    }
    return ret;
}

bool GlEditLayerOverlay::addPoint(OGRPoint* coordinates)
{
    bool ret = EditLayerOverlay::addPoint(coordinates);
    if(ret) {
        fill();
    }
    return ret;
}

enum ngsEditDeleteType GlEditLayerOverlay::deletePoint()
{
    enum ngsEditDeleteType ret = EditLayerOverlay::deletePoint();
	fill();
    return ret;
}

bool GlEditLayerOverlay::addHole()
{
    bool ret = EditLayerOverlay::addHole();
    if(ret) {
        fill();
    }
    return ret;
}

enum ngsEditDeleteType GlEditLayerOverlay::deleteHole()
{
    enum ngsEditDeleteType ret = EditLayerOverlay::deleteHole();
    fill();
    return ret;
}

bool GlEditLayerOverlay::addGeometryPart()
{
    bool ret = EditLayerOverlay::addGeometryPart();
    if(ret) {
        fill();
    }
    return ret;
}

enum ngsEditDeleteType GlEditLayerOverlay::deleteGeometryPart()
{
    enum ngsEditDeleteType ret = EditLayerOverlay::deleteGeometryPart();
    fill();
    return ret;
}

void GlEditLayerOverlay::setGeometry(GeometryUPtr geometry)
{
    EditLayerOverlay::setGeometry(std::move(geometry));
    freeGlBuffers();

    if(!m_geometry) {
        return;
    }
    fill();
}

bool GlEditLayerOverlay::singleTap(const OGRRawPoint& mapCoordinates)
{
    bool ret = EditLayerOverlay::singleTap(mapCoordinates);
    if(ret) {
        fill();
    }
    return ret;
}

bool GlEditLayerOverlay::shiftPoint(const OGRRawPoint& mapOffset)
{
    bool ret = EditLayerOverlay::shiftPoint(mapOffset);
    if(ret) {
        fill();
    }
    return ret;
}

bool GlEditLayerOverlay::fill()
{
    if(!m_geometry) {
        return false;
    }

    freeGlBuffers();

    switch(OGR_GT_Flatten(m_geometry->getGeometryType())) {
        case wkbPoint:
        case wkbMultiPoint: {
            fillPoints();
            break;
        }
        case wkbLineString:
        case wkbMultiLineString: {
            fillLines();
            break;
        }
        case wkbPolygon:
        case wkbMultiPolygon: {
            fillPolygons();
            break;
        }
        default:
            break; // Not supported yet
    }
    return true;
}

void GlEditLayerOverlay::fillPoints()
{
    if(!m_geometry)
        return;

    switch(OGR_GT_Flatten(m_geometry->getGeometryType())) {
        case wkbPoint: {
            const OGRPoint* pt = static_cast<const OGRPoint*>(m_geometry.get());

            auto getPoint = [pt](int /*index*/) -> SimplePoint {
                SimplePoint spt;
                spt.x = static_cast<float>(pt->getX());
                spt.y = static_cast<float>(pt->getY());
                return spt;
            };

            auto isSelectedPoint = [this](int /*index*/) -> bool {
                return (m_selectedPointId.pointId() == 0);
            };

            fillPointElements(1, getPoint, isSelectedPoint);
            break;
        }
        case wkbMultiPoint: {
            const OGRMultiPoint* mpt =static_cast<const OGRMultiPoint*>(
                        m_geometry.get());

            auto getPoint = [mpt](int index) -> SimplePoint {
                const OGRPoint* pt = static_cast<const OGRPoint*>(
                        mpt->getGeometryRef(index));
                SimplePoint spt;
                spt.x = static_cast<float>(pt->getX());
                spt.y = static_cast<float>(pt->getY());
                return spt;
            };

            auto isSelectedPoint = [this](int index) -> bool {
                return (m_selectedPointId.geometryId() == index &&
                        m_selectedPointId.pointId() == 0);
            };

            fillPointElements(mpt->getNumGeometries(), getPoint, isSelectedPoint);
            break;
        }
    }
}

void GlEditLayerOverlay::fillLines()
{
    if(!m_geometry) {
        return;
    }

    switch(OGR_GT_Flatten(m_geometry->getGeometryType())) {
        case wkbLineString: {
            const OGRLineString* line = static_cast<const OGRLineString*>(
                        m_geometry.get());

            auto getLineFunc = [line](int /*index*/) -> const OGRLineString* {
                return line;
            };

            auto isSelectedLineFunc = [this](int /*index*/) -> bool {
                return (m_selectedPointId.pointId() >= 0);
            };

            fillLineElements(1, getLineFunc, isSelectedLineFunc);
            break;
        }

        case wkbMultiLineString: {
            const OGRMultiLineString* mline =
                    static_cast<const OGRMultiLineString*>(m_geometry.get());

            auto getLineFunc = [mline](int index) -> const OGRLineString* {
                return static_cast<const OGRLineString*>(
                        mline->getGeometryRef(index));
            };

            auto isSelectedLineFunc = [this](int index) -> bool {
                return (m_selectedPointId.geometryId() == index &&
                        m_selectedPointId.pointId() >= 0);
            };

            fillLineElements(mline->getNumGeometries(), getLineFunc,
                             isSelectedLineFunc);
            break;
        }
    }
}

void GlEditLayerOverlay::fillPolygons()
{
    if(!m_geometry) {
        return;
    }

    switch(OGR_GT_Flatten(m_geometry->getGeometryType())) {
        case wkbPolygon: {
            const OGRPolygon* polygon = static_cast<const OGRPolygon*>(
                        m_geometry.get());

            auto getPolygonFunc = [polygon](int /*index*/) -> const OGRPolygon* {
                return polygon;
            };

            auto isSelectedPolygonFunc = [this](int /*index*/) -> bool {
                return (m_selectedPointId.ringId() >= 0 &&
                        m_selectedPointId.pointId() >= 0);
            };

            fillPolygonElements(1, getPolygonFunc, isSelectedPolygonFunc);
            break;
        }

        case wkbMultiPolygon: {
            const OGRMultiPolygon* mpoly =
                    static_cast<const OGRMultiPolygon*>(m_geometry.get());

            auto getPolygonFunc = [mpoly](int index) -> const OGRPolygon* {
                return static_cast<const OGRPolygon*>(
                        mpoly->getGeometryRef(index));
            };

            auto isSelectedPolygonFunc = [this](int index) -> bool {
                return (m_selectedPointId.geometryId() == index &&
                        m_selectedPointId.ringId() >= 0 &&
                        m_selectedPointId.pointId() >= 0);
            };

            fillPolygonElements(mpoly->getNumGeometries(), getPolygonFunc,
                    isSelectedPolygonFunc);
            break;
        }
    }
}

void GlEditLayerOverlay::fillPointElements(int numPoints,
        GetPointFunc getPointFunc,
        IsSelectedGeometryFunc isSelectedPointFunc)
{
    EditPointStyle* editPointStyle = ngsDynamicCast(EditPointStyle, m_pointStyle);
    GlBuffer* buffer = new GlBuffer(GlBuffer::BF_PT);
    VectorGlObject* bufferArray = new VectorGlObject();
    GlBuffer* selBuffer = new GlBuffer(GlBuffer::BF_PT);
    VectorGlObject* selBufferArray = new VectorGlObject();

    enum ngsEditElementType elementType =
            (m_walkMode) ? EET_WALK_POINT : EET_POINT;

    int index = 0;
    for(int i = 0; i < numPoints; ++i) {
        SimplePoint pt = getPointFunc(i);

        if(isSelectedPointFunc(i)) {
            if(editPointStyle) {
                editPointStyle->setEditElementType(EET_SELECTED_POINT);
            }
            m_pointStyle->addPoint(pt, 0.0f, 0, selBuffer);
            continue;
        }

        if(buffer->vertexSize() >= GlBuffer::maxVertices()) {
            bufferArray->addBuffer(buffer);
            index = 0;
            buffer = new GlBuffer(GlBuffer::BF_PT);
        }

        if(editPointStyle) {
            editPointStyle->setEditElementType(elementType);
        }
        index = m_pointStyle->addPoint(pt, 0.0f,
                                       static_cast<unsigned short>(index),
                                       buffer);
    }

    bufferArray->addBuffer(buffer);
    m_elements[elementType] = GlObjectPtr(bufferArray);
    selBufferArray->addBuffer(selBuffer);
    m_elements[EET_SELECTED_POINT] = GlObjectPtr(selBufferArray);
}

void GlEditLayerOverlay::fillMedianPointElements(int numPoints,
        GetPointFunc getPointFunc,
        IsSelectedGeometryFunc isSelectedMedianPointFunc)
{
    EditPointStyle* editPointStyle = ngsDynamicCast(EditPointStyle, m_pointStyle);
    GlBuffer* buffer = new GlBuffer(GlBuffer::BF_PT);
    VectorGlObject* bufferArray = new VectorGlObject();
    GlBuffer* selBuffer = new GlBuffer(GlBuffer::BF_PT);
    VectorGlObject* selBufferArray = new VectorGlObject();

    int index = 0;
    for(int i = 0; i < numPoints - 1; ++i) {
        SimplePoint pt1 = getPointFunc(i);
        SimplePoint pt2 = getPointFunc(i + 1);
        SimplePoint pt = ngsGetMedianPoint(pt1, pt2);

        if(isSelectedMedianPointFunc(i)) {
            if(editPointStyle)
                editPointStyle->setEditElementType(EET_SELECTED_MEDIAN_POINT);
            /*int selIndex = */
            m_pointStyle->addPoint(pt, 0.0f, 0, selBuffer);
            continue;
        }

        if(buffer->vertexSize() >= GlBuffer::maxVertices()) {
            bufferArray->addBuffer(buffer);
            index = 0;
            buffer = new GlBuffer(GlBuffer::BF_PT);
        }

        if(editPointStyle) {
            editPointStyle->setEditElementType(EET_MEDIAN_POINT);
        }
        index = m_pointStyle->addPoint(pt, 0.0f,
                                       static_cast<unsigned short>(index),
                                       buffer);
    }

    bufferArray->addBuffer(buffer);
    m_elements[EET_MEDIAN_POINT] = GlObjectPtr(bufferArray);
    selBufferArray->addBuffer(selBuffer);
    m_elements[EET_SELECTED_MEDIAN_POINT] = GlObjectPtr(selBufferArray);
}

void GlEditLayerOverlay::fillLineElements(int numLines,
        GetLineFunc getLineFunc,
        IsSelectedGeometryFunc isSelectedLineFunc,
        bool addToBuffer)
{
    VectorGlObject* bufferArray = nullptr;
    VectorGlObject* selBufferArray = nullptr;
    if(addToBuffer) {
        bufferArray = static_cast<VectorGlObject*>(m_elements[EET_LINE].get());
        selBufferArray = static_cast<VectorGlObject*>(
                m_elements[EET_SELECTED_LINE].get());
    }

    bool insertToLineElements = false;
    if(!bufferArray) {
        bufferArray = new VectorGlObject();
        insertToLineElements = true;
    }
    bool insertToSelLineElements = false;
    if(!selBufferArray) {
        selBufferArray = new VectorGlObject();
        insertToSelLineElements = true;
    }

    for(int i = 0; i < numLines; ++i) {
        const OGRLineString* line = getLineFunc(i);
        if(!line) {
            continue;
        }

        int numPoints = line->getNumPoints();
        bool isSelectedLine = isSelectedLineFunc(i);

        m_lineStyle->setEditElementType(
                (isSelectedLine) ? EET_SELECTED_LINE : EET_LINE);

        if(isSelectedLine) {
            auto getPointFunc = [line](int index) -> SimplePoint {
                OGRPoint pt;
                line->getPoint(index, &pt);
                SimplePoint spt;
                spt.x = static_cast<float>(pt.getX());
                spt.y = static_cast<float>(pt.getY());
                return spt;
            };

            fillLineBuffers(line, selBufferArray);

            if(!m_walkMode) {
                auto isSelectedMedianPointFunc = [this](int /*index*/) -> bool {
                    return false;
                };

                fillMedianPointElements(
                        numPoints, getPointFunc, isSelectedMedianPointFunc);
            }

            auto isSelectedPointFunc = [this](int index) -> bool {
                return (m_walkMode) ? false
                                    : (m_selectedPointId.pointId() == index);
            };

            fillPointElements(numPoints, getPointFunc, isSelectedPointFunc);
            continue;
        }

        fillLineBuffers(line, bufferArray);
    }

    if(insertToLineElements) {
        m_elements[EET_LINE] = GlObjectPtr(bufferArray);
    }
    if(insertToSelLineElements) {
        m_elements[EET_SELECTED_LINE] = GlObjectPtr(selBufferArray);
    }
}

void GlEditLayerOverlay::fillLineBuffers(const OGRLineString* line,
                                         VectorGlObject* bufferArray)
{
    auto getPointFunc = [line](int index) -> SimplePoint {
        OGRPoint pt;
        line->getPoint(index, &pt);
        SimplePoint spt;
        spt.x = static_cast<float>(pt.getX());
        spt.y = static_cast<float>(pt.getY());
        return spt;
    };

    GlBuffer* buffer = new GlBuffer(GlBuffer::BF_LINE);
    int numPoints = line->getNumPoints();

    if(numPoints > 0) {
        bool isClosedLine = line->get_IsClosed();
        unsigned short index = 0;
        Normal prevNormal;

        auto createBufferIfNeed = [bufferArray, &buffer, &index](
                                          size_t amount) -> void {
            if(!buffer->canStoreVertices(amount, true)) {
                bufferArray->addBuffer(buffer);
                index = 0;
                buffer = new GlBuffer(GlBuffer::BF_LINE);
            }
        };

        for(int i = 0; i < numPoints - 1; ++i) {
            SimplePoint pt1 = getPointFunc(i);
            SimplePoint pt2 = getPointFunc(i + 1);
            Normal normal = ngsGetNormals(pt1, pt2);

            if(i == 0 || i == numPoints - 2) { // Add cap
                if(!isClosedLine) {
                    if(i == 0) {
                        createBufferIfNeed(m_lineStyle->lineCapVerticesCount());
                        index = m_lineStyle->addLineCap(
                                pt1, normal, 0.0f, index, buffer);
                    }

                    if(i == numPoints - 2) {
                        createBufferIfNeed(m_lineStyle->lineCapVerticesCount());

                        Normal reverseNormal;
                        reverseNormal.x = -normal.x;
                        reverseNormal.y = -normal.y;
                        index = m_lineStyle->addLineCap(
                                pt2, reverseNormal, 0.0f, index, buffer);
                    }
                }
            }

            if(i != 0) { // Add join
                createBufferIfNeed(m_lineStyle->lineJoinVerticesCount());
                index = m_lineStyle->addLineJoin(
                        pt1, prevNormal, normal, 0.0f, index, buffer);
            }

            createBufferIfNeed(12);
            index = m_lineStyle->addSegment(
                    pt1, pt2, normal, 0.0f, index, buffer);
            prevNormal = normal;
        }
    }

    bufferArray->addBuffer(buffer);
}

void GlEditLayerOverlay::fillPolygonElements(int numPolygons,
        GetPolygonFunc getPolygonFunc,
        IsSelectedGeometryFunc isSelectedPolygonFunc)
{
    VectorGlObject* bufferArray = new VectorGlObject();
    VectorGlObject* selBufferArray = new VectorGlObject();

    for(int i = 0; i < numPolygons; ++i) {
        const OGRPolygon* polygon = getPolygonFunc(i);
        int numRings = polygon->getNumInteriorRings() + 1;
        bool isSelectedPolygon = isSelectedPolygonFunc(i);

        m_fillStyle->setEditElementType(
                (isSelectedPolygon) ? EET_SELECTED_POLYGON : EET_POLYGON);

        auto getLineFunc = [polygon](int index) -> const OGRLineString* {
            const OGRLinearRing* ring;
            if(0 == index) {
                ring = polygon->getExteriorRing();
            } else if(index <= polygon->getNumInteriorRings()) {
                ring = polygon->getInteriorRing(index - 1);
            } else {
                ring = nullptr;
            }
            return ring;
        };

        if(isSelectedPolygon) {
            auto isSelectedLineFunc = [this](int index) -> bool {
                return (m_selectedPointId.ringId() == index &&
                        m_selectedPointId.pointId() >= 0);
            };

            fillPolygonBuffers(polygon, selBufferArray);
            fillLineElements(numRings, getLineFunc, isSelectedLineFunc, true);
            continue;
        }

        auto isSelectedLineFunc = [this](int /*index*/) -> bool {
            return false;
        };

        fillPolygonBuffers(polygon, bufferArray);
        fillLineElements(numRings, getLineFunc, isSelectedLineFunc, true);
    }

    m_elements[EET_POLYGON] = GlObjectPtr(bufferArray);
    m_elements[EET_SELECTED_POLYGON] = GlObjectPtr(selBufferArray);
}

void GlEditLayerOverlay::fillPolygonBuffers(
        const OGRPolygon* polygon, VectorGlObject* bufferArray)
{
    // The number type to use for tessellation.
    using Coord = double;

    // The index type. Defaults to uint32_t, but you can also pass uint16_t
    // if you know that your data won't have more than 65536 vertices.
    using N = unsigned short;

    using MBPoint = std::array<Coord, 2>;

    // Create array.
    std::vector<std::vector<MBPoint>> mbPolygon;

    OGRPoint pt;
    for(int i = 0, num = polygon->getNumInteriorRings() + 1; i < num; ++i) {
        const OGRLinearRing* ring = (0 == i)
                ? polygon->getExteriorRing()
                : polygon->getInteriorRing(i - 1);
        if(!ring) {
            continue;
        }

        std::vector<MBPoint> mbRing;
        for(int j = 0, numPt = ring->getNumPoints(); j < numPt; ++j) {
            ring->getPoint(j, &pt);
            mbRing.emplace_back(MBPoint({pt.getX(), pt.getY()}));
        }
        mbPolygon.emplace_back(mbRing);
    }

    // Run tessellation.
    // Returns array of indices that refer to the vertices of the input polygon.
    // Three subsequent indices form a triangle.
    std::vector<N> mbIndices = mapbox::earcut<N>(mbPolygon);

    auto findPointByIndex = [mbPolygon](N index) -> OGRPoint {
        N currentIndex = index;
        for(const auto& vertices : mbPolygon) {
            if(currentIndex >= vertices.size()) {
                currentIndex -= vertices.size();
                continue;
            }

            MBPoint mbPt = vertices[currentIndex];
            return OGRPoint(mbPt[0], mbPt[1]);
        }
        return OGRPoint();
    };

	// Fill triangles.
    GlBuffer* fillBuffer = new GlBuffer(GlBuffer::BF_FILL);
    unsigned short index = 0;
    for(auto mbIndex : mbIndices) {
        if(!fillBuffer->canStoreVertices(mbIndices.size() * 3)) {
            bufferArray->addBuffer(fillBuffer);
            index = 0;
            fillBuffer = new GlBuffer(GlBuffer::BF_FILL);
        }

        pt = findPointByIndex(mbIndex);
        if(pt.IsEmpty()) { // Should never happen.
            pt.setX(BIG_VALUE);
            pt.setY(BIG_VALUE);
        }

        fillBuffer->addVertex(pt.getX());
        fillBuffer->addVertex(pt.getY());
        fillBuffer->addVertex(0.0f);

        fillBuffer->addIndex(index++);
    }
    bufferArray->addBuffer(fillBuffer);
}

void GlEditLayerOverlay::fillCrossElement()
{
    freeGlBuffer(m_elements[EET_CROSS]);

    GlBuffer* buffer = new GlBuffer(GlBuffer::BF_PT);
    VectorGlObject* bufferArray = new VectorGlObject();

    OGRRawPoint pt = m_map->getCenter();
    SimplePoint spt;
    spt.x = static_cast<float>(pt.x);
    spt.y = static_cast<float>(pt.y);

    m_crossStyle->addPoint(spt, 0.0f, 0, buffer);

    bufferArray->addBuffer(buffer);
    m_elements[EET_CROSS] = GlObjectPtr(bufferArray);
}

void GlEditLayerOverlay::freeResources()
{
    EditLayerOverlay::freeResources();
    freeGlBuffers();
    //freeGlStyles(); // TODO: only on close map
}

void GlEditLayerOverlay::freeGlStyle(StylePtr style)
{
    if(style) {
        GlView* glView = dynamic_cast<GlView*>(m_map);
        if(glView) {
            glView->freeResource(style);
        }
    }
}

void GlEditLayerOverlay::freeGlBuffer(GlObjectPtr buffer)
{
    if(buffer) {
        GlView* glView = dynamic_cast<GlView*>(m_map);
        if(glView) {
            glView->freeResource(buffer);
        }
    }
}

void GlEditLayerOverlay::freeGlBuffers()
{
    for(auto it = m_elements.begin(); it != m_elements.end(); ++it) {
        freeGlBuffer(it->second);
    }
    m_elements.clear();
}

bool GlEditLayerOverlay::draw()
{
    if(!visible()) {
        return true;
    }

    if(crossVisible()) {
        fillCrossElement();
    } else if(!m_walkMode) {
        auto findIt = m_elements.find(EET_SELECTED_POINT);
        if(m_elements.end() == findIt || !findIt->second) {
            // One of the vertices must always be selected.
            return false; // Data is not yet loaded.
        }
    }

    for(auto it = m_elements.begin(); it != m_elements.end(); ++it) {
        GlObjectPtr glBuffer = it->second;
        if(!glBuffer) {
            continue;
        }

        enum ngsEditElementType styleType = it->first;
        Style* style = nullptr;
        if(EET_POINT == styleType ||
           EET_SELECTED_POINT == styleType ||
           EET_WALK_POINT == styleType ||
           EET_MEDIAN_POINT == styleType ||
           EET_SELECTED_MEDIAN_POINT == styleType) {
            style = m_pointStyle.get();

            EditPointStyle* editPointStyle = ngsDynamicCast(EditPointStyle,
                                                            m_pointStyle);
            if(editPointStyle) {
                editPointStyle->setEditElementType(it->first);
            }
        }
        if(EET_LINE == styleType || EET_SELECTED_LINE == styleType) {
            style = m_lineStyle.get();
            m_lineStyle->setEditElementType(it->first);
        }
        if(EET_POLYGON == styleType || EET_SELECTED_POLYGON == styleType) {
            style = m_fillStyle.get();
            m_fillStyle->setEditElementType(it->first);
        }
        if(EET_CROSS == styleType) {
            style = m_crossStyle.get();
        }
        if(!style) {
            continue;
        }

        VectorGlObject* vectorGlBuffer = ngsDynamicCast(VectorGlObject, glBuffer);
        for(const GlBufferPtr& buff : vectorGlBuffer->buffers()) {
            if(buff->bound()) {
                buff->rebind();
            } else {
                buff->bind();
            }

            style->prepare(m_map->getSceneMatrix(), m_map->getInvViewMatrix(),
                           buff->type());
            style->draw(*buff);
        }
    }
    return true;
}

//------------------------------------------------------------------------------
// GlLocationOverlay
//------------------------------------------------------------------------------

GlLocationOverlay::GlLocationOverlay(MapView* map) : LocationOverlay(map),
    GlRenderOverlay(),
    m_style(static_cast<PointStyle*>(Style::createStyle("simpleLocation", nullptr)))
{
    m_style->setType(PT_DIAMOND);
    m_style->setColor({255,0,0,255});
}

bool GlLocationOverlay::setStyleName(const char* name)
{
    if(EQUAL(name, m_style->name())) {
        return true;
    }

    GlView* mapView = dynamic_cast<GlView*>(m_map);
    PointStyle* style = static_cast<PointStyle*>(
                Style::createStyle(name, mapView ? mapView->textureAtlas() : nullptr));
    if(nullptr == style) {
        return false;
    }
    if(mapView) {
        mapView->freeResource(m_style);
    }
    m_style = PointStylePtr(style);

    return true;
}

bool GlLocationOverlay::setStyle(const CPLJSONObject& style)
{
    return m_style->load(style);
}

bool GlLocationOverlay::draw()
{
    if(!visible()) {
        return true;
    }

    GlBuffer buffer(GlBuffer::BF_FILL);
    m_style->setRotation(m_direction);
    /*int index = */m_style->addPoint(m_location, 0.0f, 0, &buffer);
    LocationStyle* lStyle = ngsDynamicCast(LocationStyle, m_style);
    if(nullptr != lStyle) {
        lStyle->setStatus(isEqual(m_direction, -1.0f) ? LocationStyle::LS_STAY :
                                                        LocationStyle::LS_MOVE);
    }

    buffer.bind();
    m_style->prepare(m_map->getSceneMatrix(), m_map->getInvViewMatrix(),
                     buffer.type());
    m_style->draw(buffer);
    buffer.destroy();

    return true;
}

} // namespace ngs
