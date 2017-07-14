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
#include "layer.h"

#include "catalog/catalog.h"
#include "ds/simpledataset.h"
#include "ngstore/util/constants.h"
#include "util/error.h"

namespace ngs {

constexpr const char* LAYER_NAME_KEY = "name";
constexpr const char* LAYER_SOURCE_KEY = "src";
constexpr const char* LAYER_VISIBLE_KEY = "visible";

//------------------------------------------------------------------------------
// Layer
//------------------------------------------------------------------------------

Layer::Layer(const CPLString &name, Type type) :
    m_name(name),
    m_type(type),
    m_visible(true)
{
}

bool Layer::load(const CPLJSONObject &store, ObjectContainer * /*objectContainer*/)
{
    m_name = store.GetString(LAYER_NAME_KEY, m_name);
    m_visible = store.GetBool(LAYER_VISIBLE_KEY, m_visible);
    return true;
}

CPLJSONObject Layer::save(const ObjectContainer * /*objectContainer*/) const
{
    CPLJSONObject out;
    out.Add(LAYER_NAME_KEY, m_name);
    out.Add(LAYER_TYPE_KEY, static_cast<int>(m_type));
    out.Add(LAYER_VISIBLE_KEY, m_visible);
    return out;
}

//------------------------------------------------------------------------------
// FeatureLayer
//------------------------------------------------------------------------------

FeatureLayer::FeatureLayer(const CPLString& name) : Layer(name, Type::Vector)
{
}


bool FeatureLayer::load(const CPLJSONObject &store, ObjectContainer *objectContainer)
{
    if(!Layer::load(store, objectContainer))
        return false;

    const char* path = store.GetString(LAYER_SOURCE_KEY, "");
    ObjectPtr fcObject;
    // Check absolute or relative catalog path
    if(nullptr == objectContainer) { // absolute path
        CatalogPtr catalog = Catalog::getInstance();
        fcObject = catalog->getObject(path);
    }
    else { // relative path
        fcObject = Catalog::fromRelativePath(path, objectContainer);
    }

    m_featureClass = std::dynamic_pointer_cast<FeatureClass>(fcObject);
    if(m_featureClass)
        return true;
    return false;
}

CPLJSONObject FeatureLayer::save(const ObjectContainer *objectContainer) const
{
    CPLJSONObject out = Layer::save(objectContainer);
    // Check absolute or relative catalog path
    if(nullptr == objectContainer) { // absolute path
        out.Add(LAYER_SOURCE_KEY, m_featureClass->getPath());
    }
    else { // relative path
        out.Add(LAYER_SOURCE_KEY, Catalog::toRelativePath(m_featureClass.get(),
                                                          objectContainer));
    }
    return out;
}

//------------------------------------------------------------------------------
// RasterLayer
//------------------------------------------------------------------------------

RasterLayer::RasterLayer(const CPLString& name) : Layer(name, Type::Raster)
{
}


bool RasterLayer::load(const CPLJSONObject &store, ObjectContainer *objectContainer)
{
    if(!Layer::load(store, objectContainer))
        return false;

    const char* path = store.GetString(LAYER_SOURCE_KEY, "");
    ObjectPtr fcObject;
    // Check absolute or relative catalog path
    if(nullptr == objectContainer) { // absolute path
        CatalogPtr catalog = Catalog::getInstance();
        fcObject = catalog->getObject(path);
    }
    else { // relative path
        fcObject = Catalog::fromRelativePath(path, objectContainer);
    }

    m_raster = std::dynamic_pointer_cast<Raster>(fcObject);
    if(m_raster) {
        return m_raster->open(GDAL_OF_SHARED|GDAL_OF_READONLY|GDAL_OF_VERBOSE_ERROR);
    }
    else {
        errorMessage(_("Raster not found in path: %s"), path);
    }
    return false;
}

CPLJSONObject RasterLayer::save(const ObjectContainer *objectContainer) const
{
    CPLJSONObject out = Layer::save(objectContainer);
    // Check absolute or relative catalog path
    if(nullptr == objectContainer) { // absolute path
        out.Add(LAYER_SOURCE_KEY, m_raster->getPath());
    }
    else { // relative path
        out.Add(LAYER_SOURCE_KEY, Catalog::toRelativePath(m_raster.get(),
                                                          objectContainer));
    }
    return out;
}

} // namespace ngs
