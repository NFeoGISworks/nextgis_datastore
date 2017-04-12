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
#ifndef NGSRASTERDATASET_H
#define NGSRASTERDATASET_H

#include "catalog/object.h"
#include "coordinatetransformation.h"

namespace ngs {

/**
 * @brief The Raster dataset class represent image or raster
 */
class Raster : public Object, public ISpatialDataset
{
public:
    Raster(ObjectContainer * const parent = nullptr,
           const enum ngsCatalogObjectType type = ngsCatalogObjectType::CAT_RASTER_ANY,
           const CPLString & name = "",
           const CPLString & path = "");
    virtual ~Raster();

    // ISpatialDataset interface
public:
    virtual OGRSpatialReference *getSpatialReference() const override;

protected:
    OGRSpatialReference m_spatialReference;
    GDALDataset* m_DS;

};

}

#endif // NGSRASTERDATASET_H
