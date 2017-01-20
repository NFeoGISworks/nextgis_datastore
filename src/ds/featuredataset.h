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
#ifndef NGSFEATUREDATASET_H
#define NGSFEATUREDATASET_H

#include "spatialdataset.h"
#include "table.h"

// gdal
#include "ogr_spatialref.h"

#define ngsFeatureLoadSkipType(x) static_cast<unsigned int>( ngs::FeatureDataset::SkipType::x )

namespace ngs {

typedef std::shared_ptr< OGRGeometry > GeometryPtr;
typedef std::unique_ptr< OGRGeometry > GeometryUPtr;

class CoordinateTransformationPtr
{
public:
    CoordinateTransformationPtr( OGRSpatialReference *srcSRS,
                                 OGRSpatialReference *dstSRS);
    ~CoordinateTransformationPtr();
    bool transform(OGRGeometry *geom);
protected:
    // no copy constructor
    CoordinateTransformationPtr(const CoordinateTransformationPtr& /*other*/) {}
protected:
    OGRCoordinateTransformation* m_oCT;
};

class FeatureDataset : public Table, public SpatialDataset
{
public:
    enum class SkipType : unsigned int {
        NoSkip          = 0x0000,
        EmptyGeometry   = 0x0001,
        InvalidGeometry = 0x0002
    };

    enum class GeometryReportType {
        Full,
        Ogc,
        Simple
    };

public:
    FeatureDataset(OGRLayer * const layer);
    virtual OGRSpatialReference *getSpatialReference() const override;
    OGRwkbGeometryType getGeometryType() const;
    virtual int copyFeatures(const FeatureDataset *srcDataset,
                             const FieldMapPtr fieldMap,
                             OGRwkbGeometryType filterGeomType,
                             ProgressInfo* progressInfo);
    bool setIgnoredFields(const char **fields);
    std::vector<CPLString> getGeometryColumns() const;
    CPLString getGeometryColumn() const;
    ResultSetPtr executeSQL(const CPLString& statement,
                            GeometryPtr spatialFilter,
                            const CPLString& dialect = "") const;
    ResultSetPtr getGeometries(unsigned char zoom,
                            GeometryPtr spatialFilter) const;

    // static
    static CPLString getGeometryTypeName(OGRwkbGeometryType type,
                                         enum GeometryReportType reportType);

};

}

#endif // NGSFEATUREDATASET_H
