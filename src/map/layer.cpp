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

namespace ngs {

constexpr const char* LAYER_NAME_KEY = "name";
constexpr const char* LAYER_SOURCE_KEY = "src";

//------------------------------------------------------------------------------
// Layer
//------------------------------------------------------------------------------

Layer::Layer(const CPLString &name, Type type) :
    m_name(name),
    m_type(type)
{
}

bool Layer::load(const JSONObject& store, ObjectContainer * /*objectContainer*/)
{
    m_name = store.getString(LAYER_NAME_KEY, DEFAULT_LAYER_NAME);
    return true;
}

JSONObject Layer::save(const ObjectContainer * /*objectContainer*/) const
{
    JSONObject out;
    out.add(LAYER_NAME_KEY, m_name);
    out.add(LAYER_TYPE_KEY, static_cast<int>(m_type));
    return out;
}

//------------------------------------------------------------------------------
// FeatureLayer
//------------------------------------------------------------------------------

FeatureLayer::FeatureLayer(const CPLString& name) : Layer(name, Type::Vector)
{
}


bool FeatureLayer::load(const JSONObject &store, ObjectContainer *objectContainer)
{
    if(!Layer::load(store, objectContainer))
        return false;

    const char* path = store.getString(LAYER_SOURCE_KEY, "");
    ObjectPtr fcObject;
    // Check absolute or relative catalog path
    if(nullptr == objectContainer) { // absolute path
        CatalogPtr catalog = Catalog::getInstance();
        fcObject = catalog->getObject(path);
    }
    else { // relative path
        fcObject = Catalog::fromRelativePath(path, objectContainer);
    }

    if(fcObject->getType() == ngsCatalogObjectType::CAT_CONTAINER_SIMPLE) {
        SimpleDataset * const simpleDS = ngsDynamicCast(SimpleDataset, fcObject);
        simpleDS->hasChildren();
        m_featureClass = std::dynamic_pointer_cast<FeatureClass>(
                    simpleDS->getInternalObject());
    }
    else {
        m_featureClass = std::dynamic_pointer_cast<FeatureClass>(fcObject);
    }

    if(m_featureClass)
        return true;
    return false;
}

JSONObject FeatureLayer::save(const ObjectContainer *objectContainer) const
{
    JSONObject out = Layer::save(objectContainer);
    ObjectContainer* parent = m_featureClass->getParent();
    // Check absolute or relative catalog path
    if(nullptr == objectContainer) { // absolute path
        if(parent->getType() == ngsCatalogObjectType::CAT_CONTAINER_SIMPLE) {
            out.add(LAYER_SOURCE_KEY, parent->getPath());
        }
        else {
            out.add(LAYER_SOURCE_KEY, m_featureClass->getPath());
        }
    }
    else { // relative path
        if(parent->getType() == ngsCatalogObjectType::CAT_CONTAINER_SIMPLE) {
            out.add(LAYER_SOURCE_KEY, Catalog::toRelativePath(parent,
                                                              objectContainer));
        }
        else {
            out.add(LAYER_SOURCE_KEY, Catalog::toRelativePath(
                        m_featureClass.get(), objectContainer));
        }
    }
    return out;
}

} // namespace ngs
