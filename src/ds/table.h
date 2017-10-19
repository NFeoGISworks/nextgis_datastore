/******************************************************************************
 * Project:  libngstore
 * Purpose:  NextGIS store and visualization support library
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
#ifndef NGSTABLE_H
#define NGSTABLE_H

// gdal
#include "ogrsf_frmts.h"

#include "catalog/object.h"
#include "ngstore/codes.h"

namespace ngs {

constexpr const char* SAVE_EDIT_HISTORY_KEY = "save_edit_history";

class FieldMapPtr : public std::shared_ptr<int>
{
public:
    explicit FieldMapPtr(unsigned long size);
    int &operator[](int key);
    const int &operator[](int key) const;
};

typedef struct _Field {
    char m_name[255];
    char m_originalName[255];
    char m_alias[1024];
    OGRFieldType m_type;
} Field;

class Table;

class FeaturePtr : public std::shared_ptr<OGRFeature>
{
public:
    FeaturePtr(OGRFeature* feature, Table* table = nullptr);
    FeaturePtr(OGRFeature* feature, const Table* table);
    FeaturePtr();
    FeaturePtr& operator=(OGRFeature* feature);
    operator OGRFeature*() const { return get(); }
    Table* table() const { return m_table; }
    void setTable(Table* table) { m_table = table; }
private:
    Table* m_table;
};

typedef std::shared_ptr<Table> TablePtr;

class Table : public Object
{
    friend class Dataset;
    friend class Folder;
public:
    typedef struct _attachmentInfo {
        GIntBig id;
        CPLString name;
        CPLString description;
        CPLString path;
        GIntBig size;
        GIntBig rid;
    } AttachmentInfo;
public:
    explicit Table(OGRLayer* layer,
          ObjectContainer* const parent = nullptr,
          const enum ngsCatalogObjectType type = CAT_TABLE_ANY,
          const CPLString& name = "");
    virtual ~Table();
    FeaturePtr createFeature() const;
    FeaturePtr getFeature(GIntBig id) const;
    virtual bool insertFeature(const FeaturePtr& feature);
    virtual bool updateFeature(const FeaturePtr& feature);
    virtual bool deleteFeature(GIntBig id);
    virtual bool deleteFeatures();
    GIntBig featureCount(bool force = false) const;
    void reset() const;
    void setAttributeFilter(const char* filter);
    FeaturePtr nextFeature() const;
    virtual int copyRows(const TablePtr srcTable,
                         const FieldMapPtr fieldMap,
                         const Progress& progress = Progress());
    const char* fidColumn() const;    
    const std::vector<Field>& fields();
    virtual GIntBig addAttachment(GIntBig fid, const char* fileName,
                          const char* description, const char* filePath,
                          char** options = nullptr);
    virtual bool deleteAttachment(GIntBig aid);
    virtual bool deleteAttachments(GIntBig fid);
    virtual bool updateAttachment(GIntBig aid, const char* fileName,
                          const char* description);
    virtual std::vector<AttachmentInfo> attachments(GIntBig fid);
    virtual bool setProperty(const char* key, const char* value, const char* domain);
    virtual CPLString property(const char* key, const char* defaultValue,
                                  const char* domain);
    virtual std::map<CPLString, CPLString> properties(const char* domain);
    virtual void deleteProperties();

    // Edit log
    virtual void deleteEditOperation(const ngsEditOperation& op);
    virtual std::vector<ngsEditOperation> editOperations() const;

    // Object interface
public:
    virtual bool canDestroy() const override;
    virtual bool destroy() override;
    virtual char** metadata(const char* domain) const override;
    virtual bool setMetadataItem(const char* name, const char* value,
                                 const char* domain) override {
        return setProperty(name, value, domain);
    }

protected:
    OGRFeatureDefn* definition() const;
    bool initAttachmentsTable();
    bool initEditHistoryTable();
    CPLString getAttachmentsPath() const;
    virtual void fillFields();
    virtual void addEditOperation(GIntBig fid, GIntBig aid, enum ngsChangeCode code);

protected:
    OGRLayer* m_layer;
    OGRLayer* m_attTable;
    OGRLayer* m_editHistoryTable;
    bool m_saveEditHistory;
    std::vector<Field> m_fields;
    CPLMutex* m_featureMutex;
};

}
#endif // NGSTABLE_H
