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

#include <iterator>

#include "ds/geometry.h"
#include "map/gl/layer.h"
#include "map/mapview.h"
#include "util/error.h"
#include "util/settings.h"

namespace ngs {

constexpr double TOLERANCE_PX = 7.0;
constexpr double GEOMETRY_SIZE_PX = 50.0;
constexpr int MAX_UNDO = 10;

//------------------------------------------------------------------------------
// Overlay
//------------------------------------------------------------------------------

Overlay::Overlay(MapView* map, ngsMapOverlayType type) : m_map(map),
    m_type(type),
    m_visible(false)
{
}

//------------------------------------------------------------------------------
// LocationOverlay
//------------------------------------------------------------------------------
LocationOverlay::LocationOverlay(MapView* map) :
    Overlay(map, MOT_LOCATION),
    m_location({0.0f, 0.0f}),
    m_direction(0.0f),
    m_accuracy(0.0f)
{
}

void LocationOverlay::setLocation(const ngsCoordinate& location, float direction,
                                  float accuracy)
{
    m_location.x = static_cast<float>(location.X);
    m_location.y = static_cast<float>(location.Y);
    m_direction = direction;
    m_accuracy = accuracy;
    if(!m_visible) {
        m_visible = true;
    }
}

//------------------------------------------------------------------------------
// EditLayerOverlay
//------------------------------------------------------------------------------

EditLayerOverlay::EditLayerOverlay(MapView* map) : Overlay(map, MOT_EDIT),
    m_editFeatureId(NOT_FOUND),
    m_selectedPointId(),
    m_selectedPointCoordinates(),
    m_historyState(-1),
    m_isTouchMoved(false),
    m_isTouchingSelectedPoint(false)
{
    Settings& settings = Settings::instance();
    m_tolerancePx =
            settings.getDouble("map/overlay/edit/tolerance", TOLERANCE_PX);
}

bool EditLayerOverlay::undo()
{
    if(!canUndo())
        return false;
    return restoreFromHistory(--m_historyState);
}

bool EditLayerOverlay::redo()
{
    if(!canRedo())
        return false;
    return restoreFromHistory(++m_historyState);
}

bool EditLayerOverlay::canUndo()
{
    return m_historyState > 0 && m_historyState < static_cast<int>(m_history.size());
}

bool EditLayerOverlay::canRedo()
{
    return m_historyState >= 0 && m_historyState < static_cast<int>(m_history.size()) - 1;
}

void EditLayerOverlay::saveToHistory()
{
    if(!m_geometry) {
        return;
    }

    int stateToDelete = m_historyState + 1;
    if(stateToDelete >= 1 && stateToDelete < static_cast<int>(m_history.size())) {
        auto it = std::next(m_history.begin(), stateToDelete);
        m_history.erase(it, m_history.end());
    }

    if(m_history.size() >= MAX_UNDO + 1)
        m_history.pop_front();

    m_history.push_back(GeometryUPtr(m_geometry->clone()));
    m_historyState = static_cast<int>(m_history.size()) - 1;
}

bool EditLayerOverlay::restoreFromHistory(int historyState)
{
    if(historyState < 0 || historyState >= static_cast<int>(m_history.size()))
        return false;

    auto it = std::next(m_history.begin(), historyState);
    m_geometry = GeometryUPtr((*it)->clone());
    selectFirstPoint();
    return true;
}

void EditLayerOverlay::clearHistory()
{
    m_history.clear();
    m_historyState = -1;
}

FeaturePtr EditLayerOverlay::save()
{
    if(!m_featureClass) {
        errorMessage(_("Feature class is null"));
        return FeaturePtr();
    }

    if(m_geometry && OGR_GT_Flatten(m_geometry->getGeometryType()) > 3) { // If multi geometry is empty then delete a feature.
        OGRGeometryCollection* multyGeom = ngsDynamicCast(OGRGeometryCollection,
                                                          m_geometry);
        if(multyGeom && multyGeom->getNumGeometries() == 0) {
            m_geometry.reset();
        }
    }
    bool deleteGeometry = (!m_geometry);
    bool featureHasEdits = (m_editFeatureId >= 0);

    FeaturePtr feature;
    OGRGeometry* geom = nullptr;
    if(featureHasEdits && deleteGeometry) { // Delete a feature.
        bool featureDeleted = m_featureClass->deleteFeature(m_editFeatureId);
        if(!featureDeleted) {
            errorMessage(_("Delete feature failed"));
            return FeaturePtr();
        }
    }
    else if(!deleteGeometry) { // Insert or update a feature.
        feature = (featureHasEdits)
                ? m_featureClass->getFeature(m_editFeatureId)
                : m_featureClass->createFeature();

        geom = m_geometry.release();

        if(OGRERR_NONE != feature->SetGeometryDirectly(geom)) {
            delete geom;
            errorMessage(_("Set geometry to feature failed"));
            return FeaturePtr();
        }

        bool featureSaved = false;
        if(featureHasEdits) {
            featureSaved = m_featureClass->updateFeature(feature);
        }
        else {
            featureSaved = m_featureClass->insertFeature(feature);
        }
        if(!featureSaved) {
            errorMessage(_("Save feature failed"));
            return FeaturePtr();
        }
    }

    if(m_editLayer) { // Unhide a hidden ids.
        GlSelectableFeatureLayer* featureLayer =
                ngsDynamicCast(GlSelectableFeatureLayer, m_editLayer);
        if(featureLayer) {
            m_editFeatureId = NOT_FOUND;
            featureLayer->setHideIds(); // Empty hidden ids.
        }
    }

    if(geom) { // Redraw a geometry tile.
        OGREnvelope ogrEnv;
        geom->getEnvelope(&ogrEnv);
        m_map->invalidate(Envelope(ogrEnv));
    }
    else {
        // If geometry deleted invalidate it's oroginal envelope
        m_map->invalidate(Envelope(-0.5, -0.5, 0.5, 0.5));
    }

    freeResources();
    setVisible(false);
    return feature;
}

void EditLayerOverlay::cancel()
{
    if(m_editLayer) {
        GlSelectableFeatureLayer* featureLayer =
                ngsDynamicCast(GlSelectableFeatureLayer, m_editLayer);
        if(!featureLayer) {
            errorMessage(_("Feature layer is null"));
            return;
        }
        m_editFeatureId = NOT_FOUND;
        featureLayer->setHideIds(std::set<GIntBig>()); // Empty hidden ids.
        m_map->invalidate(Envelope());
    }

    freeResources();
    setVisible(false);
}

bool EditLayerOverlay::createGeometry(FeatureClassPtr datasource)
{
    m_featureClass = datasource;
    m_editLayer.reset();
    m_editFeatureId = NOT_FOUND;

    OGRwkbGeometryType geometryType = m_featureClass->geometryType();
    OGRRawPoint geometryCenter = m_map->getCenter();
    OGRRawPoint mapDist =
            m_map->getMapDistance(GEOMETRY_SIZE_PX, GEOMETRY_SIZE_PX);

    GeometryUPtr geometry = GeometryUPtr();
    switch(OGR_GT_Flatten(geometryType)) {
        case wkbPoint: {
            geometry = GeometryUPtr(
                    new OGRPoint(geometryCenter.x, geometryCenter.y));
            break;
        }
        case wkbLineString: {
            OGRLineString* line = new OGRLineString();
            line->addPoint(
                    geometryCenter.x - mapDist.x, geometryCenter.y - mapDist.y);
            line->addPoint(
                    geometryCenter.x + mapDist.x, geometryCenter.y + mapDist.y);
            geometry = GeometryUPtr(line);

            // FIXME: for test, remove it.
            line->addPoint(geometryCenter.x + 2 * mapDist.x,
                    geometryCenter.y - 2 * mapDist.y);
            break;
        }
        case wkbMultiPoint: {
            OGRMultiPoint* mpt = new OGRMultiPoint();
            mpt->addGeometryDirectly(
                    new OGRPoint(geometryCenter.x, geometryCenter.y));
            geometry = GeometryUPtr(mpt);
            break;
        }
        case wkbMultiLineString: {
            OGRMultiLineString* mline = new OGRMultiLineString();

            OGRLineString* line = new OGRLineString();
            line->addPoint(
                    geometryCenter.x - mapDist.x, geometryCenter.y - mapDist.y);
            line->addPoint(
                    geometryCenter.x + mapDist.x, geometryCenter.y + mapDist.y);

            // FIXME: for test, remove it.
            line->addPoint(geometryCenter.x + 2 * mapDist.x,
                    geometryCenter.y - 2 * mapDist.y);

            mline->addGeometryDirectly(line);
            geometry = GeometryUPtr(mline);

            break;
        }
        default: {
            break;
        }
    }

    setGeometry(std::move(geometry));
    if(!m_geometry) {
        return errorMessage(_("Geometry is null"));
    }
    setVisible(true);
    return true;
}

bool EditLayerOverlay::editGeometry(LayerPtr layer, GIntBig featureId)
{
    bool useLayerParam = (layer.get());
    m_editLayer = layer;

    GlSelectableFeatureLayer* featureLayer = nullptr;
    if(m_editLayer) {
        featureLayer = ngsDynamicCast(GlSelectableFeatureLayer, m_editLayer);
    }
    else {
        int layerCount = static_cast<int>(m_map->layerCount());
        for(int i = 0; i < layerCount; ++i) {
            layer = m_map->getLayer(i);
            featureLayer = ngsDynamicCast(GlSelectableFeatureLayer, layer);
            if(featureLayer && featureLayer->hasSelectedIds()) {
                // Get selection from first layer which has selected ids.
                m_editLayer = layer;
                break;
            }
        }
    }

    if(!featureLayer) {
        return errorMessage(_("Render layer is null"));
    }

    m_featureClass = std::dynamic_pointer_cast<FeatureClass>(
                featureLayer->datasource());
    if(!m_featureClass) {
        return errorMessage(_("Layer datasource is null"));
    }

    if(useLayerParam && featureId > 0) {
        m_editFeatureId = featureId;
    }
    else {
        // Get first selected feature.
        m_editFeatureId = *featureLayer->selectedIds().cbegin();
    }

    FeaturePtr feature = m_featureClass->getFeature(m_editFeatureId);
    if(!feature) {
        return errorMessage(_("Feature is null"));
    }

    GeometryUPtr geometry = GeometryUPtr(feature->GetGeometryRef()->clone());
    setGeometry(std::move(geometry));
    if(!m_geometry) {
        return errorMessage(_("Geometry is null"));
    }

    std::set<GIntBig> hideIds;
    hideIds.insert(m_editFeatureId);
    featureLayer->setHideIds(hideIds);

    OGREnvelope ogrEnv;
    m_geometry->getEnvelope(&ogrEnv);
    m_map->invalidate(Envelope(ogrEnv));

    setVisible(true);
    return true;
}

bool EditLayerOverlay::deleteGeometry()
{
    if(!m_geometry)
        return false;

    m_geometry.reset();
    m_selectedPointId = PointId();
    m_selectedPointCoordinates = OGRPoint();
    return save();
}

bool EditLayerOverlay::addPoint()
{
    if(!m_geometry)
        return false;

    OGRLineString* line; // only to line
    switch(OGR_GT_Flatten(m_geometry->getGeometryType())) {
        case wkbLineString: {
            line = ngsDynamicCast(OGRLineString, m_geometry);
            break;
        }
        case wkbMultiLineString: {
            OGRMultiLineString* ml =
                    ngsDynamicCast(OGRMultiLineString, m_geometry);
            if(!ml)
                break;
            line = dynamic_cast<OGRLineString*>(
                    ml->getGeometryRef(m_selectedPointId.geometryId()));
            break;
        }
        default:
            break; // Not supported yet
    }
    if(!line)
        return false;

    int id = line->getNumPoints() - 1; // Add after the last point.
    OGRRawPoint geometryCenter = m_map->getCenter();
    OGRPoint pt(geometryCenter.x, geometryCenter.y);
    int addedPtId = addPoint(line, id, pt);
    saveToHistory();

    m_selectedPointId.setPointId(addedPtId); // Update only pointId.
    m_selectedPointCoordinates = pt;
    return true;
}

int EditLayerOverlay::addPoint(OGRLineString* line, int id, const OGRPoint& pt)
{
    bool toLineEnd = (line->getNumPoints() - 1 == id);
    if(toLineEnd) {
        line->addPoint(&pt);
        return line->getNumPoints() - 1;
    }

    int addedPtId = id + 1;
    OGRLineString* newLine = new OGRLineString();
    newLine->addSubLineString(line, 0, id);
    newLine->addPoint(&pt);
    newLine->addSubLineString(line, addedPtId);

    line->empty();
    line->addSubLineString(newLine);
    delete newLine;
    return addedPtId;
}

bool EditLayerOverlay::deletePoint()
{
    if(!m_geometry)
        return false;

    bool ret = false;
    switch(OGR_GT_Flatten(m_geometry->getGeometryType())) { // only lines
        case wkbLineString: {
            OGRLineString* line = ngsDynamicCast(OGRLineString, m_geometry);
            if(!line)
                break;

            int minNumPoint = line->get_IsClosed() ? 3 : 2;
            if(line->getNumPoints() <= minNumPoint)
                break;

            OGRLineString* newLine = new OGRLineString();

            bool isStartPoint = (0 == m_selectedPointId.pointId());
            if(!isStartPoint)
                newLine->addSubLineString(
                        line, 0, m_selectedPointId.pointId() - 1);
            newLine->addSubLineString(line, m_selectedPointId.pointId() + 1);

            m_geometry = GeometryUPtr(newLine);
            saveToHistory();

            if(!isStartPoint)
                m_selectedPointId.setPointId(m_selectedPointId.pointId() - 1);

            if(m_selectedPointId.isValid()) {
                OGRPoint pt;
                line->getPoint(m_selectedPointId.pointId(), &pt);
                m_selectedPointCoordinates = pt;
            } else {
                m_selectedPointId = PointId();
                m_selectedPointCoordinates = OGRPoint();
            }
            ret = true;
            break;
        }
        default: {
            break;
        }
    }
    return ret;
}

bool EditLayerOverlay::addGeometryPart()
{
    if(!m_geometry)
        return false;

    OGRRawPoint geometryCenter = m_map->getCenter();
    OGRRawPoint mapDist =
            m_map->getMapDistance(GEOMETRY_SIZE_PX, GEOMETRY_SIZE_PX);
    bool ret = false;

    switch(OGR_GT_Flatten(m_geometry->getGeometryType())) { // only multi
        case wkbMultiPoint: {
            OGRMultiPoint* mpt = ngsDynamicCast(OGRMultiPoint, m_geometry);
            if(!mpt)
                break;

            OGRPoint* pt = new OGRPoint(geometryCenter.x, geometryCenter.y);

            if(OGRERR_NONE != mpt->addGeometryDirectly(pt)) {
                delete pt;
                break;
            }

            int num = mpt->getNumGeometries();
            m_selectedPointId = PointId(0, NOT_FOUND, num - 1);
            m_selectedPointCoordinates = *pt;
            ret = true;
            break;
        }
        case wkbMultiLineString: {
            OGRMultiLineString* mline = ngsDynamicCast(OGRMultiLineString, m_geometry);
            if(!mline)
                break;

            OGRPoint startPt(
                    geometryCenter.x - mapDist.x, geometryCenter.y - mapDist.y);
            OGRPoint endPt(
                    geometryCenter.x + mapDist.x, geometryCenter.y + mapDist.y);
            OGRLineString* line = new OGRLineString();
            line->addPoint(&startPt);
            line->addPoint(&endPt);

            if(OGRERR_NONE != mline->addGeometryDirectly(line)) {
                delete line;
                break;
            }

            int num = mline->getNumGeometries();
            m_selectedPointId = PointId(0, NOT_FOUND, num - 1);
            m_selectedPointCoordinates = startPt;
            ret = true;
            break;
        }
        default: {
            break;
        }
    }

    if(ret) {
        saveToHistory();
    }
    return ret;
}

bool EditLayerOverlay::deleteGeometryPart()
{
    bool deletedLastPart = false;
    if(!m_geometry)
        return deletedLastPart;

    OGRGeometryCollection* collect =
            ngsDynamicCast(OGRGeometryCollection, m_geometry);
    if(!collect || collect->getNumGeometries() == 0) // only multi
        return deletedLastPart;

    if(OGRERR_NONE
            != collect->removeGeometry(m_selectedPointId.geometryId())) {
        return deletedLastPart;
    }
    saveToHistory();

    int lastGeomId = collect->getNumGeometries() - 1;
    if(lastGeomId < 0) {
        deletedLastPart = true;
        m_selectedPointId = PointId();
        m_selectedPointCoordinates = OGRPoint();
    }

    if(!deletedLastPart) {
        const OGRGeometry* lastGeom = collect->getGeometryRef(lastGeomId);
        switch(OGR_GT_Flatten(m_geometry->getGeometryType())) { // only multi
            case wkbMultiPoint: {
                OGRMultiPoint* mpt = ngsDynamicCast(OGRMultiPoint, m_geometry);
                if(!mpt)
                    break;

                const OGRPoint* lastPt =
                        dynamic_cast<const OGRPoint*>(lastGeom);
                m_selectedPointId = PointId(0, NOT_FOUND, lastGeomId);
                m_selectedPointCoordinates = *lastPt;
                break;
            }
            case wkbMultiLineString: {
                OGRMultiLineString* mline =
                        ngsDynamicCast(OGRMultiLineString, m_geometry);
                if(!mline)
                    break;

                const OGRLineString* lastLine =
                        dynamic_cast<const OGRLineString*>(lastGeom);
                int lastPointId = lastLine->getNumPoints() - 1;
                OGRPoint lastPt;
                lastLine->getPoint(lastPointId, &lastPt);
                m_selectedPointId = PointId(lastPointId, NOT_FOUND, lastGeomId);
                m_selectedPointCoordinates = lastPt;
                break;
            }
            default: {
                break;
            }
        }
    }

    return deletedLastPart;
}

void EditLayerOverlay::setGeometry(GeometryUPtr geometry)
{
    m_geometry = std::move(geometry);
    clearHistory();
    saveToHistory();
    selectFirstPoint();
}

ngsPointId EditLayerOverlay::touch(double x, double y, enum ngsMapTouchType type)
{
    CPLDebug("ngstore", "display x: %f, display y: %f, touch type: %d", x, y, type);
    bool returnSelectedPoint = false;
    switch(type) {
        case MTT_ON_DOWN: {
            m_touchStartPoint = OGRRawPoint(x, y);
            OGRRawPoint mapPt = m_map->displayToWorld(OGRRawPoint(
                                                          m_touchStartPoint.x,
                                                          m_touchStartPoint.y));
            if(!m_map->getYAxisInverted()) {
                mapPt.y = -mapPt.y;
            }

            m_isTouchingSelectedPoint = hasSelectedPoint(mapPt);
            if(m_isTouchingSelectedPoint) {
                returnSelectedPoint = true;
            }
            break;
        }
        case MTT_ON_MOVE: {
            if(!m_isTouchMoved) {
                m_isTouchMoved = true;
            }

            OGRRawPoint pt = OGRRawPoint(x, y);
            OGRRawPoint offset(
                    pt.x - m_touchStartPoint.x, pt.y - m_touchStartPoint.y);
            OGRRawPoint mapOffset = m_map->getMapDistance(offset.x, offset.y);
            if(!m_map->getYAxisInverted()) {
                mapOffset.y = -mapOffset.y;
            }

            if(m_isTouchingSelectedPoint) {
                shiftPoint(mapOffset);
                returnSelectedPoint = true;
            }

            m_touchStartPoint = pt;
            break;
        }
        case MTT_ON_UP: {
            if(m_isTouchMoved) {
                m_isTouchMoved = false;
                if(m_isTouchingSelectedPoint) {
                    saveToHistory();
                    m_isTouchingSelectedPoint = false;
                }
            }
            break;
        case MTT_SINGLE:
            {
                OGRRawPoint mapPt = m_map->displayToWorld(OGRRawPoint(x, y));
                if(!m_map->getYAxisInverted()) {
                    mapPt.y = -mapPt.y;
                }

                if(singleTap(mapPt)) {
                    returnSelectedPoint = true;
                }
            }
            break;
        }
        default:
            break;
    }

    ngsPointId ptId;
    if(returnSelectedPoint) {
        ptId.pointId = m_selectedPointId.pointId();
        ptId.isHole = m_selectedPointId.ringId() >= 1;
    } else {
        ptId = {NOT_FOUND, 0};
    }

    CPLDebug("ngstore", "point id: %d, isHole: %d", ptId.pointId, ptId.isHole);
    return ptId;
}

bool EditLayerOverlay::singleTap(const OGRRawPoint& mapCoordinates)
{
    return clickPoint(mapCoordinates) || clickMedianPoint(mapCoordinates) ||
            clickLine(mapCoordinates);
}

bool EditLayerOverlay::clickPoint(const OGRRawPoint& mapCoordinates)
{
    if(m_geometry) {
        OGRRawPoint mapTolerance = m_map->getMapDistance(m_tolerancePx,
                                                         m_tolerancePx);

        double minX = mapCoordinates.x - mapTolerance.x;
        double maxX = mapCoordinates.x + mapTolerance.x;
        double minY = mapCoordinates.y - mapTolerance.y;
        double maxY = mapCoordinates.y + mapTolerance.y;
        Envelope mapEnv(minX, minY, maxX, maxY);

        OGRPoint coordinates;
        PointId id = PointId::getGeometryPointId(*m_geometry, mapEnv,
                                                 &m_selectedPointId, &coordinates);

        if(id.isValid()) {
            m_selectedPointId = id;
            m_selectedPointCoordinates = coordinates;
            return true;
        }
    }
    return false;
}

bool EditLayerOverlay::clickMedianPoint(const OGRRawPoint& mapCoordinates)
{
    if(!m_geometry)
        return false;

    OGRLineString* line = nullptr;
    switch(OGR_GT_Flatten(m_geometry->getGeometryType())) {
        case wkbLineString: {
            line = ngsDynamicCast(OGRLineString, m_geometry);
            break;
        }
        case wkbMultiLineString: {
            OGRMultiLineString* ml =
                    ngsDynamicCast(OGRMultiLineString, m_geometry);
            if(!ml)
                break;
            line = dynamic_cast<OGRLineString*>(
                    ml->getGeometryRef(m_selectedPointId.geometryId()));
            break;
        }
        default:
            break; // Not supported yet
    }
    if(!line)
        return false;

    OGRRawPoint mapTolerance =
            m_map->getMapDistance(m_tolerancePx, m_tolerancePx);
    double minX = mapCoordinates.x - mapTolerance.x;
    double maxX = mapCoordinates.x + mapTolerance.x;
    double minY = mapCoordinates.y - mapTolerance.y;
    double maxY = mapCoordinates.y + mapTolerance.y;
    Envelope mapEnv(minX, minY, maxX, maxY);

    OGRPoint coordinates;
    PointId id = PointId::getLineStringMedianPointId(*line, mapEnv, &coordinates);

    if(!id.isValid()) {
        return false;
    }

    int addedPtId = addPoint(line, id.pointId(), coordinates);
    saveToHistory();

    m_selectedPointId.setPointId(addedPtId); // Update only pointId.
    m_selectedPointCoordinates = coordinates;
    return true;
}

bool EditLayerOverlay::clickLine(const OGRRawPoint& mapCoordinates)
{
    if(m_geometry) {
        OGRRawPoint mapTolerance = m_map->getMapDistance(m_tolerancePx,
                                                         m_tolerancePx);

        double minX = mapCoordinates.x - mapTolerance.x;
        double maxX = mapCoordinates.x + mapTolerance.x;
        double minY = mapCoordinates.y - mapTolerance.y;
        double maxY = mapCoordinates.y + mapTolerance.y;
        Envelope mapEnv(minX, minY, maxX, maxY);

        PointId id;
        id = PointId::getGeometryPointId(*m_geometry, mapEnv);

        if(id.intersects()) {
            id.setPointId(0);
            OGRPoint coordinates = PointId::getGeometryPointCoordinates(
                        *m_geometry, id);
            m_selectedPointId = id;
            m_selectedPointCoordinates = coordinates;
            return true;
        }
    }
    return false;
}

bool EditLayerOverlay::hasSelectedPoint(const OGRRawPoint& mapCoordinates) const
{
    bool ret = m_selectedPointId.isValid();
    if(ret) {
        OGRRawPoint mapTolerance = m_map->getMapDistance(m_tolerancePx,
                                                         m_tolerancePx);

        double minX = mapCoordinates.x - mapTolerance.x;
        double maxX = mapCoordinates.x + mapTolerance.x;
        double minY = mapCoordinates.y - mapTolerance.y;
        double maxY = mapCoordinates.y + mapTolerance.y;
        Envelope mapEnv(minX, minY, maxX, maxY);

        ret = geometryIntersects(m_selectedPointCoordinates, mapEnv);
    }
    return ret;
}

bool EditLayerOverlay::selectFirstPoint()
{
    if(!m_geometry) {
        return false;
    }
    m_selectedPointId = PointId(0, 0, 0);
    m_selectedPointCoordinates = PointId::getGeometryPointCoordinates(
            *m_geometry, m_selectedPointId);
    return true;
}

bool EditLayerOverlay::shiftPoint(const OGRRawPoint& mapOffset)
{
    if(!m_geometry || !m_selectedPointId.isValid()) {
        return false;
    }

    return PointId::shiftGeometryPoint(*m_geometry, m_selectedPointId,
            mapOffset, &m_selectedPointCoordinates);
}

void EditLayerOverlay::freeResources()
{
    clearHistory();
    m_editLayer.reset();
    m_featureClass.reset();
    m_editFeatureId = NOT_FOUND;
    m_geometry.reset();
}

//------------------------------------------------------------------------------
// PointId
//------------------------------------------------------------------------------

PointId::PointId(int pointId, int ringId, int geometryId) :
        m_pointId(pointId), m_ringId(ringId), m_geometryId(geometryId)
{
}

bool PointId::operator==(const PointId& other) const
{
    return m_pointId == other.m_pointId && m_ringId == other.m_ringId &&
            m_geometryId == other.m_geometryId;
}

const PointId& PointId::setIntersects()
{
    if(m_geometryId < 0) {
        m_geometryId = 0;
    }
    return *this;
}

//------------------------------------------------------------------------------
// PointId static functions
//------------------------------------------------------------------------------

// static
PointId PointId::getGeometryPointId(const OGRGeometry& geometry,
        const Envelope env,
        const PointId* selectedPointId,
        OGRPoint* coordinates)
{
    switch(OGR_GT_Flatten(geometry.getGeometryType())) {
        case wkbPoint: {
            const OGRPoint& pt = static_cast<const OGRPoint&>(geometry);
            return getPointId(pt, env, selectedPointId, coordinates);
        }
        case wkbLineString: {
            const OGRLineString& lineString =
                    static_cast<const OGRLineString&>(geometry);
            return getLineStringPointId(
                    lineString, env, selectedPointId, coordinates);
        }
        case wkbPolygon: {
            const OGRPolygon& polygon =
                    static_cast<const OGRPolygon&>(geometry);
            return getPolygonPointId(
                    polygon, env, selectedPointId, coordinates);
        }
        case wkbMultiPoint: {
            const OGRMultiPoint& mpt =
                    static_cast<const OGRMultiPoint&>(geometry);
            return getMultiPointPointId(mpt, env, selectedPointId, coordinates);
        }
        case wkbMultiLineString: {
            const OGRMultiLineString& mline =
                    static_cast<const OGRMultiLineString&>(geometry);
            return getMultiLineStringPointId(
                    mline, env, selectedPointId, coordinates);
        }
        case wkbMultiPolygon: {
            const OGRMultiPolygon& mpolygon =
                    static_cast<const OGRMultiPolygon&>(geometry);
            return getMultiPolygonPointId(
                    mpolygon, env, selectedPointId, coordinates);
        }
    }
    return PointId();
}

// static
PointId PointId::getPointId(const OGRPoint& pt,
        const Envelope env,
        const PointId* /*selectedPointId*/,
        OGRPoint* coordinates)
{
    if(pt.IsEmpty() || !geometryIntersects(pt, env)) {
        return PointId();
    }

    if(coordinates) {
        coordinates->setX(pt.getX());
        coordinates->setY(pt.getY());
    }
    return PointId(0);
}

// static
PointId PointId::getLineStringPointId(const OGRLineString& line,
        const Envelope env,
        const PointId* selectedPointId,
        OGRPoint* coordinates)
{
    if(line.IsEmpty() || !geometryIntersects(line, env)) {
        return PointId();
    }

    int startId = 0;
    bool checkSelected = false;
    bool found = false;

    // First, check the selected point in the line.
    if(selectedPointId) {
        startId = selectedPointId->pointId();
        checkSelected = true;
    }

    OGRPoint pt;
    int pointId = startId;
    for(int num = line.getNumPoints(); pointId < num; ++pointId) {
        line.getPoint(pointId, &pt);
        if(geometryIntersects(pt, env)) {
            found = true;
            break;
        }

        // Then check the remaining points, skipping the checked point.
        if(checkSelected) {
            // Reset a counter to the start point.
            checkSelected = false;
            pointId = -1;
        } else if(pointId + 1 == startId) {
            // Skip a checking of the selected point.
            ++pointId;
        }
    }

    if(found) {
        if(coordinates) {
            coordinates->setX(pt.getX());
            coordinates->setY(pt.getY());
        }
        return PointId(pointId);
    } else {
        return PointId().setIntersects();
    }
}

// static
PointId PointId::getLineStringMedianPointId(
        const OGRLineString& line, const Envelope env, OGRPoint* coordinates)
{
    if(line.IsEmpty() || !geometryIntersects(line, env)) {
        return PointId();
    }

    int id = 0;
    bool found = false;

    OGRPoint medianPt;

    for(int i = 0, num = line.getNumPoints(); i < num - 1; ++i) {
        OGRPoint pt1;
        line.getPoint(i, &pt1);
        OGRPoint pt2;
        line.getPoint(i + 1, &pt2);
        medianPt = OGRPoint((pt2.getX() - pt1.getX()) / 2 + pt1.getX(),
                (pt2.getY() - pt1.getY()) / 2 + pt1.getY());
        if(geometryIntersects(medianPt, env)) {
            found = true;
            break;
        }
        ++id;
    }

    if(found) {
        if(coordinates) {
            coordinates->setX(medianPt.getX());
            coordinates->setY(medianPt.getY());
        }
        return PointId(id);
    } else {
        return PointId();
    }
}

// static
PointId PointId::getPolygonPointId(const OGRPolygon& polygon,
        const Envelope env,
        const PointId* selectedPointId,
        OGRPoint* coordinates)
{
    if(polygon.IsEmpty() || !geometryIntersects(polygon, env)) {
        return PointId();
    }

    int startId = 0;
    bool checkSelected = false;
    PointId intersectedId;

    // First, check the selected ring in the polygon.
    if(selectedPointId) {
        startId = selectedPointId->ringId();
        checkSelected = true;
    }

    int ringId = startId;
    for(int num = polygon.getNumInteriorRings() + 1; ringId < num; ++ringId) {
        const OGRLinearRing* ring = (ringId > 0)
                ? polygon.getInteriorRing(ringId - 1)
                : polygon.getExteriorRing();
        if(!ring) {
            continue;
        }

        PointId id =
                getLineStringPointId(*ring, env, selectedPointId, coordinates);
        if(id.isValid()) {
            return PointId(id.pointId(), ringId);
        }

        // Check an intersecting, first for the interior rings.
        if(!intersectedId.intersects() && id.intersects() && ringId > 0) {
            intersectedId.setRingId(ringId);
        }

        // Then check the remaining rings, skipping the checked ring.
        if(checkSelected) {
            // Reset a counter to the start line.
            checkSelected = false;
            ringId = -1;
        } else if(ringId + 1 == startId) {
            // Skip a checking of the selected line.
            ++ringId;
        }
    }

    // Check an intersecting, then for the exterior ring.
    if(!intersectedId.intersects()) {
        intersectedId.setRingId(0);
    }

    return intersectedId; // First intersected interior ring or exterior ring.
}

// static
PointId PointId::getMultiPointPointId(const OGRMultiPoint& mpt,
        const Envelope env,
        const PointId* selectedPointId,
        OGRPoint* coordinates)
{
    if(mpt.IsEmpty() || !geometryIntersects(mpt, env)) {
        return PointId();
    }

    int startId = 0;
    bool checkSelected = false;

    // First, check the selected point in the multi.
    if(selectedPointId) {
        startId = selectedPointId->geometryId();
        checkSelected = true;
    }

    for(int geometryId = startId, num = mpt.getNumGeometries();
            geometryId < num; ++geometryId) {

        const OGRPoint* pt =
                static_cast<const OGRPoint*>(mpt.getGeometryRef(geometryId));
        if(geometryIntersects(*pt, env)) {
            if(coordinates) {
                coordinates->setX(pt->getX());
                coordinates->setY(pt->getY());
            }
            return PointId(0, NOT_FOUND, geometryId);
        }

        // Then check the remaining points, skipping the checked point.
        if(checkSelected) {
            // Reset a counter to the start geometry.
            checkSelected = false;
            geometryId = -1;
        } else if(geometryId + 1 == startId) {
            // Skip a checking of the selected geometry.
            ++geometryId;
        }
    }

    return PointId();
}

// static
PointId PointId::getMultiLineStringPointId(const OGRMultiLineString& mline,
        const Envelope env,
        const PointId* selectedPointId,
        OGRPoint* coordinates)
{
    if(mline.IsEmpty() || !geometryIntersects(mline, env)) {
        return PointId();
    }

    int startId = 0;
    bool checkSelected = false;
    PointId intersectedId;

    // First, check the selected line in the multi.
    if(selectedPointId) {
        startId = selectedPointId->geometryId();
        checkSelected = true;
    }

    for(int geometryId = startId, numGeoms = mline.getNumGeometries();
            geometryId < numGeoms; ++geometryId) {

        const OGRLineString* line = static_cast<const OGRLineString*>(
                mline.getGeometryRef(geometryId));

        PointId id =
                getLineStringPointId(*line, env, selectedPointId, coordinates);
        if(id.isValid()) {
            return PointId(id.pointId(), NOT_FOUND, geometryId);
        }

        if(!intersectedId.intersects() && id.intersects()) {
            intersectedId.setGeometryId(geometryId);
        }

        // Then check the remaining lines, skipping the checked line.
        if(checkSelected) {
            // Reset a counter to the start geometry.
            checkSelected = false;
            geometryId = -1;
        } else if(geometryId + 1 == startId) {
            // Skip a checking of the selected geometry.
            ++geometryId;
        }
    }

    return intersectedId; // First intersected line.
}

// static
PointId PointId::getMultiPolygonPointId(const OGRMultiPolygon& mpolygon,
        const Envelope env,
        const PointId* selectedPointId,
        OGRPoint* coordinates)
{
    if(mpolygon.IsEmpty() || !geometryIntersects(mpolygon, env)) {
        return PointId();
    }

    int startId = 0;
    bool checkSelected = false;
    PointId intersectedId;

    // First, check the selected polygon in the multi.
    if(selectedPointId) {
        startId = selectedPointId->geometryId();
        checkSelected = true;
    }

    for(int geometryId = startId, numGeoms = mpolygon.getNumGeometries();
            geometryId < numGeoms; ++geometryId) {

        const OGRPolygon* polygon = static_cast<const OGRPolygon*>(
                mpolygon.getGeometryRef(geometryId));
        PointId id =
                getPolygonPointId(*polygon, env, selectedPointId, coordinates);
        if(id.isValid()) {
            return PointId(id.pointId(), id.ringId(), geometryId);
        }
        if(!intersectedId.intersects() && id.intersects()) {
            intersectedId.setRingId(id.ringId());
            intersectedId.setGeometryId(geometryId);
        }

        // Then check the remaining polygons, skipping the checked polygon.
        if(checkSelected) {
            // Reset a counter to the start geometry.
            checkSelected = false;
            geometryId = -1;
        } else if(geometryId + 1 == startId) {
            // Skip a checking of the selected geometry.
            ++geometryId;
        }
    }

    return intersectedId; // First intersected polygon.
}

// static
OGRPoint PointId::getGeometryPointCoordinates(
        const OGRGeometry& geometry, const PointId& id)
{
    switch(OGR_GT_Flatten(geometry.getGeometryType())) {
        case wkbPoint: {
            const OGRPoint& pt = static_cast<const OGRPoint&>(geometry);
            return getPointCoordinates(pt, id);
        }
        case wkbLineString: {
            const OGRLineString& lineString =
                    static_cast<const OGRLineString&>(geometry);
            return getLineStringPointCoordinates(lineString, id);
        }
        case wkbPolygon: {
            const OGRPolygon& polygon =
                    static_cast<const OGRPolygon&>(geometry);
            return getPolygonPointCoordinates(polygon, id);
        }
        case wkbMultiPoint: {
            const OGRMultiPoint& mpt =
                    static_cast<const OGRMultiPoint&>(geometry);
            return getMultiPointPointCoordinates(mpt, id);
        }
        case wkbMultiLineString: {
            const OGRMultiLineString& mline =
                    static_cast<const OGRMultiLineString&>(geometry);
            return getMultiLineStringPointCoordinates(mline, id);
        }
        case wkbMultiPolygon: {
            const OGRMultiPolygon& mpolygon =
                    static_cast<const OGRMultiPolygon&>(geometry);
            return getMultiPolygonPointCoordinates(mpolygon, id);
        }
    }
    return OGRPoint();
}

// static
OGRPoint PointId::getPointCoordinates(const OGRPoint& pt, const PointId& id)
{
    if(0 != id.pointId()) {
        return OGRPoint();
    }
    return pt;
}

// static
OGRPoint PointId::getLineStringPointCoordinates(
        const OGRLineString& line, const PointId& id)
{
    if(!id.isValid() || id.pointId() >= line.getNumPoints()) {
        return OGRPoint();
    }

    OGRPoint pt;
    line.getPoint(id.pointId(), &pt);
    return pt;
}

// static
OGRPoint PointId::getPolygonPointCoordinates(
        const OGRPolygon& polygon, const PointId& id)
{
    if(!id.isValid() || id.ringId() >= polygon.getNumInteriorRings() + 1) {
        return OGRPoint();
    }

    const OGRLinearRing* ring;
    if(id.ringId() == 0) {
        ring = polygon.getExteriorRing();
    } else {
        ring = polygon.getInteriorRing(id.ringId() - 1);
    }

    return getLineStringPointCoordinates(*ring, id);
}

// static
OGRPoint PointId::getMultiPointPointCoordinates(
        const OGRMultiPoint& mpt, const PointId& id)
{
    if(!id.isValid() || id.geometryId() >= mpt.getNumGeometries()) {
        return OGRPoint();
    }
    const OGRPoint* pt =
            static_cast<const OGRPoint*>(mpt.getGeometryRef(id.geometryId()));
    return getPointCoordinates(*pt, id);
}

// static
OGRPoint PointId::getMultiLineStringPointCoordinates(
        const OGRMultiLineString& mline, const PointId& id)
{
    if(!id.isValid() || id.geometryId() >= mline.getNumGeometries()) {
        return OGRPoint();
    }
    const OGRLineString* line = static_cast<const OGRLineString*>(
            mline.getGeometryRef(id.geometryId()));
    return getLineStringPointCoordinates(*line, id);
}

// static
OGRPoint PointId::getMultiPolygonPointCoordinates(
        const OGRMultiPolygon& mpolygon, const PointId& id)
{
    if(!id.isValid() || id.geometryId() >= mpolygon.getNumGeometries()) {
        return OGRPoint();
    }
    const OGRPolygon* polygon = static_cast<const OGRPolygon*>(
            mpolygon.getGeometryRef(id.geometryId()));
    return getPolygonPointCoordinates(*polygon, id);
}

// static
bool PointId::shiftGeometryPoint(OGRGeometry& geometry,
        const PointId& id,
        const OGRRawPoint& offset,
        OGRPoint* coordinates)
{
    switch(OGR_GT_Flatten(geometry.getGeometryType())) {
        case wkbPoint: {
            OGRPoint& pt = static_cast<OGRPoint&>(geometry);
            return shiftPoint(pt, id, offset, coordinates);
        }
        case wkbLineString: {
            OGRLineString& lineString = static_cast<OGRLineString&>(geometry);
            return shiftLineStringPoint(lineString, id, offset, coordinates);
        }
        case wkbPolygon: {
            OGRPolygon& polygon = static_cast<OGRPolygon&>(geometry);
            return shiftPolygonPoint(polygon, id, offset, coordinates);
        }
        case wkbMultiPoint: {
            OGRMultiPoint& mpt = static_cast<OGRMultiPoint&>(geometry);
            return shiftMultiPointPoint(mpt, id, offset, coordinates);
        }
        case wkbMultiLineString: {
            OGRMultiLineString& mline =
                    static_cast<OGRMultiLineString&>(geometry);
            return shiftMultiLineStringPoint(mline, id, offset, coordinates);
        }
        case wkbMultiPolygon: {
            OGRMultiPolygon& mpolygon = static_cast<OGRMultiPolygon&>(geometry);
            return shiftMultiPolygonPoint(mpolygon, id, offset, coordinates);
        }
        default: {
            return false;
        }
    }
}

// static
bool PointId::shiftPoint(OGRPoint& pt,
        const PointId& id,
        const OGRRawPoint& offset,
        OGRPoint* coordinates)
{
    if(0 != id.pointId()) {
        return false;
    }

    pt.setX(pt.getX() + offset.x);
    pt.setY(pt.getY() + offset.y);
    if(coordinates) {
        coordinates->setX(pt.getX());
        coordinates->setY(pt.getY());
    }
    return true;
}

// static
bool PointId::shiftLineStringPoint(OGRLineString& lineString,
        const PointId& id,
        const OGRRawPoint& offset,
        OGRPoint* coordinates)
{
    int pointId = id.pointId();
    if(pointId < 0 || pointId >= lineString.getNumPoints()) {
        return false;
    }

    OGRPoint pt;
    lineString.getPoint(pointId, &pt);
    lineString.setPoint(pointId, pt.getX() + offset.x, pt.getY() + offset.y);
    if(coordinates) {
        coordinates->setX(pt.getX());
        coordinates->setY(pt.getY());
    }
    return true;
}

// static
bool PointId::shiftPolygonPoint(OGRPolygon& polygon,
        const PointId& id,
        const OGRRawPoint& offset,
        OGRPoint* coordinates)
{
    int pointId = id.pointId();
    int ringId = id.ringId();
    int numInteriorRings = polygon.getNumInteriorRings();

    // ringId == 0 - exterior ring, 1+ - interior rings
    if(pointId < 0 || ringId < 0 || ringId > numInteriorRings) {
        return false;
    }

    OGRLinearRing* ring = (0 == ringId) ? polygon.getExteriorRing()
                                        : polygon.getInteriorRing(ringId - 1);
    if(!ring) {
        return false;
    }

    int ringNumPoints = ring->getNumPoints();
    if(pointId >= ringNumPoints) {
        return false;
    }

    return shiftLineStringPoint(*ring, PointId(pointId), offset, coordinates);
}

// static
bool PointId::shiftMultiPointPoint(OGRMultiPoint& mpt,
        const PointId& id,
        const OGRRawPoint& offset,
        OGRPoint* coordinates)
{
    int pointId = id.pointId();
    int geometryId = id.geometryId();
    if(pointId != 0 || geometryId < 0 || geometryId >= mpt.getNumGeometries()) {
        return false;
    }
    OGRPoint* pt = static_cast<OGRPoint*>(mpt.getGeometryRef(geometryId));
    return shiftPoint(*pt, PointId(0), offset, coordinates);
}

// static
bool PointId::shiftMultiLineStringPoint(OGRMultiLineString& mline,
        const PointId& id,
        const OGRRawPoint& offset,
        OGRPoint* coordinates)
{
    int pointId = id.pointId();
    int geometryId = id.geometryId();
    if(pointId < 0 || geometryId < 0 ||
            geometryId >= mline.getNumGeometries()) {
        return false;
    }

    OGRLineString* line =
            static_cast<OGRLineString*>(mline.getGeometryRef(geometryId));

    int lineNumPoints = line->getNumPoints();
    if(pointId >= lineNumPoints) {
        return false;
    }

    return shiftLineStringPoint(*line, PointId(pointId), offset, coordinates);
}

// static
bool PointId::shiftMultiPolygonPoint(OGRMultiPolygon& mpolygon,
        const PointId& id,
        const OGRRawPoint& offset,
        OGRPoint* coordinates)
{
    int pointId = id.pointId();
    int ringId = id.ringId();
    int geometryId = id.geometryId();
    if(pointId < 0 || ringId < 0 || geometryId < 0 ||
            geometryId >= mpolygon.getNumGeometries()) {
        return false;
    }

    OGRPolygon* polygon =
            static_cast<OGRPolygon*>(mpolygon.getGeometryRef(geometryId));

    return shiftPolygonPoint(
            *polygon, PointId(pointId, ringId), offset, coordinates);
}

} // namespace ngs
