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
#ifndef MAPSTORE_H
#define MAPSTORE_H

#include "datastore.h"
#include "map.h"
#include "api.h"

namespace ngs {


/**
 * @brief The MapStore class store maps with layers connected to datastore tables
 *        and styles
 */
class MapStore
{
    friend class Map;
public:
    MapStore(const DataStorePtr &dataStore);
    ~MapStore();
    /**
     * @brief create default map
     * @return ngsErrorCodes value - SUCCES if everything is OK
     */
    int create();
    /**
     * @brief inform about map count in storage
     * @return map count
     */
    GIntBig mapCount() const;
    MapWPtr getMap(const char* name);
    MapWPtr getMap(int index);
    int initMap(const char *name, void *buffer, int width, int height);
    int drawMap(const char *name, ngsProgressFunc progressFunc,
                void* progressArguments = nullptr);
    void onLowMemory();
    void setNotifyFunc(ngsNotifyFunc notifyFunc);
    void unsetNotifyFunc();
    ngsRGBA getMapBackgroundColor(const char *name);
    int setMapBackgroundColor(const char *name, const ngsRGBA& color);

protected:
    int storeMap(Map* map);
    bool isNameValid(const string& name) const;
    int destroyMap(GIntBig mapId);
    const Table* getLayersTable() const;

protected:
    DataStorePtr m_datastore;
    map<string, MapPtr> m_maps;
    ngsNotifyFunc m_notifyFunc;
};

typedef shared_ptr<MapStore> MapStorePtr;

}
#endif // MAPSTORE_H
