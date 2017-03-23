/******************************************************************************
 * Project: libngstore
 * Purpose: NextGIS store and visualization support library
 * Author:  Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
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
#ifndef NGSFILTER_H
#define NGSFILTER_H

#include "catalog/object.h"

#include <vector>

namespace ngs {

/**
 * @brief The simple catalog filter class
 */
class Filter
{
public:
    Filter(const ngsCatalogObjectType type = CAT_UNKNOWN);
    virtual ~Filter() = default;
    virtual bool canDisplay(ObjectPtr object) const;

public:
    static bool isFeatureClass(const ngsCatalogObjectType type);
    static bool isRaster(const ngsCatalogObjectType type);
    static bool isTable(const ngsCatalogObjectType type);
    static bool isContainer(const ngsCatalogObjectType type);
protected:
    enum ngsCatalogObjectType type;
};

class MultiFilter : public Filter
{
public:
    MultiFilter();
    virtual ~MultiFilter() = default;
    virtual bool canDisplay(ObjectPtr object) const override;
    void addType(enum ngsCatalogObjectType newType);

protected:
    std::vector< enum ngsCatalogObjectType > types;
};

}

#endif // NGSFILTER_H
