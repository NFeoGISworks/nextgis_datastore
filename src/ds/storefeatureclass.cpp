/******************************************************************************
 * Project:  libngstore
 * Purpose:  NextGIS store and visualization support library
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
#include "storefeatureclass.h"

#include "datastore.h"
#include "catalog/file.h"
#include "catalog/folder.h"

namespace ngs {

static std::map<CPLString, CPLString> propMapFromList(char** list)
{
    std::map<CPLString, CPLString> out;
    if(nullptr != list) {
        int i = 0;
        while (list[i] != nullptr) {
            const char* item = list[i];
            size_t len = CPLStrnlen(item, 1024);
            CPLString key, value;
            for(size_t j = 0; j < len; ++j) {
                if(item[j] == '=' || item[j] == ':' ) {
                    value = CPLString(item + 1 + j);
                    break;
                }
                key += item[j];
            }
            out[key] = value;
            i++;
        }
    }
    return out;
}

static FeaturePtr GetFeatureByRemoteId(GIntBig rid, const Table* table, OGRLayer* layer)
{
    Dataset* dataset = dynamic_cast<Dataset*>(table->parent());
    DatasetExecuteSQLLockHolder holder(dataset);
    layer->SetAttributeFilter(CPLSPrintf("%s = " CPL_FRMT_GIB, REMOTE_ID_KEY, rid));
    //    m_layer->ResetReading();
    OGRFeature* pFeature = layer->GetNextFeature();
    FeaturePtr out;
    if (nullptr != pFeature) {
        out = FeaturePtr(pFeature, table);
    }
    layer->SetAttributeFilter(nullptr);
    return out;
}

static bool SetFeatureAttachmentRemoteId(GIntBig aid, GIntBig rid, Table* table,
                                         OGRLayer* layer)
{
    Dataset* dataset = dynamic_cast<Dataset*>(table->parent());
    DatasetExecuteSQLLockHolder holder(dataset);
    FeaturePtr attFeature = layer->GetFeature(aid);
    if(!attFeature) {
        return false;
    }

    attFeature->SetField(REMOTE_ID_KEY, rid);
    return layer->SetFeature(attFeature) == OGRERR_NONE;
}

//------------------------------------------------------------------------------
// StoreTable
//------------------------------------------------------------------------------
StoreTable::StoreTable(OGRLayer* layer, ObjectContainer* const parent,
                       const CPLString& name) :
    Table(layer, parent, CAT_TABLE_GPKG, name)
{
}

FeaturePtr StoreTable::getFeatureByRemoteId(GIntBig rid) const
{
    return GetFeatureByRemoteId(rid, this, m_layer);
}

bool StoreTable::setFeatureAttachmentRemoteId(GIntBig aid, GIntBig rid)
{
    if(!initAttachmentsTable()) {
        return false;
    }

    return SetFeatureAttachmentRemoteId(aid, rid, this, m_attTable);
}

void StoreTable::setRemoteId(FeaturePtr feature, GIntBig rid)
{
    feature->SetField(feature->GetFieldIndex(REMOTE_ID_KEY), rid);
}

GIntBig StoreTable::getRemoteId(FeaturePtr feature)
{
    return feature->GetFieldAsInteger64(feature->GetFieldIndex(REMOTE_ID_KEY));
}

void StoreTable::fillFields()
{
    Table::fillFields();
    // Hide remote id field from user
    if(EQUAL(m_fields.back().m_name, REMOTE_ID_KEY)) {
        m_fields.pop_back();
    }
}

std::vector<Table::AttachmentInfo> StoreTable::attachments(GIntBig fid)
{
    std::vector<AttachmentInfo> out;

    if(!initAttachmentsTable()) {
        return out;
    }

    Dataset* dataset = dynamic_cast<Dataset*>(m_parent);
    if(nullptr == dataset) {
        dataset->lockExecuteSql(true);
    }

    m_attTable->SetAttributeFilter(CPLSPrintf("%s = " CPL_FRMT_GIB,
                                              ATTACH_FEATURE_ID_FIELD, fid));
    m_attTable->ResetReading();
    FeaturePtr attFeature;
    while((attFeature = m_attTable->GetNextFeature())) {
        AttachmentInfo info;
        info.name = attFeature->GetFieldAsString(ATTACH_FILE_NAME_FIELD);
        info.description = attFeature->GetFieldAsString(ATTACH_DESCRIPTION_FIELD);
        info.id = attFeature->GetFID();
        info.rid = attFeature->GetFieldAsInteger64(REMOTE_ID_KEY);

        CPLString attFeaturePath = CPLFormFilename(getAttachmentsPath(),
                                                   CPLSPrintf(CPL_FRMT_GIB, fid),
                                                   nullptr);
        info.path = CPLFormFilename(attFeaturePath,
                                    CPLSPrintf(CPL_FRMT_GIB, info.id),
                                    nullptr);

        info.size = File::fileSize(info.path);

        out.push_back(info);
    }

    if(nullptr == dataset) {
        dataset->lockExecuteSql(false);
    }
    return out;
}

GIntBig StoreTable::addAttachment(GIntBig fid, const char* fileName,
                             const char* description, const char* filePath,
                             char** options)
{
    if(!initAttachmentsTable()) {
        return NOT_FOUND;
    }
    bool move = CPLFetchBool(options, "MOVE", false);
    GIntBig rid = atoll(CSLFetchNameValueDef(options, "RID", "-1"));

    FeaturePtr newAttachment = OGRFeature::CreateFeature(
                m_attTable->GetLayerDefn());

    newAttachment->SetField(ATTACH_FEATURE_ID_FIELD, fid);
    newAttachment->SetField(ATTACH_FILE_NAME_FIELD, fileName);
    newAttachment->SetField(ATTACH_DESCRIPTION_FIELD, description);
    newAttachment->SetField(REMOTE_ID_KEY, rid);

    if(m_attTable->CreateFeature(newAttachment) == OGRERR_NONE) {
        CPLString dstTablePath = getAttachmentsPath();
        if(!Folder::isExists(dstTablePath)) {
            Folder::mkDir(dstTablePath);
        }
        CPLString dstFeaturePath = CPLFormFilename(dstTablePath,
                                                   CPLSPrintf(CPL_FRMT_GIB, fid),
                                                   nullptr);
        if(!Folder::isExists(dstFeaturePath)) {
            Folder::mkDir(dstFeaturePath);
        }

        CPLString dstPath = CPLFormFilename(dstFeaturePath,
                                            CPLSPrintf(CPL_FRMT_GIB,
                                                       newAttachment->GetFID()),
                                            nullptr);
        if(Folder::isExists(filePath)) {
            if(move) {
                File::moveFile(filePath, dstPath);
            }
            else {
                File::copyFile(filePath, dstPath);
            }
        }
        return newAttachment->GetFID();
    }

    return NOT_FOUND;
}

bool StoreTable::setProperty(const char* key, const char* value,
                                    const char* domain)
{
    return m_layer->SetMetadataItem(key, value, domain) == OGRERR_NONE;
}

CPLString StoreTable::property(const char* key, const char* defaultValue,
                                         const char* domain)
{
    const char* item = m_layer->GetMetadataItem(key, domain);
    return item != nullptr ? item : defaultValue;
}

std::map<CPLString, CPLString> StoreTable::properties(const char* domain)
{
    return propMapFromList(m_layer->GetMetadata(domain));
}

void StoreTable::deleteProperties()
{
    m_layer->SetMetadata(nullptr, nullptr);
}

//------------------------------------------------------------------------------
// StoreFeatureClass
//------------------------------------------------------------------------------

StoreFeatureClass::StoreFeatureClass(OGRLayer* layer,
                                     ObjectContainer* const parent,
                                     const CPLString& name) :
    FeatureClass(layer, parent, CAT_FC_GPKG, name)
{
    if(m_zoomLevels.empty()) {
        fillZoomLevels();
    }
}

FeaturePtr StoreFeatureClass::getFeatureByRemoteId(GIntBig rid) const
{
    return GetFeatureByRemoteId(rid, this, m_layer);
}

bool StoreFeatureClass::setFeatureAttachmentRemoteId(GIntBig aid, GIntBig rid)
{
    if(!initAttachmentsTable()) {
        return false;
    }

    return SetFeatureAttachmentRemoteId(aid, rid, this, m_attTable);
}

void StoreFeatureClass::fillFields()
{
    Table::fillFields();
    // Hide remote id field from user
    if(EQUAL(m_fields.back().m_name, REMOTE_ID_KEY)) {
        m_fields.pop_back();
    }
}

std::vector<Table::AttachmentInfo> StoreFeatureClass::attachments(GIntBig fid)
{
    std::vector<AttachmentInfo> out;

    if(!initAttachmentsTable()) {
        return out;
    }

    Dataset* dataset = dynamic_cast<Dataset*>(m_parent);
    if(nullptr == dataset) {
        dataset->lockExecuteSql(true);
    }

    m_attTable->SetAttributeFilter(CPLSPrintf("%s = " CPL_FRMT_GIB,
                                              ATTACH_FEATURE_ID_FIELD, fid));
    m_attTable->ResetReading();
    FeaturePtr attFeature;
    while((attFeature = m_attTable->GetNextFeature())) {
        AttachmentInfo info;
        info.name = attFeature->GetFieldAsString(ATTACH_FILE_NAME_FIELD);
        info.description = attFeature->GetFieldAsString(ATTACH_DESCRIPTION_FIELD);
        info.id = attFeature->GetFID();
        info.rid = attFeature->GetFieldAsInteger64(REMOTE_ID_KEY);

        CPLString attFeaturePath = CPLFormFilename(getAttachmentsPath(),
                                                   CPLSPrintf(CPL_FRMT_GIB, fid),
                                                   nullptr);
        info.path = CPLFormFilename(attFeaturePath,
                                    CPLSPrintf(CPL_FRMT_GIB, info.id),
                                    nullptr);

        info.size = File::fileSize(info.path);

        out.push_back(info);
    }

    if(nullptr == dataset) {
        dataset->lockExecuteSql(false);
    }
    return out;
}

GIntBig StoreFeatureClass::addAttachment(GIntBig fid, const char* fileName,
                             const char* description, const char* filePath,
                             char** options)
{
    if(!initAttachmentsTable()) {
        return NOT_FOUND;
    }
    bool move = CPLFetchBool(options, "MOVE", false);
    GIntBig rid = atoll(CSLFetchNameValueDef(options, "RID", "-1"));

    FeaturePtr newAttachment = OGRFeature::CreateFeature(
                m_attTable->GetLayerDefn());

    newAttachment->SetField(ATTACH_FEATURE_ID_FIELD, fid);
    newAttachment->SetField(ATTACH_FILE_NAME_FIELD, fileName);
    newAttachment->SetField(ATTACH_DESCRIPTION_FIELD, description);
    newAttachment->SetField(REMOTE_ID_KEY, rid);

    if(m_attTable->CreateFeature(newAttachment) == OGRERR_NONE) {
        CPLString dstTablePath = getAttachmentsPath();
        if(!Folder::isExists(dstTablePath)) {
            Folder::mkDir(dstTablePath);
        }
        CPLString dstFeaturePath = CPLFormFilename(dstTablePath,
                                                   CPLSPrintf(CPL_FRMT_GIB, fid),
                                                   nullptr);
        if(!Folder::isExists(dstFeaturePath)) {
            Folder::mkDir(dstFeaturePath);
        }

        CPLString dstPath = CPLFormFilename(dstFeaturePath,
                                            CPLSPrintf(CPL_FRMT_GIB,
                                                       newAttachment->GetFID()),
                                            nullptr);
        if(Folder::isExists(filePath)) {
            if(move) {
                File::moveFile(filePath, dstPath);
            }
            else {
                File::copyFile(filePath, dstPath);
            }
        }
        return newAttachment->GetFID();
    }

    return NOT_FOUND;
}

bool StoreFeatureClass::setProperty(const char* key, const char* value,
                                    const char* domain)
{
    return m_layer->SetMetadataItem(key, value, domain) == OGRERR_NONE;
}

CPLString StoreFeatureClass::property(const char* key, const char* defaultValue,
                                         const char* domain)
{
    const char* item = m_layer->GetMetadataItem(key, domain);
    return item != nullptr ? item : defaultValue;
}

std::map<CPLString, CPLString> StoreFeatureClass::properties(const char* domain)
{
    return propMapFromList(m_layer->GetMetadata(domain));
}

void StoreFeatureClass::deleteProperties()
{
    m_layer->SetMetadata(nullptr, nullptr);
}

} // namespace ngs

