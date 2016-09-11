/******************************************************************************
*  Project: NextGIS ...
*  Purpose: Application for ...
*  Author:  Dmitry Baryshnikov, bishop.dev@gmail.com
*******************************************************************************
*  Copyright (C) 2012-2016 NextGIS, info@nextgis.ru
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 2 of the License, or
*   (at your option) any later version.
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/
#include "storefeaturedataset.h"
#include "maputil.h"
#include "geometryutil.h"

using namespace ngs;

StoreFeatureDataset::StoreFeatureDataset(OGRLayer * const layer) :
    FeatureDataset(layer)
{
    m_type = ngsDatasetType(Store) | ngsDatasetType(Featureset);
}

int StoreFeatureDataset::copyFeatures(const FeatureDataset *srcDataset,
                                      const FieldMapPtr fieldMap,
                                      OGRwkbGeometryType filterGeomType,
                                      unsigned int skipGeometryFlags,
                                      unsigned int taskId,
                                      ngsProgressFunc progressFunc,
                                      void *progressArguments)
{
    if(progressFunc)
        progressFunc(taskId, 0, CPLSPrintf ("Start copy features from '%s' to '%s'",
                                    srcDataset->name ().c_str (), name().c_str ()),
                     progressArguments);

    OGRSpatialReference *srcSRS = srcDataset->getSpatialReference();
    OGRSpatialReference *dstSRS = getSpatialReference();
    CoordinateTransformationPtr CT (srcSRS, dstSRS);
    GIntBig featureCount = srcDataset->featureCount();
    OGRwkbGeometryType dstGeomType = getGeometryType();
    double counter = 0;
    srcDataset->reset ();
    FeaturePtr feature;

    while((feature = srcDataset->nextFeature ()) != nullptr) {
        if(progressFunc)
            progressFunc(taskId, counter / featureCount, "copying...",
                         progressArguments);

        OGRGeometry * geom = feature->GetGeometryRef ();
        OGRGeometry *newGeom = nullptr;
        if(nullptr == geom && ngsFeatureLoadSkipType(EmptyGeometry))
            continue;

        if(nullptr != geom) {
            if((skipGeometryFlags & ngsFeatureLoadSkipType(EmptyGeometry)) &&
                    geom->IsEmpty ())
                continue;
            if((skipGeometryFlags & ngsFeatureLoadSkipType(InvalidGeometry)) &&
                    !geom->IsValid())
                continue;


            OGRwkbGeometryType geomType = geom->getGeometryType ();
            OGRwkbGeometryType nonMultiGeomType = geomType;
            if (OGR_GT_Flatten(geomType) > wkbPolygon &&
                    OGR_GT_Flatten(geomType) < wkbGeometryCollection) {
                nonMultiGeomType = static_cast<OGRwkbGeometryType>(geomType - 3);
            }
            if (filterGeomType != wkbUnknown && filterGeomType != nonMultiGeomType) {
                continue;
            }

            if (dstGeomType != geomType) {
                newGeom = OGRGeometryFactory::forceTo(geom, dstGeomType);
            }
            else {
                newGeom = geom->clone ();
            }

            CT.transform(newGeom);
        }

        FeaturePtr dstFeature = createFeature ();
        if(nullptr != newGeom) {
            dstFeature->SetGeometryDirectly (newGeom);
/* TODO: Create tiled geometries in separate ogrlayer
            // create geometries for zoom levels
            if(OGR_GT_Flatten(newGeom->getGeometryType ()) != wkbPoint &&
               OGR_GT_Flatten(newGeom->getGeometryType ()) != wkbMultiPoint) {
                for(auto sampleDist : gSampleDists) {
                    GeometryUPtr simpleGeom(simplifyGeometry (newGeom,
                                                                sampleDist.first));
                    if(nullptr != simpleGeom) {
                        int blobLen = simpleGeom->WkbSize();
                        GByte* geomBlob = (GByte*) VSI_MALLOC_VERBOSE(blobLen);
                        if( geomBlob != nullptr && simpleGeom->exportToWkb(
                                    wkbNDR, geomBlob) == OGRERR_NONE )
                            dstFeature->SetField (dstFeature->GetFieldIndex(
                                CPLSPrintf ("ngs_geom_%d", sampleDist.second)),
                                blobLen, geomBlob);
                        CPLFree(geomBlob);
                    }
                }
            }
*/
        }
        dstFeature->SetFieldsFrom (feature, fieldMap.get());

        if(insertFeature(dstFeature) != ngsErrorCodes::EC_SUCCESS) {
            CPLError(CE_Warning, CPLE_AppDefined, "Create feature failed. "
                     "Source feature FID:%lld", feature->GetFID ());
        }
        counter++;
    }
    if(progressFunc)
        progressFunc(taskId, 1.0, CPLSPrintf ("Done. Copied %d features",
                                              int(counter)), progressArguments);

    return ngsErrorCodes::EC_SUCCESS;
}

/* TODO: createDataset() with history also add StoreFeatureDataset to override copyRows function
// 4. for each feature
// 4.1. read
// 4.2. create samples for several scales
// 4.3. create feature in storage dataset
// 4.4. create mapping of fields and original spatial reference metadata
*/
