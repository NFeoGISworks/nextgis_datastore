/******************************************************************************
 * Project:  libngstore
 * Purpose:  NextGIS store and visualisation support library
 * Author: Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
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
#include "table.h"

#include "api_priv.h"
#include "dataset.h"
#include "ngstore/api.h"
#include "util/error.h"
#include "util/notify.h"

namespace ngs {

//------------------------------------------------------------------------------
// FieldMapPtr
//-------------------------------------------------

FieldMapPtr::FieldMapPtr(unsigned long size) :
    shared_ptr(static_cast<int*>(CPLMalloc(sizeof(int) * size)), CPLFree)
{

}

int &FieldMapPtr::operator[](int key)
{
    return get()[key];
}

const int &FieldMapPtr::operator[](int key) const
{
    return get()[key];
}

//------------------------------------------------------------------------------
// FeaturePtr
//------------------------------------------------------------------------------

FeaturePtr::FeaturePtr(OGRFeature* feature) :
    shared_ptr( feature, OGRFeature::DestroyFeature )
{

}

FeaturePtr:: FeaturePtr() :
    shared_ptr( nullptr, OGRFeature::DestroyFeature )
{

}

FeaturePtr& FeaturePtr::operator=(OGRFeature* feature) {
    reset(feature);
    return *this;
}

//------------------------------------------------------------------------------
// Table
//------------------------------------------------------------------------------

Table::Table(OGRLayer *layer,
             ObjectContainer * const parent,
             const enum ngsCatalogObjectType type,
             const CPLString &name) :
    Object(parent, type, name, ""), m_layer(layer)
{
    fillFields();
}

Table::~Table()
{
    if(m_type == CAT_QUERY_RESULT || m_type == CAT_QUERY_RESULT_FC) {
        Dataset* const dataset = dynamic_cast<Dataset*>(m_parent);
        if(nullptr != dataset) {
            GDALDataset * const DS = dataset->getGDALDataset();
            if(nullptr != DS) {
                DS->ReleaseResultSet(m_layer);
            }
        }
    }
}

FeaturePtr Table::createFeature() const
{
    if(nullptr == m_layer)
        return FeaturePtr();

    OGRFeature* pFeature = OGRFeature::CreateFeature( m_layer->GetLayerDefn() );
    if (nullptr == pFeature)
        return FeaturePtr();

    return FeaturePtr(pFeature);
}

FeaturePtr Table::getFeature(GIntBig id) const
{
    if(nullptr == m_layer)
        return FeaturePtr();

    OGRFeature* pFeature = m_layer->GetFeature(id);
    if (nullptr == pFeature)
        return FeaturePtr();

    return FeaturePtr(pFeature);
}

bool Table::insertFeature(const FeaturePtr &feature)
{
   if(nullptr == m_layer)
        return false;

    if(m_layer->CreateFeature(feature) == OGRERR_NONE) {
        Notify::instance().onNotify(fullName(),
                                    ngsChangeCode::CC_CREATE_FEATURE);
        return true;
    }

    return false;
}

bool Table::updateFeature(const FeaturePtr &feature)
{
    if(nullptr == m_layer)
        return false;

    if(m_layer->SetFeature(feature) == OGRERR_NONE) {
        Notify::instance().onNotify(fullName(),
                                    ngsChangeCode::CC_CHANGE_FEATURE);
        return true;
    }

    return false;
}

bool Table::deleteFeature(GIntBig id)
{
    if(nullptr == m_layer)
        return false;

    if(m_layer->DeleteFeature(id) == OGRERR_NONE) {
        Notify::instance().onNotify(fullName(),
                                    ngsChangeCode::CC_DELETE_FEATURE);
        return true;
    }

    return false;
}

GIntBig Table::featureCount(bool force) const
{
    if(nullptr == m_layer)
        return 0;

    return m_layer->GetFeatureCount(force ? TRUE : FALSE);
}

void Table::reset() const
{
    if(nullptr != m_layer)
        m_layer->ResetReading();
}

FeaturePtr Table::nextFeature() const
{
    if(nullptr == m_layer)
        return FeaturePtr();
    return m_layer->GetNextFeature();
}

int Table::copyRows(const TablePtr srcTable, const FieldMapPtr fieldMap,
                     const Progress& progress)
{
    if(!srcTable) {
        return errorMessage(COD_COPY_FAILED, _("Source table is invalid"));
    }

    progress.onProgress(COD_IN_PROCESS, 0.0,
                       _("Start copy records from '%s' to '%s'"),
                       srcTable->name().c_str(), m_name.c_str());

    GIntBig featureCount = srcTable->featureCount();
    double counter = 0;
    srcTable->reset();
    FeaturePtr feature;
    while((feature = srcTable->nextFeature ())) {
        double complete = counter / featureCount;
        if(!progress.onProgress(COD_IN_PROCESS, complete,
                           _("Copy in process ..."))) {
            return  COD_CANCELED;
        }

        FeaturePtr dstFeature = createFeature();
        dstFeature->SetFieldsFrom(feature, fieldMap.get());

        if(!insertFeature(dstFeature)) {
            if(!progress.onProgress(COD_WARNING, complete,
                               _("Create feature failed. Source feature FID:%lld"),
                               feature->GetFID ())) {
               return  COD_CANCELED;
            }
        }
        counter++;
    }

    progress.onProgress(COD_FINISHED, 1.0, _("Done. Copied %d rows"),
                       int(counter));

    return COD_SUCCESS;
}

const char* Table::fidColumn() const
{
    if(nullptr == m_layer)
        return "";
    return m_layer->GetFIDColumn();
}

char**Table::getMetadata(const char* domain) const
{
    if(nullptr == m_layer)
        return nullptr;
    return m_layer->GetMetadata(domain);
}

bool Table::destroy()
{
    Dataset* const dataset = dynamic_cast<Dataset*>(m_parent);
    if(nullptr == dataset) {
        return errorMessage(_("Parent is not dataset"));
    }
    CPLString name = fullName();
    if(dataset->destroyTable(this)) {
        Notify::instance().onNotify(name, ngsChangeCode::CC_DELETE_OBJECT);
        return true;
    }
    return false;
}

OGRFeatureDefn*Table::definition() const
{
    if(nullptr == m_layer)
        return nullptr;
    return m_layer->GetLayerDefn();
}

void Table::fillFields()
{
    m_fields.clear();
    if(nullptr != m_layer) {
        Dataset* parentDataset = dynamic_cast<Dataset*>(m_parent);
        OGRFeatureDefn* defn = m_layer->GetLayerDefn();
        if(nullptr == defn || nullptr == parentDataset) {
            return;
        }

        auto properties = parentDataset->getProperties(m_name);

        for(int i = 0; i < defn->GetFieldCount(); ++i) {
            OGRFieldDefn* fieldDefn = defn->GetFieldDefn(i);
            Field fieldDesc;
            fieldDesc.m_type = fieldDefn->GetType();
            fieldDesc.m_name = fieldDefn->GetNameRef();

            fieldDesc.m_alias = properties[CPLSPrintf("FIELD_%d_ALIAS", i)];
            fieldDesc.m_originalName = properties[CPLSPrintf("FIELD_%d_NAME", i)];

            m_fields.push_back(fieldDesc);
        }

        // Fill metadata
        for(auto it = properties.begin(); it != properties.end(); ++it) {
            if(EQUALN(it->first, "FIELD_", 6)) {
                continue;
            }
            m_layer->SetMetadataItem(it->first, it->second, KEY_USER);
        }
    }
}

} // namespace ngs
