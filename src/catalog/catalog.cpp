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
#include "catalog.h"

// gdal
#include "cpl_conv.h"
#include "api_priv.h"

#include "folder.h"
#include "localconnections.h"

#include "ngstore/common.h"
#include "util/settings.h"
#include "util/stringutil.h"

// factories
#include "factories/datastorefactory.h"
#include "factories/folderfactory.h"
#include "factories/filefactory.h"

namespace ngs {

static CatalogPtr gCatalog;

constexpr const char * CONNECTIONS_DIR = "connections";
constexpr const char * CATALOG_PREFIX = "ngc:/";
constexpr const char * CATALOG_PREFIX_FULL = "ngc://";
constexpr int CATALOG_PREFIX_LEN = length(CATALOG_PREFIX_FULL);

Catalog::Catalog() : ObjectContainer(nullptr, CAT_CONTAINER_ROOT, _("Catalog"))
{
    m_showHidden = Settings::instance().getBool("catalog/show_hidden", true);
}

CPLString Catalog::getFullName() const
{
    return CATALOG_PREFIX;
}

ObjectPtr Catalog::getObject(const char *path)
{
    if(EQUAL(path, CATALOG_PREFIX_FULL))
        return std::static_pointer_cast<Object>(gCatalog);

    // Skip prefix ngc://
    path += CATALOG_PREFIX_LEN;
    return ObjectContainer::getObject(path);
}

ObjectPtr Catalog::getObjectByLocalPath(const char *path)
{
    if(!hasChildren())
        return ObjectPtr();

    // Find LocalConnections
    LocalConnections* localConnections = nullptr;
    for(const ObjectPtr &rootObject : m_children) {
        if(rootObject->getType() == CAT_CONTAINER_LOCALCONNECTION) {
            localConnections = ngsDynamicCast(LocalConnections, rootObject);
            break;
        }
    }

    if(nullptr == localConnections || !localConnections->hasChildren())
        return ObjectPtr();

    return localConnections->getObjectByLocalPath(path);
}

void Catalog::freeResources()
{
    for(ObjectPtr &child : m_children) {
        ObjectContainer * const container = ngsDynamicCast(ObjectContainer,
                                                           child);
        if(nullptr != container) {
            container->clear();
        }
    }
}

void Catalog::createObjects(ObjectPtr object, std::vector<const char *> names)
{
    if(names.empty()) {
        return;
    }

    ObjectContainer* const container = ngsDynamicCast(ObjectContainer, object);
    if(nullptr == container) {
        return;
    }
    // Check each factory for objects
    for(const ObjectFactoryUPtr& factory : m_factories) {
        if(factory->getEnabled()) {
            factory->createObjects(container, &names);
        }
    }
}

bool Catalog::hasChildren()
{
    if(m_childrenLoaded)
        return ObjectContainer::hasChildren();

    const char* settingsPath = CPLGetConfigOption("NGS_SETTINGS_PATH", nullptr);
    if(nullptr == settingsPath)
        return false;

    if(!Folder::isExists(settingsPath)) {
        if(!Folder::mkDir(settingsPath))
            return false;
    }
    // 1. Load factories
    // TODO: Add NextGIS Map document factory
    m_factories.push_back(ObjectFactoryUPtr(new DataStoreFactory()));
    m_factories.push_back(ObjectFactoryUPtr(new FileFactory()));
    m_factories.push_back(ObjectFactoryUPtr(new FolderFactory()));

    // 2. Load root objects
    const char* connectionsPath = CPLFormFilename(settingsPath, CONNECTIONS_DIR,
                                                  "");
    if(!Folder::isExists(connectionsPath)) {
        if(!Folder::mkDir(connectionsPath))
            return false;
    }
    m_children.push_back(ObjectPtr(new LocalConnections(this, connectionsPath)));

    m_childrenLoaded = true;
    return ObjectContainer::hasChildren();
}

CPLString Catalog::getSeparator()
{
    return "/";
}

unsigned short Catalog::getMaxPathLength()
{
    return 1024;
}

#ifdef _WIN32
bool Catalog::isFileHidden(const CPLString &filePath, const char *fileName)
#else
bool Catalog::isFileHidden(const CPLString &/*path*/, const char *name)
#endif
{
    if(m_showHidden)
        return false;

    if(EQUALN(name, ".", 1))
        return true;

#ifdef _WIN32
    DWORD attrs = GetFileAttributes(CPLFormFilename(filePath, fileName, NULL));
    if (attrs != INVALID_FILE_ATTRIBUTES)
        return attrs & FILE_ATTRIBUTE_HIDDEN;
#endif
    return false;
}

void Catalog::setShowHidden(bool value)
{
    m_showHidden = value;
    Settings::instance().set("catalog/show_hidden", value);
}

void Catalog::setInstance(Catalog *pointer)
{
    if(gCatalog && nullptr != pointer) // Can be initialized only once.
        return;
    gCatalog.reset(pointer);
}

CatalogPtr Catalog::getInstance()
{
    return gCatalog;
}

}
