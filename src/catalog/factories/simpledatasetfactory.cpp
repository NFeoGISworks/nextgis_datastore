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
#include "simpledatasetfactory.h"

#include <array>

#include "ds/simpledataset.h"
#include "ngstore/catalog/filter.h"

namespace ngs {

static const char *shpMainExts[] = {"shx", "dbf", nullptr};
static const char *shpExtraExts[] = {"sbn", "sbx", "cpg", "prj", "qix", "osf", nullptr};
constexpr FORMAT_EXT shpExt = {"shp", shpMainExts, shpExtraExts};

static const char *tabMainExts[] = {"dat", "map", "id", "ind", nullptr};
static const char *tabExtraExts[] = {"cpg", "qix", "osf", nullptr};
constexpr FORMAT_EXT tabExt = {"tab", tabMainExts, tabExtraExts};

static const char *mifMainExts[] = {"mid", nullptr};
static const char *mifExtraExts[] = {"cpg", "qix", "osf", nullptr};
constexpr FORMAT_EXT mifExt = {"mif", mifMainExts, mifExtraExts};

SimpleDatasetFactory::SimpleDatasetFactory() : ObjectFactory()
{
    m_shpSupported = Filter::getGDALDriver(
                ngsCatalogObjectType::CAT_FC_ESRI_SHAPEFILE);
    m_miSupported = Filter::getGDALDriver(
                ngsCatalogObjectType::CAT_FC_MAPINFO_TAB);
}

const char *SimpleDatasetFactory::getName() const
{
    return _("Feature classes and tables");
}

void SimpleDatasetFactory::createObjects(ObjectContainer * const container,
                                         std::vector<const char *> * const names)
{
    nameExtMap nameExts;
    std::vector<const char *>::iterator it = names->begin();
    while( it != names->end() ) {
        const char* ext = CPLGetExtension(*it);
        const char* baseName = CPLGetBasename(*it);
        nameExts[baseName].push_back(ext);
        ++it;
    }

    for(const auto& nameExtsItem : nameExts) {

        // Check if ESRI Shapefile
        if(m_shpSupported) {
        SimpleDatasetFactory::FORMAT_RESULT result = isFormatSupported(
                    nameExtsItem.first, nameExtsItem.second, shpExt);
        if(result.isSupported) {
            const char* path = CPLFormFilename(container->getPath(), result.name,
                                               nullptr);
            addChild(container, result.name, path,
                     ngsCatalogObjectType::CAT_FC_ESRI_SHAPEFILE,
                     result.siblingFiles, names);
        }
        }

        // Check if MapInfo tab
        if(m_miSupported) {
        SimpleDatasetFactory::FORMAT_RESULT result = isFormatSupported(
                            nameExtsItem.first, nameExtsItem.second, tabExt);
        if(result.isSupported) {
            const char* path = CPLFormFilename(container->getPath(), result.name,
                                               nullptr);
            addChild(container, result.name, path,
                     ngsCatalogObjectType::CAT_FC_MAPINFO_TAB,
                     result.siblingFiles, names);
        }

        // Check if MapInfo mif/mid
        result = isFormatSupported(
                            nameExtsItem.first, nameExtsItem.second, mifExt);
        if(result.isSupported) {
            const char* path = CPLFormFilename(container->getPath(), result.name,
                                               nullptr);
            addChild(container, result.name, path,
                     ngsCatalogObjectType::CAT_FC_MAPINFO_MIF,
                     result.siblingFiles, names);
        }
        }
    }
}

void SimpleDatasetFactory::addChild(ObjectContainer * const container,
                                    const CPLString &name,
                                    const CPLString &path,
                                    ngsCatalogObjectType subType,
                                    const std::vector<CPLString> &siblingFiles,
                                    std::vector<const char *> * const names)
{
    ObjectFactory::addChild(container,
                            ObjectPtr(new SimpleDataset(subType, siblingFiles,
                                                        container, name, path)));
    for(const auto& siblingFile : siblingFiles) {
        auto it = names->begin();
        while( it != names->end() ) {
            if(siblingFile.compare(*it) == 0) {
                it = names->erase(it);
            }
            else {
                ++it;
            }
        }
    }
}


SimpleDatasetFactory::FORMAT_RESULT SimpleDatasetFactory::isFormatSupported(
        const CPLString &name, std::vector<CPLString> extensions,
        FORMAT_EXT testExts)
{
    SimpleDatasetFactory::FORMAT_RESULT out;
    out.isSupported = false;

    unsigned char counter = 0;

    for(const auto& extension : extensions) {
        if(EQUAL(extension, testExts.mainExt)) { // Check main format extension
            counter++;
            out.name = CPLFormFilename(nullptr, name, extension);
        }
        else {
            int i = 0;
            bool breakCompare = false;
            while(testExts.mainExts[i] != nullptr) { // Check additional format extensions [required]
                if(EQUAL(extension, testExts.mainExts[i])) {
                    counter++;
                    breakCompare = true;
                    out.siblingFiles.push_back(CPLFormFilename(nullptr, name,
                                                               extension));
                }
                i++;
            }
            if(!breakCompare) {
                i = 0;
                while(testExts.extraExts[i] != nullptr) { // Check additional format extensions [optional]
                    if(EQUAL(extension, testExts.extraExts[i])) {
                        out.siblingFiles.push_back(CPLFormFilename(nullptr, name,
                                                                   extension));
                    }
                    i++;
                }
            }
        }
    }

    if(counter > CSLCount(testExts.mainExts) ) {
        out.isSupported = true;
    }

    return out;
}

} // namespace ngs

