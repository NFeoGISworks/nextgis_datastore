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
#include "mapview.h"

#include "api_priv.h"
#include "catalog/mapfile.h"

namespace ngs {

constexpr const char* MAP_EXTENT_KEY = "extent";
constexpr const char* MAP_ROTATE_X_KEY = "rotate_x";
constexpr const char* MAP_ROTATE_Y_KEY = "rotate_y";
constexpr const char* MAP_ROTATE_Z_KEY = "rotate_z";
constexpr const char* MAP_X_LOOP_KEY = "x_looped";

MapView::MapView() : Map(), MapTransform(480, 640)
{
}

MapView::MapView(const CPLString &name, const CPLString &description,
                 unsigned short epsg, const Envelope &bounds) :
    Map(name, description, epsg, bounds), MapTransform(480, 640)
{
}

bool MapView::draw(ngsDrawState state, const Progress &progress)
{
    clearBackground();

    if(m_layers.empty()) {
        progress.onProgress(ngsCode::COD_FINISHED, 1.0,
                            _("No layers. Nothing to render."));
        return true;
    }

    float level = 0.0f;
    double done = 0.0;
    for(auto it = m_layers.rbegin(); it != m_layers.rend(); ++it) {
        LayerPtr layer = *it;
        IRenderLayer* const renderLayer = ngsDynamicCast(IRenderLayer, layer);
        done += renderLayer->draw(state, getExtent(), getZoom(), level++, progress);
    }

    if(isEqual(done, m_layers.size())) {
        progress.onProgress(ngsCode::COD_FINISHED, 1.0,
                            _("Map render finished."));
    }
    else {
        progress.onProgress(ngsCode::COD_IN_PROCESS, done / m_layers.size(),
                            _("Rendering ..."));
    }

    return true;
}

bool MapView::openInternal(const JSONObject &root, MapFile * const mapFile)
{
    if(!Map::openInternal(root, mapFile))
        return false;

    setRotate(ngsDirection::DIR_X, root.getDouble(MAP_ROTATE_X_KEY, 0));
    setRotate(ngsDirection::DIR_Y, root.getDouble(MAP_ROTATE_Y_KEY, 0));
    setRotate(ngsDirection::DIR_Z, root.getDouble(MAP_ROTATE_Z_KEY, 0));

    Envelope env;
    env.load(root.getObject(MAP_EXTENT_KEY), DEFAULT_BOUNDS);
    setExtent(env);

    m_XAxisLooped = root.getBool(MAP_X_LOOP_KEY, true);

    return true;
}

bool MapView::saveInternal(JSONObject &root, MapFile * const mapFile)
{
    if(!Map::saveInternal(root, mapFile))
        return false;

    root.add(MAP_EXTENT_KEY, getExtent().save());
    root.add(MAP_ROTATE_X_KEY, getRotate(ngsDirection::DIR_X));
    root.add(MAP_ROTATE_Y_KEY, getRotate(ngsDirection::DIR_Y));
    root.add(MAP_ROTATE_Z_KEY, getRotate(ngsDirection::DIR_Z));

    root.add(MAP_X_LOOP_KEY, m_XAxisLooped);
    return true;
}

} // namespace ngs
