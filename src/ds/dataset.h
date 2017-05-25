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
#ifndef NGSDATASET_H
#define NGSDATASET_H

#include <memory>

#include "api_priv.h"
#include "geometry.h"
#include "featureclass.h"
#include "catalog/objectcontainer.h"

namespace ngs {

/**
 * @brief The wrapper class around GDALDataset pointer
 */
class GDALDatasetPtr : public std::shared_ptr<GDALDataset>
{
public:
    GDALDatasetPtr(GDALDataset* ds);
    GDALDatasetPtr();
    GDALDatasetPtr(const GDALDatasetPtr& ds);
    GDALDatasetPtr& operator=(GDALDataset* ds);
    operator GDALDataset*() const;
};

/**
 * @brief The DatasetBase class
 */
class DatasetBase
{
public:
    DatasetBase();
    virtual ~DatasetBase();
    virtual const char* getOptions(enum ngsOptionType optionType) const = 0;
    virtual GDALDataset * getGDALDataset() const { return m_DS; }
    virtual bool open(unsigned int openFlags, const Options &options = Options()) = 0;
    // is checks
    virtual bool isOpened() const { return m_DS != nullptr; }
    virtual bool isReadOnly() const;

protected:
    bool open(const char* path, unsigned int openFlags,
              const Options &options = Options());
    const char* getOptions(const enum ngsCatalogObjectType type,
                           enum ngsOptionType optionType) const;

protected:
    GDALDataset* m_DS;
};

/**
 * @brief The Dataset class is base class of DataStore. Each table, raster,
 * feature class, etc. are Dataset. The DataStore is an array of Datasets as
 * Map is array of Layers.
 */
class Dataset : public ObjectContainer, public DatasetBase
{
    friend class FeatureClass;
public:
    Dataset(ObjectContainer * const parent = nullptr,
            const enum ngsCatalogObjectType type = ngsCatalogObjectType::CAT_CONTAINER_ANY,
            const CPLString & name = "",
            const CPLString & path = "");
    virtual ~Dataset();
    virtual const char* getOptions(enum ngsOptionType optionType) const override;

    TablePtr executeSQL(const char* statement, const char* dialect = "");
    TablePtr executeSQL(const char* statement,
                            GeometryPtr spatialFilter,
                            const char* dialect = "");


    virtual bool open(unsigned int openFlags,
                      const Options &options = Options()) override;
    virtual FeatureClass* createFeatureClass(const CPLString &name,
                                             OGRFeatureDefn* const definition,
                                             const OGRSpatialReference *spatialRef,
                                             OGRwkbGeometryType type,
                                             const Options& options = Options(),
                                             const Progress &progress = Progress());
    virtual Table* createTable(const CPLString &name,
                               OGRFeatureDefn* const definition,
                               const Options& options = Options(),
                               const Progress &progress = Progress());
    // Object interface
public:
    virtual bool destroy() override;
    virtual bool canDestroy() const override { return !isReadOnly(); }

    // ObjectContainer interface
public:
    virtual bool hasChildren() override;
    virtual int paste(ObjectPtr child, bool move = false,
                      const Options & options = Options(),
                      const Progress &progress = Progress()) override;
    virtual bool canPaste(const enum ngsCatalogObjectType type) const override;
    virtual bool canCreate(const enum ngsCatalogObjectType type) const override;

protected:
    virtual bool isNameValid(const char *name) const;
    virtual CPLString normalizeDatasetName(const CPLString& name) const;
    virtual CPLString normalizeFieldName(const CPLString& name) const;
    virtual void fillFeatureClasses();
    GDALDataset* getOverviewDataset() const { return m_ovrDS; }
    GDALDataset* createOverviewDataset();

protected:
    GDALDataset* m_ovrDS;
};

}

#endif // NGSDATASET_H
