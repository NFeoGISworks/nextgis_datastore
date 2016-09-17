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
#ifndef DATASETCONTAINER_H
#define DATASETCONTAINER_H

#include "dataset.h"

namespace ngs {

void LoadingThread(void * store);

typedef struct _loadData {
    CPLString path;
    CPLString srcSubDatasetName;
    CPLString dstDatasetName;
    CPLString dstDatasetNewName;
    bool move;
    unsigned int skipType;
    ngsProgressFunc progressFunc;
    void *progressArguments;
    enum ngsErrorCodes status;
} LoadData;

class DatasetContainer : public Dataset
{
    friend void LoadingThread(void * store);
public:
    DatasetContainer();
    virtual ~DatasetContainer();
    virtual int datasetCount() const;
    virtual int rasterCount() const;
    virtual DatasetPtr getDataset(const CPLString& name);
    virtual DatasetPtr getDataset(int index);
    // TODO: getRaster
    unsigned int loadDataset(const CPLString& name, const CPLString& path,
                    const CPLString& subDatasetName, bool move,
                    unsigned int skipType, ngsProgressFunc progressFunc,
                    void* progressArguments = nullptr);
    /* TODO: does this need here?
    bool canCopy(const CPLString &destPath);
    bool canMove(const CPLString &destPath);
    */
    /* TODO: release this
    int move(const CPLString &destPath, ngsProgressFunc progressFunc = nullptr,
             void* progressArguments = nullptr);
    int copy(const CPLString &destPath, ngsProgressFunc progressFunc = nullptr,
             void* progressArguments = nullptr);
    */
    virtual int copyDataset(DatasetPtr srcDataset, CPLString &dstDatasetName,
                            unsigned int skipGeometryFlags, unsigned int taskId,
                            ngsProgressFunc progressFunc = nullptr,
                            void* progressArguments = nullptr);
    virtual int moveDataset(DatasetPtr srcDataset, CPLString& dstDatasetName,
                            unsigned int skipGeometryFlags,
                            unsigned int taskId = 0,
                            ngsProgressFunc progressFunc = nullptr,
                            void* progressArguments = nullptr);
    virtual DatasetPtr createDataset(const CPLString &name,
                                     OGRFeatureDefn* const definition,
                                     char** options = nullptr,
                                     unsigned int taskId = 0,
                                     ngsProgressFunc progressFunc = nullptr,
                                     void* progressArguments = nullptr);
    virtual DatasetPtr createDataset(const CPLString &name,
                                     OGRFeatureDefn* const definition,
                                     const OGRSpatialReference *spatialRef,
                                     OGRwkbGeometryType type = wkbUnknown,
                                     char** options = nullptr,
                                     unsigned int taskId = 0,
                                     ngsProgressFunc progressFunc = nullptr,
                                     void* progressArguments = nullptr);
    ngsLoadTaskInfo getLoadTaskInfo (unsigned int taskId) const;
    // TODO: createRaster()
protected:
    virtual bool isNameValid(const CPLString& name) const;
    virtual CPLString normalizeFieldName(const CPLString& name) const;
    bool isDatabase() const;
    vector<OGRwkbGeometryType> getGeometryTypes(DatasetPtr srcDataset);
protected:
    map<string, DatasetPtr> m_datasets;
    /**
     * Load Dataset
     */
    CPLJoinableThread* m_hLoadThread;
    bool m_cancelLoad;
    vector<LoadData> m_loadData;

    // Dataset interface
public:
    virtual int destroy(ngsProgressFunc progressFunc, void *progressArguments) override;
};

typedef shared_ptr<DatasetContainer> DatasetContainerPtr;
}

#endif // DATASETCONTAINER_H
