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

#include <cstring>
#include <math.h>

#include "cpl_conv.h"
#include "ogr_core.h"

#include "layer.h"
#include "style.h"
#include "view.h"

#include "util/error.h"

namespace ngs {

//------------------------------------------------------------------------------
// IGlRenderLayer
//------------------------------------------------------------------------------

GlRenderLayer::GlRenderLayer() : m_dataMutex(CPLCreateMutex())
{
    CPLReleaseMutex(m_dataMutex);
}

GlRenderLayer::~GlRenderLayer()
{
    CPLDestroyMutex(m_dataMutex);
}

void GlRenderLayer::free(GlTilePtr tile)
{
    double lockTime = CPLAtofM(CPLGetConfigOption("HTTP_TIMEOUT", "5"));
    CPLMutexHolder holder(m_dataMutex, lockTime);
    auto it = m_tiles.find(tile->getTile());
    if(it != m_tiles.end()) {
        if(it->second) {
            it->second->destroy();
        }
        m_tiles.erase(it);
    }
    // CPLDebug("ngstore", "GlRenderLayer::free: %ld GlObject in layer", m_tiles.size());
}

//------------------------------------------------------------------------------
// GlFeatureLayer
//------------------------------------------------------------------------------

GlFeatureLayer::GlFeatureLayer(const CPLString &name) : FeatureLayer(name),
    GlRenderLayer()
{
}

bool GlFeatureLayer::fill(GlTilePtr tile, bool /*isLastTry*/)
{
    double lockTime = CPLAtofM(CPLGetConfigOption("HTTP_TIMEOUT", "5"));
    if(!m_visible) {
        CPLMutexHolder holder(m_dataMutex, lockTime);
        m_tiles[tile->getTile()] = GlObjectPtr();
        return true;
    }

    VectorGlObject *bufferArray = nullptr;
    VectorTile vtile = m_featureClass->getTile(tile->getTile(), tile->getExtent());
    if(vtile.empty()) {
        CPLMutexHolder holder(m_dataMutex, lockTime);
        m_tiles[tile->getTile()] = GlObjectPtr();
        return true;
    }

    switch(m_style->type()) {
    case Style::T_POINT:
        bufferArray = fillPoints(vtile);
        break;
    case Style::T_LINE:
        bufferArray = fillLines(vtile);
        break;
    case Style::T_FILL:
        bufferArray = fillPolygons(vtile);
        break;
    case Style::T_IMAGE:
        return true;
    }

    if(!bufferArray) {
        CPLMutexHolder holder(m_dataMutex, lockTime);
        m_tiles[tile->getTile()] = GlObjectPtr();
        return true;
    }

    CPLMutexHolder holder(m_dataMutex, lockTime);
    m_tiles[tile->getTile()] = GlObjectPtr(bufferArray);

    return true;
}

bool GlFeatureLayer::draw(GlTilePtr tile)
{
    if(!m_style) {
        return true; // Should never happened
    }

    CPLMutexHolder holder(m_dataMutex, 0.5);
    auto tileDataIt = m_tiles.find(tile->getTile());
    if(tileDataIt == m_tiles.end()) {
        return false; // Data not yet loaded
    }
    else if(!tileDataIt->second) {
        return true; // Out of tile extent
    }

    VectorGlObject *vectorGlObject = ngsStaticCast(VectorGlObject, tileDataIt->second);
    for(const GlBufferPtr& buff : vectorGlObject->buffers()) {

        if(buff->bound()) {
            buff->rebind();
        }
        else {
            buff->bind();
        }

        m_style->prepare(tile->getSceneMatrix(), tile->getInvViewMatrix(),
                         buff->type());
        m_style->draw(*buff);
    }
    return true;
}


bool GlFeatureLayer::load(const CPLJSONObject &store, ObjectContainer *objectContainer)
{
    bool result = FeatureLayer::load(store, objectContainer);
    if(!result)
        return false;
    const char* styleName = store.GetString("style_name", "");
    if(styleName != nullptr && !EQUAL(styleName, "")) {
        m_style = StylePtr(Style::createStyle(styleName));
        return m_style->load(store.GetObject("style"));
    }
    return true;
}

CPLJSONObject GlFeatureLayer::save(const ObjectContainer *objectContainer) const
{
    CPLJSONObject out = FeatureLayer::save(objectContainer);
    if(m_style) {
        out.Add("style_name", m_style->name());
        out.Add("style", m_style->save());
    }
    return out;
}

void GlFeatureLayer::setFeatureClass(const FeatureClassPtr &featureClass)
{
    FeatureLayer::setFeatureClass(featureClass);
    switch(OGR_GT_Flatten(featureClass->geometryType())) {
    case wkbPoint:
    case wkbMultiPoint:
        m_style = StylePtr(Style::createStyle("simplePoint"));
        break;
    case wkbLineString:
    case wkbMultiLineString:
        m_style = StylePtr(Style::createStyle("simpleLine"));
        break;

    case wkbPolygon:
    case wkbMultiPolygon:        
        m_style = StylePtr(Style::createStyle("simpleFillBordered"));
        break;
    }
}

VectorGlObject *GlFeatureLayer::fillPoints(const VectorTile &tile)
{
    VectorGlObject *bufferArray = new VectorGlObject;
    auto items = tile.items();
    auto it = items.begin();
    unsigned short index = 0;
    GlBuffer *buffer = new GlBuffer(GlBuffer::BF_PT);
    while(it != items.end()) {
        VectorTileItem tileItem = *it;
        if(!m_skipFIDs.empty() && tileItem.isIdsPresent(m_skipFIDs)) {
            ++it;
            continue;
        }

        if(tileItem.pointCount() < 1) {
            ++it;
            continue;
        }

        for(size_t i = 0; i < tileItem.pointCount(); ++i) {
            if(buffer->vertexSize() >= GlBuffer::maxVertices()) {
                bufferArray->addBuffer(buffer);
                index = 0;
                buffer = new GlBuffer(GlBuffer::BF_PT);
            }

            const SimplePoint& pt = tileItem.point(i);
            buffer->addVertex(pt.x);
            buffer->addVertex(pt.y);
            buffer->addVertex(0.0f);
            buffer->addIndex(index++);
        }
        ++it;
    }

    bufferArray->addBuffer(buffer);

    return bufferArray;
}

VectorGlObject *GlFeatureLayer::fillLines(const VectorTile &tile)
{
    VectorGlObject *bufferArray = new VectorGlObject;
    auto items = tile.items();
    auto it = items.begin();
    unsigned short index = 0;
    GlBuffer *buffer = new GlBuffer(GlBuffer::BF_LINE);
    while(it != items.end()) {
        VectorTileItem tileItem = *it;
        if(tileItem.isIdsPresent(m_skipFIDs)) {
            ++it;
            continue;
        }

        if(tileItem.pointCount() < 2) {
            ++it;
            continue;
        }

        // Check if line is closed or not
        bool closed = tileItem.isClosed();

        Normal prevNormal;
        for(size_t i = 0; i < tileItem.pointCount() - 1; ++i) {
            const SimplePoint& pt1 = tileItem.point(i);
            const SimplePoint& pt2 = tileItem.point(i + 1);
            Normal normal = ngsGetNormals(pt1, pt2);

            if(i == 0 || i == tileItem.pointCount() - 2) { // Add cap
                if(!closed) {
                    if(i == 0) {
                        if(!buffer->canStoreVertices(lineCapVerticesCount(), true)) {
                            bufferArray->addBuffer(buffer);
                            index = 0;
                            buffer = new GlBuffer(GlBuffer::BF_LINE);
                        }
                        index = addLineCap(pt1, normal, index, buffer);
                    }

                    if(i == tileItem.pointCount() - 2) {
                        if(!buffer->canStoreVertices(lineCapVerticesCount(), true)) {
                            bufferArray->addBuffer(buffer);
                            index = 0;
                            buffer = new GlBuffer(GlBuffer::BF_LINE);
                        }

                        Normal reverseNormal;
                        reverseNormal.x = -normal.x;
                        reverseNormal.y = -normal.y;
                        index = addLineCap(pt2, reverseNormal, index, buffer);
                    }
                }
            }

            if(i != 0) { // Add join
                if(!buffer->canStoreVertices(lineJoinVerticesCount(), true)) {
                    bufferArray->addBuffer(buffer);
                    index = 0;
                    buffer = new GlBuffer(GlBuffer::BF_LINE);
                }
                index = addLineJoin(pt1, prevNormal, normal, index, buffer);
            }

            if(!buffer->canStoreVertices(12, true)) {
                bufferArray->addBuffer(buffer);
                index = 0;
                buffer = new GlBuffer(GlBuffer::BF_LINE);
            }

            // 0
            buffer->addVertex(pt1.x);
            buffer->addVertex(pt1.y);
            buffer->addVertex(0.0f);
            buffer->addVertex(-normal.x);
            buffer->addVertex(-normal.y);
            buffer->addIndex(index++); // 0

            // 1
            buffer->addVertex(pt2.x);
            buffer->addVertex(pt2.y);
            buffer->addVertex(0.0f);
            buffer->addVertex(-normal.x);
            buffer->addVertex(-normal.y);
            buffer->addIndex(index++); // 1

            // 2
            buffer->addVertex(pt1.x);
            buffer->addVertex(pt1.y);
            buffer->addVertex(0.0f);
            buffer->addVertex(normal.x);
            buffer->addVertex(normal.y);
            buffer->addIndex(index++); // 2

            // 3
            buffer->addVertex(pt2.x);
            buffer->addVertex(pt2.y);
            buffer->addVertex(0.0f);
            buffer->addVertex(normal.x);
            buffer->addVertex(normal.y);

            buffer->addIndex(index - 2); // index = 3 at that point
            buffer->addIndex(index - 1);
            buffer->addIndex(index++);

            prevNormal = normal;
        }
        ++it;
    }
    bufferArray->addBuffer(buffer);

    return bufferArray;
}

VectorGlObject *GlFeatureLayer::fillPolygons(const VectorTile &tile)
{
    VectorGlObject *bufferArray = new VectorGlObject;
    auto items = tile.items();
    auto it = items.begin();
    unsigned short fillIndex = 0;
    unsigned short lineIndex = 0;
    GlBuffer *fillBuffer = new GlBuffer(GlBuffer::BF_FILL);
    GlBuffer *lineBuffer = new GlBuffer(GlBuffer::BF_LINE);

    while(it != items.end()) {
        VectorTileItem tileItem = *it;
        if(tileItem.isIdsPresent(m_skipFIDs)) {
            ++it;
            continue;
        }

        auto points = tileItem.points();
        auto indices = tileItem.indices();

        if(points.size() < 3 || points.size() > GlBuffer::maxIndices() ||
                points.size() > GlBuffer::maxVertices()) {
            ++it;
            continue;
        }

        // Fill polygons
        if(!fillBuffer->canStoreVertices(points.size() * 3, false)) {
            bufferArray->addBuffer(fillBuffer);
            fillIndex = 0;
            fillBuffer = new GlBuffer(GlBuffer::BF_FILL);
        }

        for(auto point : points) {
            fillBuffer->addVertex(point.x);
            fillBuffer->addVertex(point.y);
            fillBuffer->addVertex(0.0f);
        }

        // FIXME: Expected indices should fit to buffer as points can
        unsigned short maxFillIndex = 0;
        for(auto indexPoint : indices) {
            fillBuffer->addIndex(fillIndex + indexPoint);
            if(maxFillIndex < indexPoint) {
                maxFillIndex = indexPoint;
            }
        }
        fillIndex += maxFillIndex;


        // Fill borders
        // FIXME: May be more styles with borders
        if(EQUAL(m_style->name(), "simpleFillBordered")) {

        auto borders = (*it).borderIndices();
        for(auto border : borders) {
            Normal prevNormal;
            Normal firstNormal;
            bool firstNormalSet = false;
            for(size_t i = 0; i < border.size() - 1; ++i) {
                auto borderIndex = border[i];
                auto borderIndex1 = border[i + 1];
                Normal normal = ngsGetNormals(points[borderIndex], points[borderIndex1]);

                if(i == border.size() - 2) {
                    if(!lineBuffer->canStoreVertices(lineCapVerticesCount(), true)) {
                        bufferArray->addBuffer(lineBuffer);
                        lineIndex = 0;
                        lineBuffer = new GlBuffer(GlBuffer::BF_LINE);
                    }

                    Normal reverseNormal;
                    reverseNormal.x = -normal.x;
                    reverseNormal.y = -normal.y;
                    lineIndex = addLineJoin(points[borderIndex1], firstNormal,
                                        reverseNormal, lineIndex, lineBuffer);
                }

                if(i != 0) {
                    if(!lineBuffer->canStoreVertices(lineJoinVerticesCount(), true)) {
                        bufferArray->addBuffer(lineBuffer);
                        lineIndex = 0;
                        lineBuffer = new GlBuffer(GlBuffer::BF_LINE);
                    }
                    lineIndex = addLineJoin(points[borderIndex], prevNormal,
                                            normal, lineIndex, lineBuffer);
                }

                if(!lineBuffer->canStoreVertices(12, true)) {
                    bufferArray->addBuffer(lineBuffer);
                    lineIndex = 0;
                    lineBuffer = new GlBuffer(GlBuffer::BF_LINE);
                }

                // 0
                lineBuffer->addVertex(points[borderIndex].x);
                lineBuffer->addVertex(points[borderIndex].y);
                lineBuffer->addVertex(0.0f);
                lineBuffer->addVertex(-normal.x);
                lineBuffer->addVertex(-normal.y);
                lineBuffer->addIndex(lineIndex++); // 0

                // 1
                lineBuffer->addVertex(points[borderIndex1].x);
                lineBuffer->addVertex(points[borderIndex1].y);
                lineBuffer->addVertex(0.0f);
                lineBuffer->addVertex(-normal.x);
                lineBuffer->addVertex(-normal.y);
                lineBuffer->addIndex(lineIndex++); // 1

                // 2
                lineBuffer->addVertex(points[borderIndex].x);
                lineBuffer->addVertex(points[borderIndex].y);
                lineBuffer->addVertex(0.0f);
                lineBuffer->addVertex(normal.x);
                lineBuffer->addVertex(normal.y);
                lineBuffer->addIndex(lineIndex++); // 2

                // 3
                lineBuffer->addVertex(points[borderIndex1].x);
                lineBuffer->addVertex(points[borderIndex1].y);
                lineBuffer->addVertex(0.0f);
                lineBuffer->addVertex(normal.x);
                lineBuffer->addVertex(normal.y);

                lineBuffer->addIndex(lineIndex - 2); // index = 3 at that point
                lineBuffer->addIndex(lineIndex - 1);
                lineBuffer->addIndex(lineIndex++);

                prevNormal = normal;
                if(!firstNormalSet) {
                    firstNormal.x = -prevNormal.x;
                    firstNormal.y = -prevNormal.y;
                    firstNormalSet = true;
                }
            }
        }
        }

        ++it;
    }

    bufferArray->addBuffer(fillBuffer);
    bufferArray->addBuffer(lineBuffer);

    return bufferArray;
}

unsigned short GlFeatureLayer::addLineCap(const SimplePoint &point,
                                          const Normal &normal,
                                          unsigned short index, GlBuffer *buffer)
{
    enum CapType capType = CapType::CT_BUTT;
    unsigned char segmentCount = 0;

    if(m_style->type() == Style::T_LINE) {
        SimpleLineStyle* lineStyle = ngsDynamicCast(SimpleLineStyle, m_style);
        if(lineStyle) {
            capType = lineStyle->capType();
            segmentCount = lineStyle->segmentCount();
        }
    }
    else if(m_style->type() == Style::T_FILL) {
        SimpleFillBorderedStyle* fillStyle = ngsDynamicCast(
                    SimpleFillBorderedStyle, m_style);
        if(fillStyle) {
            capType = fillStyle->capType();
            segmentCount = fillStyle->segmentCount();
        }
    }

    switch(capType) {
        case CapType::CT_ROUND:
        {
            float start = asinf(normal.y);
            if(normal.x < 0.0f && normal.y <= 0.0f)
                start = M_PI_F + -(start);
            else if(normal.x < 0.0f && normal.y >= 0.0f)
                start = M_PI_2_F + start;
            else if(normal.x > 0.0f && normal.y <= 0.0f)
                start = M_PI_F + M_PI_F + start;

            float end = M_PI_F + start;
            float step = (end - start) / segmentCount;
            float current = start;
            for(int i = 0 ; i < segmentCount; ++i) {
                float x = cosf(current);
                float y = sinf(current);
                current += step;
                buffer->addVertex(point.x);
                buffer->addVertex(point.y);
                buffer->addVertex(0.0f);
                buffer->addVertex(x);
                buffer->addVertex(y);

                x = cosf(current);
                y = sinf(current);
                buffer->addVertex(point.x);
                buffer->addVertex(point.y);
                buffer->addVertex(0.0f);
                buffer->addVertex(x);
                buffer->addVertex(y);

                buffer->addVertex(point.x);
                buffer->addVertex(point.y);
                buffer->addVertex(0.0f);
                buffer->addVertex(0.0f);
                buffer->addVertex(0.0f);

                buffer->addIndex(index++);
                buffer->addIndex(index++);
                buffer->addIndex(index++);
            }
        }
            break;
        case CapType::CT_BUTT:
            break;
        case CapType::CT_SQUARE:
        {
        float scX1 = -(normal.y + normal.x);
        float scY1 = -(normal.y - normal.x);
        float scX2 = normal.x - normal.y;
        float scY2 = normal.x + normal.y;

        // 0
        buffer->addVertex(point.x);
        buffer->addVertex(point.y);
        buffer->addVertex(0.0f);
        buffer->addVertex(scX1);
        buffer->addVertex(scY1);

        // 1
        buffer->addVertex(point.x);
        buffer->addVertex(point.y);
        buffer->addVertex(0.0f);
        buffer->addVertex(scX2);
        buffer->addVertex(scY2);

        // 2
        buffer->addVertex(point.x);
        buffer->addVertex(point.y);
        buffer->addVertex(0.0f);
        buffer->addVertex(-normal.x);
        buffer->addVertex(-normal.y);

        // 3
        buffer->addVertex(point.x);
        buffer->addVertex(point.y);
        buffer->addVertex(0.0f);
        buffer->addVertex(normal.x);
        buffer->addVertex(normal.y);

        buffer->addIndex(index + 0);
        buffer->addIndex(index + 1);
        buffer->addIndex(index + 2);

        buffer->addIndex(index + 3);
        buffer->addIndex(index + 2);
        buffer->addIndex(index + 1);

        index += 4;
        }
    }

    return index;
}

size_t GlFeatureLayer::lineCapVerticesCount() const
{
    enum CapType capType = CapType::CT_BUTT;
    unsigned char segmentCount = 0;

    if(m_style->type() == Style::T_LINE) {
        SimpleLineStyle* lineStyle = ngsDynamicCast(SimpleLineStyle, m_style);
        if(lineStyle) {
            capType = lineStyle->capType();
            segmentCount = lineStyle->segmentCount();
        }
    }
    else if(m_style->type() == Style::T_FILL) {
        SimpleFillBorderedStyle* fillStyle = ngsDynamicCast(
                    SimpleFillBorderedStyle, m_style);
        if(fillStyle) {
            capType = fillStyle->capType();
            segmentCount = fillStyle->segmentCount();
        }
    }

    switch(capType) {
        case CapType::CT_ROUND:
            return 3 * segmentCount;
        case CapType::CT_BUTT:
            return 0;
        case CapType::CT_SQUARE:
            return 2;
    }

    return 0;
}

float angle(const Normal &normal) {
    if(isEqual(normal.y, 0.0f)) {
        if(normal.x > 0.0f) {
            return 0.0f;
        }
        else {
            return M_PI_F;
        }
    }

    if(isEqual(normal.x, 0.0f)) {
        if(normal.y > 0.0f) {
            return M_PI_2_F;
        }
        else {
            return -M_PI_2_F;
        }
    }

    float angle = std::fabs(asinf(normal.y));
    if(normal.x < 0.0f && normal.y >= 0.0f)
        angle = M_PI_F - angle;
    else if(normal.x < 0.0f && normal.y <= 0.0f)
        angle = angle - M_PI_F;
    else if(normal.x > 0.0f && normal.y <= 0.0f)
        angle = -angle;
    return angle;
}

unsigned short GlFeatureLayer::addLineJoin(const SimplePoint &point,
                                           const Normal &prevNormal,
                                           const Normal &normal,
                                           unsigned short index, GlBuffer *buffer)
{
    enum JoinType joinType = JoinType::JT_ROUND;
    unsigned char segmentCount = 0;
    float maxWidth = 0.0f;
    float start = angle(prevNormal);
    float end = angle(normal);

    float angle = end - start;
    char mult = angle >= 0 ? -1 : 1;

    if(m_style->type() == Style::T_LINE) {
        SimpleLineStyle* lineStyle = ngsDynamicCast(SimpleLineStyle, m_style);
        if(lineStyle) {
            joinType = lineStyle->joinType();
            segmentCount = lineStyle->segmentCount();
            maxWidth = lineStyle->width() * 5;
        }
    }
    else if(m_style->type() == Style::T_FILL) {
        SimpleFillBorderedStyle* fillStyle = ngsDynamicCast(
                    SimpleFillBorderedStyle, m_style);
        if(fillStyle) {
            joinType = fillStyle->joinType();
            segmentCount = fillStyle->segmentCount();
            maxWidth = fillStyle->borderWidth() * 5;
        }
    }

    switch(joinType) {
    case JoinType::JT_ROUND:
    {
        float step = angle / segmentCount;
        float current = start;
        for(int i = 0 ; i < segmentCount; ++i) {
            float x = cosf(current) * mult;
            float y = sinf(current) * mult;

            buffer->addVertex(point.x);
            buffer->addVertex(point.y);
            buffer->addVertex(0.0f);
            buffer->addVertex(x);
            buffer->addVertex(y);

            current += step;
            x = cosf(current) * mult;
            y = sinf(current) * mult;
            buffer->addVertex(point.x);
            buffer->addVertex(point.y);
            buffer->addVertex(0.0f);
            buffer->addVertex(x);
            buffer->addVertex(y);

            buffer->addVertex(point.x);
            buffer->addVertex(point.y);
            buffer->addVertex(0.0f);
            buffer->addVertex(0.0f);
            buffer->addVertex(0.0f);

            buffer->addIndex(index++);
            buffer->addIndex(index++);
            buffer->addIndex(index++);
        }
    }
        break;
    case JoinType::JT_MITER:
    {
        Normal newNormal;
        newNormal.x = (prevNormal.x + normal.x);
        newNormal.y = (prevNormal.y + normal.y);
        float cosHalfAngle = newNormal.x * normal.x + newNormal.y * normal.y;
        float miterLength = cosHalfAngle == 0.0f ? 0.0f : 1.0f / cosHalfAngle;
        newNormal.x *= miterLength;
        newNormal.y *= miterLength;

        // 0
        buffer->addVertex(point.x);
        buffer->addVertex(point.y);
        buffer->addVertex(0.0f);
        buffer->addVertex(prevNormal.x * mult);
        buffer->addVertex(prevNormal.y * mult);

        // 1
        buffer->addVertex(point.x);
        buffer->addVertex(point.y);
        buffer->addVertex(0.0f);
        buffer->addVertex(newNormal.x * mult);
        buffer->addVertex(newNormal.y * mult);

        // 2
        buffer->addVertex(point.x);
        buffer->addVertex(point.y);
        buffer->addVertex(0.0f);
        buffer->addVertex(0.0f);
        buffer->addVertex(0.0f);

        buffer->addIndex(index++);
        buffer->addIndex(index++);
        buffer->addIndex(index++);

        // 0
        buffer->addVertex(point.x);
        buffer->addVertex(point.y);
        buffer->addVertex(0.0f);
        buffer->addVertex(normal.x * mult);
        buffer->addVertex(normal.y * mult);

        // 1
        buffer->addVertex(point.x);
        buffer->addVertex(point.y);
        buffer->addVertex(0.0f);
        buffer->addVertex(newNormal.x * mult);
        buffer->addVertex(newNormal.y * mult);

        // 2
        buffer->addVertex(point.x);
        buffer->addVertex(point.y);
        buffer->addVertex(0.0f);
        buffer->addVertex(0.0f);
        buffer->addVertex(0.0f);

        buffer->addIndex(index++);
        buffer->addIndex(index++);
        buffer->addIndex(index++);
    }
        break;
    case JoinType::JT_BEVELED:
    {
        // 0
        buffer->addVertex(point.x);
        buffer->addVertex(point.y);
        buffer->addVertex(0.0f);
        buffer->addVertex(prevNormal.x * mult);
        buffer->addVertex(prevNormal.y * mult);

        // 1
        buffer->addVertex(point.x);
        buffer->addVertex(point.y);
        buffer->addVertex(0.0f);
        buffer->addVertex(normal.x * mult);
        buffer->addVertex(normal.y * mult);

        // 2
        buffer->addVertex(point.x);
        buffer->addVertex(point.y);
        buffer->addVertex(0.0f);
        buffer->addVertex(0.0f);
        buffer->addVertex(0.0f);

        buffer->addIndex(index++);
        buffer->addIndex(index++);
        buffer->addIndex(index++);
    }
    }
    return index;
}

size_t GlFeatureLayer::lineJoinVerticesCount() const
{
    enum JoinType joinType = JoinType::JT_ROUND;
    unsigned char segmentCount = 0;

    if(m_style->type() == Style::T_LINE) {
        SimpleLineStyle* lineStyle = ngsDynamicCast(SimpleLineStyle, m_style);
        if(lineStyle) {
            joinType = lineStyle->joinType();
            segmentCount = lineStyle->segmentCount();
        }
    }
    else if(m_style->type() == Style::T_FILL) {
        SimpleFillBorderedStyle* fillStyle = ngsDynamicCast(
                    SimpleFillBorderedStyle, m_style);
        if(fillStyle) {
            joinType = fillStyle->joinType();
            segmentCount = fillStyle->segmentCount();
        }
    }

    switch(joinType) {
    case JoinType::JT_ROUND:
        return 3 * segmentCount;
    case JoinType::JT_MITER:
        return 6;
    case JoinType::JT_BEVELED:
        return 3;
    }

    return 0;
}

//------------------------------------------------------------------------------
// GlRasterLayer
//------------------------------------------------------------------------------

GlRasterLayer::GlRasterLayer(const CPLString &name) : RasterLayer(name),
    GlRenderLayer(),
    m_red(1),
    m_green(2),
    m_blue(3),
    m_alpha(0),
    m_transparancy(0),
    m_dataType(GDT_Byte)
{
}

bool GlRasterLayer::fill(GlTilePtr tile, bool isLastTry)
{
    double lockTime = CPLAtofM(CPLGetConfigOption("HTTP_TIMEOUT", "5"));
    if(!m_visible) {
        CPLMutexHolder holder(m_dataMutex, lockTime);
        m_tiles[tile->getTile()] = GlObjectPtr();
        return true;
    }

    if(m_tiles.find(tile->getTile()) != m_tiles.end()) { // Already filled
        return true;
    }

    Envelope rasterExtent = m_raster->extent();
    const Envelope & tileExtent = tile->getExtent();

    // FIXME: Reproject tile extent to raster extent

    Envelope outExt = rasterExtent.intersect(tileExtent);

    if(!outExt.isInit()) {
        CPLMutexHolder holder(m_dataMutex, lockTime);
        m_tiles[tile->getTile()] = GlObjectPtr();
        return true;
    }

    // Create inverse geotransform to get pixel data
    double geoTransform[6] = { 0 };
    double invGeoTransform[6] = { 0 };
    bool noTransform = false;
    if(m_raster->geoTransform(geoTransform)) {
        noTransform = GDALInvGeoTransform(geoTransform, invGeoTransform) == 0;
    }
    else {
        noTransform = true;
    }

    // Calc output buffer width and height
    int outWidth = static_cast<int>(rasterExtent.width() *
                                    tile->getSizeInPixels() /
                                    tileExtent.width());
    int outHeight = static_cast<int>(rasterExtent.height() *
                                     tile->getSizeInPixels() /
                                     tileExtent.height());

    if(noTransform) {
        // Swap min/max Y
        rasterExtent.setMaxY(m_raster->height() - rasterExtent.minY());
        rasterExtent.setMinY(m_raster->height() - rasterExtent.maxY());
    }
    else {
        double minX, minY, maxX, maxY;
        GDALApplyGeoTransform(invGeoTransform, rasterExtent.minX(),
                              rasterExtent.minY(), &minX, &maxY);

        GDALApplyGeoTransform(invGeoTransform, rasterExtent.maxX(),
                              rasterExtent.maxY(), &maxX, &minY);
        rasterExtent.setMinX(minX);
        rasterExtent.setMaxX(maxX);
        rasterExtent.setMinY(minY);
        rasterExtent.setMaxY(maxY);
    }

    rasterExtent.fix();

    // Get width & height in pixels of raster area
    int width = static_cast<int>(std::ceil(rasterExtent.width()));
    int height = static_cast<int>(std::ceil(rasterExtent.height()));
    int minX = static_cast<int>(std::floor(rasterExtent.minX()));
    int minY = static_cast<int>(std::floor(rasterExtent.minY()));

    // Correct data
    if(minX < 0) {
        minX = 0;
    }
    if(minY < 0) {
        minY = 0;
    }
    if(width - minX > m_raster->width()) {
        width = m_raster->width() - minX;
    }
    if(height - minY > m_raster->height()) {
        height = m_raster->height() - minY;
    }

    // Memset 255 for buffer and read data skipping 4 byte
    int bandCount = 4;
    int bands[4];
    bands[0] = m_red;
    bands[1] = m_green;
    bands[2] = m_blue;
    bands[3] = m_alpha;

    int overview = 18;
    if(outWidth > width && outHeight > height ) { // Read original raster
        outWidth = width;
        outHeight = height;
    }
    else { // Get closest overview and get overview data
        int minXOv = minX;
        int minYOv = minY;
        int outWidthOv = width;
        int outHeightOv = height;
        overview = m_raster->getBestOverview(minXOv, minYOv, outWidthOv, outHeightOv,
                                                 outWidth, outHeight);
        if(overview >= 0) {
            outWidth = outWidthOv;
            outHeight = outHeightOv;
        }
    }

    int dataSize = GDALGetDataTypeSize(m_dataType) / 8;
    size_t bufferSize = static_cast<size_t>(outWidth * outHeight *
                                            dataSize * 4); // NOTE: We use RGBA to store textures
    GLubyte* pixData = static_cast<GLubyte*>(CPLMalloc(bufferSize));
    if(m_alpha == 0) {
        std::memset(pixData, 255 - m_transparancy, bufferSize);
        if(!m_raster->pixelData(pixData, minX, minY, width, height, outWidth,
                                outHeight, m_dataType, bandCount, bands, true, true,
                                static_cast<unsigned char>(18 - overview))) {
            CPLFree(pixData);

            if(isLastTry) {
                CPLMutexHolder holder(m_dataMutex, lockTime);
                m_tiles[tile->getTile()] = GlObjectPtr();
                return true;
            }

            // TODO: Get overzoom or underzoom pixels here

            return false;
        }
    }
    else {
        if(!m_raster->pixelData(pixData, minX, minY, width, height, outWidth,
                                outHeight, m_dataType, bandCount, bands)) {
            CPLFree(pixData);

            if(isLastTry) {
                CPLMutexHolder holder(m_dataMutex, lockTime);
                m_tiles[tile->getTile()] = GlObjectPtr();
                return true;
            }

            return false;
        }
    }

    GlImage *image = new GlImage;
    image->setImage(pixData, outWidth, outHeight); // NOTE: May be not working NOD
//    image->setSmooth(true);

    // FIXME: Reproject intersect raster extent to tile extent
    GlBuffer* tileExtentBuff = new GlBuffer(GlBuffer::BF_TEX);
    tileExtentBuff->addVertex(static_cast<float>(outExt.minX() - 0.2));
    tileExtentBuff->addVertex(static_cast<float>(outExt.minY() - 0.2));
    tileExtentBuff->addVertex(0.0f);
    tileExtentBuff->addVertex(0.0f);
    tileExtentBuff->addVertex(1.0f);
    tileExtentBuff->addIndex(0);
    tileExtentBuff->addVertex(static_cast<float>(outExt.minX() - 0.2));
    tileExtentBuff->addVertex(static_cast<float>(outExt.maxY() + 0.2));
    tileExtentBuff->addVertex(0.0f);
    tileExtentBuff->addVertex(0.0f);
    tileExtentBuff->addVertex(0.0f);
    tileExtentBuff->addIndex(1);
    tileExtentBuff->addVertex(static_cast<float>(outExt.maxX() + 0.2));
    tileExtentBuff->addVertex(static_cast<float>(outExt.maxY() + 0.2));
    tileExtentBuff->addVertex(0.0f);
    tileExtentBuff->addVertex(1.0f);
    tileExtentBuff->addVertex(0.0f);
    tileExtentBuff->addIndex(2);
    tileExtentBuff->addVertex(static_cast<float>(outExt.maxX() + 0.2));
    tileExtentBuff->addVertex(static_cast<float>(outExt.minY() - 0.2));
    tileExtentBuff->addVertex(0.0f);
    tileExtentBuff->addVertex(1.0f);
    tileExtentBuff->addVertex(1.0f);
    tileExtentBuff->addIndex(0);
    tileExtentBuff->addIndex(2);
    tileExtentBuff->addIndex(3);

    GlObjectPtr tileData(new RasterGlObject(tileExtentBuff, image));

    CPLMutexHolder holder(m_dataMutex, lockTime);
    m_tiles[tile->getTile()] = tileData;

    return true;
}

bool GlRasterLayer::draw(GlTilePtr tile)
{
    double lockTime = CPLAtofM(CPLGetConfigOption("HTTP_TIMEOUT", "5"));
    CPLAcquireMutex(m_dataMutex, lockTime);
    //CPLMutexHolder holder(m_dataMutex, lockTime);
    auto tileDataIt = m_tiles.find(tile->getTile());
    if(tileDataIt == m_tiles.end()) {
        CPLReleaseMutex(m_dataMutex);
        return false; // Data not yet loaded
    }

    auto second = tileDataIt->second;
    CPLReleaseMutex(m_dataMutex);

    if(!second) {
        return true; // Out of tile extent
    }    

    RasterGlObject* rasterGlObject = ngsStaticCast(RasterGlObject, second);

    // Bind everything before call prepare and set matrices
    GlImage* img = rasterGlObject->getImageRef();
    ngsStaticCast(SimpleImageStyle, m_style)->setImage(img);
    GlBuffer* extBuff = rasterGlObject->getBufferRef();
    if(extBuff->bound()) {
        extBuff->rebind();
    }
    else {
        extBuff->bind();
    }

    m_style->prepare(tile->getSceneMatrix(), tile->getInvViewMatrix(),
                     extBuff->type());
    m_style->draw(*extBuff);

    return true;

/*/    Envelope ext = tile->getExtent();
//    ext.resize(0.9);

//    std::array<OGRPoint, 6> points;
//    points[0] = OGRPoint(ext.getMinX(), ext.getMinY());
//    points[1] = OGRPoint(ext.getMinX(), ext.getMaxY());
//    points[2] = OGRPoint(ext.getMaxX(), ext.getMaxY());
//    points[3] = OGRPoint(ext.getMaxX(), ext.getMinY());
//    points[4] = OGRPoint(ext.getMinX(), ext.getMinY());
//    points[5] = OGRPoint(ext.getMaxX(), ext.getMaxY());
//    for(size_t i = 0; i < points.size() - 1; ++i) {
//        Normal normal = ngsGetNormals(points[i], points[i + 1]);

//        GlBuffer buffer1;
//        // 0
//        buffer1.addVertex(points[i].getX());
//        buffer1.addVertex(points[i].getY());
//        buffer1.addVertex(0.0f);
//        buffer1.addVertex(-normal.x);
//        buffer1.addVertex(-normal.y);
//        buffer1.addIndex(0);

//        // 1
//        buffer1.addVertex(points[i + 1].getX());
//        buffer1.addVertex(points[i + 1].getY());
//        buffer1.addVertex(0.0f);
//        buffer1.addVertex(-normal.x);
//        buffer1.addVertex(-normal.y);
//        buffer1.addIndex(1);

//        // 2
//        buffer1.addVertex(points[i].getX());
//        buffer1.addVertex(points[i].getY());
//        buffer1.addVertex(0.0f);
//        buffer1.addVertex(normal.x);
//        buffer1.addVertex(normal.y);
//        buffer1.addIndex(2);

//        // 3
//        buffer1.addVertex(points[i + 1].getX());
//        buffer1.addVertex(points[i + 1].getY());
//        buffer1.addVertex(0.0f);
//        buffer1.addVertex(normal.x);
//        buffer1.addVertex(normal.y);

//        buffer1.addIndex(1);
//        buffer1.addIndex(2);
//        buffer1.addIndex(3);

//        SimpleLineStyle style;
//        style.setLineWidth(14.0f);
//        style.setColor({0, 0, 255, 255});
//        buffer1.bind();
//        style.prepare(tile->getSceneMatrix(), tile->getInvViewMatrix());
//        style.draw(buffer1);

//        buffer1.destroy();
//        style.destroy();
//    }

//    return true; */
}

bool GlRasterLayer::load(const CPLJSONObject &store, ObjectContainer *objectContainer)
{
    bool result = RasterLayer::load(store, objectContainer);
    if(!result)
        return false;
    CPLJSONObject raster = store.GetObject("raster");
    if(raster.IsValid()) {
        m_red = static_cast<unsigned char>(raster.GetInteger("red", m_red));
        m_green = static_cast<unsigned char>(raster.GetInteger("green", m_green));
        m_blue = static_cast<unsigned char>(raster.GetInteger("blue", m_blue));
        m_alpha = static_cast<unsigned char>(raster.GetInteger("alpha", m_alpha));
        m_transparancy = static_cast<unsigned char>(raster.GetInteger("transparancy",
                                                                      m_transparancy));
    }

    m_style = StylePtr(Style::createStyle("simpleImage"));
    return true;
}

CPLJSONObject GlRasterLayer::save(const ObjectContainer *objectContainer) const
{
    CPLJSONObject out = RasterLayer::save(objectContainer);
    CPLJSONObject raster;
    raster.Add("red", m_red);
    raster.Add("green", m_green);
    raster.Add("blue", m_blue);
    raster.Add("alpha", m_alpha);
    raster.Add("transparancy", m_transparancy);
    out.Add("raster", raster);
    return out;
}

void GlRasterLayer::setRaster(const RasterPtr &raster)
{
    RasterLayer::setRaster(raster);
    // Create default style
    m_style = StylePtr(Style::createStyle("simpleImage"));

    if(raster->bandCount() == 4) {
        m_alpha = 4;
    }
}

//------------------------------------------------------------------------------
// RasterGlObject
//------------------------------------------------------------------------------

RasterGlObject::RasterGlObject(GlBuffer* tileExtentBuff, GlImage *image) :
    GlObject(),
    m_extentBuffer(tileExtentBuff),
    m_image(image)
{

}

void RasterGlObject::bind()
{
    m_extentBuffer->bind();
    m_image->bind();
}

void RasterGlObject::rebind() const
{
    m_extentBuffer->rebind();
    m_image->rebind();
}

void RasterGlObject::destroy()
{
    m_extentBuffer->destroy();
    m_image->destroy();
}

//------------------------------------------------------------------------------
// VectorGlObject
//------------------------------------------------------------------------------

VectorGlObject::VectorGlObject() :
    GlObject()
{

}

void VectorGlObject::bind()
{
    if(m_bound)
        return;
    for(GlBufferPtr& buffer : m_buffers) {
        buffer->bind();
    }
    m_bound = true;
}

void VectorGlObject::rebind() const
{
    for(const GlBufferPtr& buffer : m_buffers) {
        buffer->rebind();
    }
}

void VectorGlObject::destroy()
{
    for(GlBufferPtr& buffer : m_buffers) {
        buffer->destroy();
    }
}

} // namespace ngs
