/******************************************************************************
 * Project:  libngstore
 * Purpose:  NextGIS store and visualization support library
 * Author: Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
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
#include "ngstore/api.h"

// stl
#include <iostream>
#include <cstring>

// gdal
#include "cpl_http.h"
#include "cpl_json.h"

#include "catalog/catalog.h"
#include "catalog/mapfile.h"
#include "ds/simpledataset.h"
#include "map/mapstore.h"
#include "ngstore/catalog/filter.h"
#include "ngstore/version.h"
#include "ngstore/util/constants.h"
#include "util/authstore.h"
#include "util/error.h"
#include "util/notify.h"
#include "util/settings.h"
#include "util/stringutil.h"
#include "util/versionutil.h"

using namespace ngs;

constexpr const char* HTTP_TIMEOUT = "2";
constexpr const char* HTTP_USE_GZIP = "ON";
#if (TARGET_OS_IPHONE == 1) || (TARGET_IPHONE_SIMULATOR == 1)
constexpr const char* CACHEMAX = "8";
#elif __ANDROID__
constexpr const char* CACHEMAX = "4";
#else
constexpr const char* CACHEMAX = "64";
#endif

static bool gDebugMode = false;

bool isDebugMode()
{
    return gDebugMode;
}

/* special hook for find EPSG files
static const char *CSVFileOverride( const char * pszInput )
{
    return crash here as we need to duplicate string -> CPLFindFile ("", pszInput);
}
*/

void initGDAL(const char* dataPath, const char* cachePath)
{
    Settings& settings = Settings::instance();
    // set config options
    if(dataPath) {
        CPLSetConfigOption("GDAL_DATA", dataPath);
        CPLDebug("ngstore", "GDAL_DATA path set to %s", dataPath);
    }

    CPLSetConfigOption("GDAL_CACHEMAX",
                       settings.getString("common/cachemax", CACHEMAX));
    CPLSetConfigOption("GDAL_HTTP_USERAGENT",
                       settings.getString("http/useragent", NGS_USERAGENT));
    CPLSetConfigOption("CPL_CURL_GZIP",
                       settings.getString("http/use_gzip", HTTP_USE_GZIP));
    CPLSetConfigOption("GDAL_HTTP_TIMEOUT",
                       settings.getString("http/timeout", HTTP_TIMEOUT));
    CPLSetConfigOption("GDAL_DRIVER_PATH", "disabled");
#ifdef NGS_MOBILE // for mobile devices
    CPLSetConfigOption("CPL_VSIL_ZIP_ALLOWED_EXTENSIONS",
                       settings.getString("gdal/CPL_VSIL_ZIP_ALLOWED_EXTENSIONS", "apk"));
#endif
    if(cachePath) {
        CPLSetConfigOption("GDAL_DEFAULT_WMS_CACHE_PATH", cachePath);
    }
    if(isDebugMode()) {
        CPLSetConfigOption("CPL_DEBUG", "ON");
        CPLSetConfigOption("CPL_CURL_VERBOSE", "ON");
    }

    CPLSetConfigOption("CPL_ZIP_ENCODING",
                       settings.getString("common/zip_encoding", "CP866"));

    CPLDebug("ngstore", "HTTP user agent set to: %s", NGS_USERAGENT);

    // Register drivers
#ifdef NGS_MOBILE // NOTE: Keep in sync with extlib.cmake gdal options. For mobile devices
    GDALRegister_VRT();
    GDALRegister_GTiff();
    GDALRegister_HFA();
    GDALRegister_PNG();
    GDALRegister_JPEG();
    GDALRegister_MEM();
    GDALRegister_WMS();
    RegisterOGRShape();
    RegisterOGRTAB();
    RegisterOGRVRT();
    RegisterOGRMEM();
    RegisterOGRGPX();
    RegisterOGRKML();
    RegisterOGRGeoJSON();
    RegisterOGRGeoPackage();
    RegisterOGRSQLite();
#else
    GDALAllRegister();
#endif
    //SetCSVFilenameHook( CSVFileOverride );
}

/**
 * @brief ngsGetVersion Get library version number as major * 10000 + minor * 100 + rev
 * @param request may be gdal, proj, geos, curl, jpeg, png, zlib, iconv, sqlite3,
 *        openssl, expat, jsonc, tiff, geotiff
 * @return library version number
 */
int ngsGetVersion(const char* request)
{
    return getVersion(request);
}

/**
 * @brief ngsGetVersionString Get library version string
 * @param request may be gdal, proj, geos, curl, jpeg, png, zlib, iconv, sqlite3,
 *        openssl, expat, jsonc, tiff, geotiff
 * @return library version string
 */
const char* ngsGetVersionString(const char* request)
{
    return getVersionString(request);
}

/**
 * @brief ngsInit Init library structures
 * @param options Init library options list:
 * - CACHE_DIR - path to cache directory (mainly for TMS/WMS cache)
 * - SETTINGS_DIR - path to settings directory
 * - GDAL_DATA - path to GDAL data directory (may be skipped on Linux)
 * - DEBUG_MODE ["ON", "OFF"] - May be ON or OFF strings to enable/disable debug mode
 * - LOCALE ["en_US.UTF-8", "de_DE", "ja_JP", ...] - Locale for error messages, etc.
 * - NUM_THREADS - Number threads in various functions (a positive number or "ALL_CPUS")
 * - GL_MULTISAMPLE - Enable sampling if applicable
 * - SSL_CERT_FILE - Path to ssl cert file (*.pem)
 * - HOME - Root directory for library
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsInit(char** options)
{    
    gDebugMode = CSLFetchBoolean(options, "DEBUG_MODE", 0) == 0 ? false : true;
    CPLDebug("ngstore", "debug mode %s", gDebugMode ? "ON" : "OFF");
    const char* dataPath = CSLFetchNameValue(options, "GDAL_DATA");
    const char* cachePath = CSLFetchNameValue(options, "CACHE_DIR");
    const char* settingsPath = CSLFetchNameValue(options, "SETTINGS_DIR");    
    if(settingsPath) {
        CPLSetConfigOption("NGS_SETTINGS_PATH", settingsPath);
    }

    // Number threads
    const char* numThreads = CSLFetchNameValueDef(options, "NUM_THREADS", nullptr);
    if(!numThreads) {
        int cpuCount = CPLGetNumCPUs() - 1;
        if(cpuCount < 2)
            cpuCount = 1;
        numThreads = CPLSPrintf("%d", cpuCount);
    }
    CPLSetConfigOption("GDAL_NUM_THREADS", numThreads);
    const char* multisample = CSLFetchNameValue(options, "GL_MULTISAMPLE");
    if(multisample) {
        CPLSetConfigOption("GL_MULTISAMPLE", multisample);
    }

    const char* cainfo = CSLFetchNameValue(options, "SSL_CERT_FILE");
    if(cainfo) {
        CPLSetConfigOption("SSL_CERT_FILE", cainfo);
        CPLDebug("ngstore", "SSL_CERT_FILE path set to %s", cainfo);
    }

    const char* home = CSLFetchNameValue(options, "HOME");
    if(home) {
        CPLSetConfigOption("NGS_HOME", home);
        CPLDebug("ngstore", "NGS_HOME path set to %s", home);
    }

#ifdef HAVE_LIBINTL_H
    const char* locale = CSLFetchNameValue(options, "LOCALE");
    //TODO: Do we need std::setlocale(LC_ALL, locale); execution here in library or it will call from programm?
#endif

#ifdef NGS_MOBILE
    if(nullptr == dataPath) {
        return errorMessage(COD_NOT_SPECIFIED,
                           _("GDAL_DATA option is required"));
    }
#endif

    initGDAL(dataPath, cachePath);

    Catalog::setInstance(new Catalog());
    MapStore::setInstance(new MapStore());

    return COD_SUCCESS;
}

/**
 * @brief ngsUnInit Clean up library structures
 */
void ngsUnInit()
{
    MapStore::setInstance(nullptr);
    Catalog::setInstance(nullptr);
    GDALDestroyDriverManager();
}

/**
 * @brief ngsFreeResources Inform library to free resources as possible
 * @param full If full is true maximum resources will be freed.
 */
void ngsFreeResources(char full)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr != mapStore) {
        mapStore->freeResources();
    }
    if(full) {
        CatalogPtr catalog = Catalog::instance();
        if(catalog) {
            catalog->freeResources();
        }
    }
}

/**
 * @brief ngsGetLastErrorMessage Fetches the last error message posted with
 * returnError, CPLError, etc.
 * @return last error message or NULL if no error message present.
 */
const char* ngsGetLastErrorMessage()
{
    return getLastError();
}

/**
 * @brief ngsAddNotifyFunction Add function triggered on some events
 * @param function Function executed on event occurred
 * @param notifyTypes The OR combination of ngsChangeCode
 */
void ngsAddNotifyFunction(ngsNotifyFunc function, int notifyTypes)
{
    Notify::instance().addNotifyReceiver(function, notifyTypes);
}

/**
 * @brief ngsRemoveNotifyFunction Remove function. No events will be occurred.
 * @param function The function to remove
 */
void ngsRemoveNotifyFunction(ngsNotifyFunc function)
{
    Notify::instance().deleteNotifyReceiver(function);
}

//------------------------------------------------------------------------------
// GDAL proxy functions
//------------------------------------------------------------------------------

/**
 * @brief ngsGetCurrentDirectory Returns curretnt working directory
 * @return Current directory path in OS
 */
const char* ngsGetCurrentDirectory()
{
    return CPLGetCurrentDir();
}

/**
 * @brief ngsAddNameValue Add key=value pair into the list
 * @param list The list pointer or NULL. If NULL provided the new list will be created
 * @param name Key name to add
 * @param value Value to add
 * @return List with added key=value string. List must bree freed using ngsDestroyList.
 */
char** ngsAddNameValue(char** list, const char* name, const char* value)
{
    return CSLAddNameValue(list, name, value);
}

/**
 * @brief ngsDestroyList Destroy list created using ngsAddNameValue
 * @param list The list to destroy.
 */
void ngsListFree(char** list)
{
    CSLDestroy(list);
}

/**
 * @brief ngsFormFileName Form new path string
 * @param path A parent path
 * @param name A file name
 * @param extension A file extension
 * @return The new path string
 */
const char* ngsFormFileName(const char* path, const char* name,
                            const char* extension)
{
    return CPLFormFilename(path, name, extension);
}

/**
 * @brief ngsFree Free pointer allocated using some function.
 * @param pointer A pointer to free.
 */
void ngsFree(void* pointer)
{
    CPLFree(pointer);
}

//------------------------------------------------------------------------------
// Miscellaneous functions
//------------------------------------------------------------------------------

/**
 * @brief ngsURLRequest Perform web request
 * @param type Request type (GET, POST, PUT, DELETE)
 * @param url Request URL
 * @param options Available options are:
 * - PERSISTENT=name, create persistent connection with provided name
 * - CLOSE_PERSISTENT=name, close persistent connection with provided name
 * - CONNECTTIMEOUT=val, where val is in seconds (possibly with decimals).
 *   This is the maximum delay for the connection to be established before
 *   being aborted
 * - TIMEOUT=val, where val is in seconds. This is the maximum delay for the whole
 *   request to complete before being aborted
 * - LOW_SPEED_TIME=val, where val is in seconds. This is the maximum time where the
 *   transfer speed should be below the LOW_SPEED_LIMIT (if not specified 1b/s),
 *   before the transfer to be considered too slow and aborted
 * - LOW_SPEED_LIMIT=val, where val is in bytes/second. See LOW_SPEED_TIME. Has only
 *   effect if LOW_SPEED_TIME is specified too
 * - HEADERS=val, where val is an extra header to use when getting a web page.
 *   For example "Accept: application/x-ogcwkt"
 * - HEADER_FILE=filename: filename of a text file with "key: value" headers
 * - HTTPAUTH=[BASIC/NTLM/GSSNEGOTIATE/ANY] to specify an authentication scheme to use
 * - USERPWD=userid:password to specify a user and password for authentication
 * - POSTFIELDS=val, where val is a nul-terminated string to be passed to the server
 *   with a POST request
 * - PROXY=val, to make requests go through a proxy server, where val is of the
 *   form proxy.server.com:port_number
 * - PROXYUSERPWD=val, where val is of the form username:password
 * - PROXYAUTH=[BASIC/NTLM/DIGEST/ANY] to specify an proxy authentication scheme
 *   to use
 * - COOKIE=val, where val is formatted as COOKIE1=VALUE1; COOKIE2=VALUE2; ...
 * - MAX_RETRY=val, where val is the maximum number of retry attempts if a 503 or
 *   504 HTTP error occurs. Default is 0
 * - RETRY_DELAY=val, where val is the number of seconds between retry attempts.
 *   Default is 30
 * - MAX_FILE_SIZE=val, where val is a number of bytes
 * @return structure of type ngsURLRequestResult
 */
ngsURLRequestResult* ngsURLRequest(enum ngsURLRequestType type, const char* url,
                                  char** options)
{
    Options requestOptions(options);
    switch (type) {
    case URT_GET:
        requestOptions.addOption("CUSTOMREQUEST", "GET");
        break;
    case URT_POST:
        requestOptions.addOption("CUSTOMREQUEST", "POST");
        break;
    case URT_PUT:
        requestOptions.addOption("CUSTOMREQUEST", "PUT");
        break;
    case URT_DELETE:
        requestOptions.addOption("CUSTOMREQUEST", "DELETE");
        break;
    }

    ngsURLRequestResult* out = new ngsURLRequestResult;    
    auto optionsPtr = requestOptions.getOptions();
    CPLHTTPResult* result = CPLHTTPFetch(url, optionsPtr.get());

    if(result->nStatus != 0) {
        errorMessage(COD_REQUEST_FAILED, result->pszErrBuf);
        out->status = 543;
        out->headers = nullptr;
        out->dataLen = 0;
        out->data = nullptr;

        CPLHTTPDestroyResult( result );

        return out;
    }
    else if(result->pszErrBuf) {
        warningMessage(COD_WARNING, result->pszErrBuf);
    }

    unsigned char* buffer = new unsigned char[result->nDataLen];
    std::memcpy(buffer, result->pabyData, static_cast<size_t>(result->nDataLen));

    out->status = static_cast<int>(result->nHTTPResponseCode);
    out->headers = result->papszHeaders;
    out->dataLen = result->nDataLen;
    out->data = buffer;

    result->papszHeaders = nullptr;

    CPLHTTPDestroyResult( result );

    return out;
}

void ngsURLRequestResultFree(ngsURLRequestResult* result)
{
    if(nullptr == result)
        return;
    delete result->data;
    CSLDestroy(result->headers);
    delete result;
}

/**
 * @brief ngsURLAuthAdd Adds HTTP Authorisation to store. When some HTTP
 * request executed it will ask store for authorisation header.
 * @param url The URL this authorisation options belongs to. All requests
 * started with this URL will add authorisation header.
 * @param options Authorisation options.
 * HTTPAUTH_TYPE - Requered. The authorisation type (i.e. bearer).
 * HTTPAUTH_CLIENT_ID - Client identificator for bearer
 * HTTPAUTH_TOKEN_SERVER - Token validate/update server
 * HTTPAUTH_ACCESS_TOKEN - Access token
 * HTTPAUTH_REFRESH_TOKEN - Refresh token
 * HTTPAUTH_EXPIRES_IN - Expires ime in sec.
 * HTTPAUTH_CONNECTION_TIMEOUT - Connection timeout to token server. Default 5.
 * HTTPAUTH_TIMEOUT - Network timeout to token server. Default 15.
 * HTTPAUTH_MAX_RETRY - Retries count to token server. Default 5.
 * HTTPAUTH_RETRY_DELAY - Delay between retries. Default 5.
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsURLAuthAdd(const char* url, char** options)
{
    Options opt(options);
    return AuthStore::addAuth(url, opt) ? COD_SUCCESS : COD_INSERT_FAILED;
}

/**
 * @brief ngsURLAuthGet If Authorization properties changed, this
 * function helps to get them back.
 * @param url The URL to search authorization options.
 * @return Key=value list (may be empty). User mast free returned
 * value via ngsDestroyList.
 */
char** ngsURLAuthGet(const char* url)
{
    Options option = AuthStore::description(url);
    if(option.empty())
        return nullptr;
    return option.getOptions().release();
}

/**
 * @brief ngsURLAuthDelete Removes authorization from store
 * @param url The URL to search authorization options.
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsURLAuthDelete(const char* url)
{
    AuthStore::deleteAuth(url);
    return COD_SUCCESS;
}

/**
 * @brief ngsMD5 Transform string to MD5 hash
 * @param value String to transform
 * @return Hex presentation of MD5 hash
 */
const char* ngsMD5(const char* value)
{
    return CPLSPrintf("%s", md5(value).c_str());
}

/**
 * @brief ngsJsonDocumentCreate Creates new JSON document
 * @return New JSON document handle. The handle must be deallocated using
 * ngsJsonDocumentDestroy function.
 */
JsonDocumentH ngsJsonDocumentCreate()
{
    return new CPLJSONDocument;
}

/**
 * @brief ngsJsonDocumentDestroy Destroy JSON document created using
 * ngsJsonDocumentCreate
 * @param document JSON documetn hadler.
 */
void ngsJsonDocumentFree(JsonDocumentH document)
{
    delete static_cast<CPLJSONDocument*>(document);
}

/**
 * @brief ngsJsonDocumentLoadUrl Load JSON request result parsing chunk by chunk
 * @param document The document handle created using ngsJsonDocumentCreate
 * @param url URL to load
 * @param options A list of key=value items ot NULL. The JSON_DEPTH=10 is the
 * JSON tokener option. The other options are the same of ngsURLRequest function.
 * @param callback Function executed periodically during load process. If returns
 * false the loading canceled. May be null.
 * @param callbackData The data for callback function execution. May be NULL.
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsJsonDocumentLoadUrl(JsonDocumentH document, const char* url, char** options,
                           ngsProgressFunc callback, void* callbackData)
{
    CPLJSONDocument* doc = static_cast<CPLJSONDocument*>(document);
    if(nullptr == doc) {
        return errorMessage(COD_LOAD_FAILED, _("Layer pointer is null"));
    }

    Progress progress(callback, callbackData);
    return doc->LoadUrl(url, options, onGDALProgress, &progress) ? COD_SUCCESS :
                                                                   COD_LOAD_FAILED;
}

/**
 * @brief ngsJsonDocumentRoot Gets JSON document root object.
 * @param document JSON document
 * @return JSON object handle or NULL. The handle must be deallocated using
 * ngsJsonObjectDestroy function.
 */
JsonObjectH ngsJsonDocumentRoot(JsonDocumentH document)
{
    CPLJSONDocument* doc = static_cast<CPLJSONDocument*>(document);
    if(nullptr == doc) {
        errorMessage(COD_GET_FAILED, _("Layer pointer is null"));
        return nullptr;
    }
    return new CPLJSONObject(doc->GetRoot());
}


void ngsJsonObjectFree(JsonObjectH object)
{
    if(nullptr != object)
        delete static_cast<CPLJSONObject*>(object);
}

int ngsJsonObjectType(JsonObjectH object)
{
    if(nullptr == object) {
        errorMessage(COD_GET_FAILED, _("The object handle is null"));
        return CPLJSONObject::Type::Null;
    }
    return static_cast<CPLJSONObject*>(object)->GetType();
}

const char* ngsJsonObjectName(JsonObjectH object)
{
    if(nullptr == object) {
        errorMessage(COD_GET_FAILED, _("The object handle is null"));
        return "";
    }
    return static_cast<CPLJSONObject*>(object)->GetName();
}

JsonObjectH* ngsJsonObjectChildren(JsonObjectH object)
{
    if(nullptr == object) {
        errorMessage(COD_GET_FAILED, _("The object handle is null"));
        return nullptr;
    }
    return reinterpret_cast<JsonObjectH*>(
                static_cast<CPLJSONObject*>(object)->GetChildren());
}

void ngsJsonObjectChildrenListFree(JsonObjectH* list)
{
    if(nullptr == list) {
        errorMessage(COD_GET_FAILED, _("The object handle is null"));
    }
    CPLJSONObject::DestroyJSONObjectList(reinterpret_cast<CPLJSONObject**>(list));
}

const char* ngsJsonObjectGetString(JsonObjectH object, const char* defaultValue)
{
    if(nullptr == object) {
        errorMessage(COD_GET_FAILED, _("The object handle is null"));
        return defaultValue;
    }
    return static_cast<CPLJSONObject*>(object)->GetString(defaultValue);
}

double ngsJsonObjectGetDouble(JsonObjectH object, double defaultValue)
{
    if(nullptr == object) {
        errorMessage(COD_GET_FAILED, _("The object handle is null"));
        return defaultValue;
    }
    return static_cast<CPLJSONObject*>(object)->GetDouble(defaultValue);
}

int ngsJsonObjectGetInteger(JsonObjectH object, int defaultValue)
{
    if(nullptr == object) {
        errorMessage(COD_GET_FAILED, _("The object handle is null"));
        return defaultValue;
    }
    return static_cast<CPLJSONObject*>(object)->GetInteger(defaultValue);
}

long ngsJsonObjectGetLong(JsonObjectH object, long defaultValue)
{
    if(nullptr == object) {
        errorMessage(COD_GET_FAILED, _("The object handle is null"));
        return defaultValue;
    }
    return static_cast<CPLJSONObject*>(object)->GetLong(defaultValue);
}

bool ngsJsonObjectGetBool(JsonObjectH object, bool defaultValue)
{
    if(nullptr == object) {
        errorMessage(COD_GET_FAILED, _("The object handle is null"));
        return defaultValue;
    }
    return static_cast<CPLJSONObject*>(object)->GetBool(defaultValue);
}

JsonObjectH ngsJsonObjectGetArray(JsonObjectH object, const char* name)
{
    if(nullptr == object) {
        errorMessage(COD_GET_FAILED, _("The object handle is null"));
        return nullptr;
    }
    return new CPLJSONArray(static_cast<CPLJSONObject*>(object)->GetArray(name));
}

JsonObjectH ngsJsonObjectGetObject(JsonObjectH object, const char* name)
{
    if(nullptr == object) {
        errorMessage(COD_GET_FAILED, _("The object handle is null"));
        return nullptr;
    }
    return new CPLJSONObject(static_cast<CPLJSONObject*>(object)->GetObject(name));
}

int ngsJsonArraySize(JsonObjectH object)
{
    if(nullptr == object) {
        errorMessage(COD_GET_FAILED, _("The object handle is null"));
        return 0;
    }
    return static_cast<CPLJSONArray*>(object)->Size();
}

JsonObjectH ngsJsonArrayItem(JsonObjectH object, int index)
{
    if(nullptr == object) {
        errorMessage(COD_GET_FAILED, _("The object handle is null"));
        return nullptr;
    }
    return new CPLJSONObject(static_cast<CPLJSONArray*>(object)->operator[](index));
}


//------------------------------------------------------------------------------
// Catalog
//------------------------------------------------------------------------------

/**
 * @brief catalogObjectQuery Request the contents of some catalog conatiner object.
 * @param object Catalog object. Must be container (containes some catalog objects).
 * @param objectFilter Filter output results
 * @return List of ngsCatalogObjectInfo items or NULL. The last element of list always NULL.
 * The list must be deallocated using ngsFree function.
 */
ngsCatalogObjectInfo* catalogObjectQuery(CatalogObjectH object,
                                         const Filter& objectFilter)
{
    Object* catalogObject = static_cast<Object*>(object);
    if(!catalogObject) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return nullptr;
    }
    ObjectPtr catalogObjectPointer = catalogObject->pointer();

    ngsCatalogObjectInfo* output = nullptr;
    size_t outputSize = 0;
    ObjectContainer* const container = dynamic_cast<ObjectContainer*>(catalogObject);
    if(!container) {
        if(!objectFilter.canDisplay(catalogObjectPointer)) {
            return nullptr;
        }

        output = static_cast<ngsCatalogObjectInfo*>(
                    CPLMalloc(sizeof(ngsCatalogObjectInfo) * 2));
        output[0] = {catalogObject->name(), catalogObject->type(), catalogObject};
        output[1] = {nullptr, -1, nullptr};
        return output;
    }

    if(!container->hasChildren()) {
        if(container->type() == CAT_CONTAINER_SIMPLE) {
            SimpleDataset* const simpleDS = dynamic_cast<SimpleDataset*>(container);
            output = static_cast<ngsCatalogObjectInfo*>(
                        CPLMalloc(sizeof(ngsCatalogObjectInfo) * 2));
            output[0] = {catalogObject->name(), simpleDS->subType(), catalogObject};
            output[1] = {nullptr, -1, nullptr};
            return output;
        }
        return nullptr;
    }

    auto children = container->getChildren();
    if(children.empty()) {
        return nullptr;
    }

    for(const auto &child : children) {
        if(objectFilter.canDisplay(child)) {
            SimpleDataset* simpleDS = nullptr;
            if(child->type() == CAT_CONTAINER_SIMPLE) {
                simpleDS = ngsDynamicCast(SimpleDataset, child);
            }

            outputSize++;
            output = static_cast<ngsCatalogObjectInfo*>(CPLRealloc(output,
                                sizeof(ngsCatalogObjectInfo) * (outputSize + 1)));

            if(simpleDS) {
                output[outputSize - 1] = {child->name(),
                                          simpleDS->subType(),
                                          simpleDS->internalObject().get()};
            }
            else {
                output[outputSize - 1] = {child->name(),
                                          child->type(), child.get()};
            }
        }
    }
    if(outputSize > 0) {
        output[outputSize] = {nullptr, -1, nullptr};
    }
    return output;
}

/**
 * @brief ngsCatalogObjectQuery Queries name and type of child objects for
 * provided path and filter
 * @param object The handle of catalog object
 * @param filter Only objects correspondent to provided filter will be return
 * @return Array of ngsCatlogObjectInfo structures. Caller must free this array
 * after using with ngsFree method
 */
ngsCatalogObjectInfo* ngsCatalogObjectQuery(CatalogObjectH object, int filter)
{
    // Create filter class from filter value.
    Filter objectFilter(static_cast<enum ngsCatalogObjectType>(filter));

    return catalogObjectQuery(object, objectFilter);
}

/**
 * @brief ngsCatalogObjectQueryMultiFilter Queries name and type of child objects for
 * provided path and filters
 * @param object The handle of catalog object
 * @param filter Only objects correspondent to provided filters will be return.
 * User must delete filters array manually
 * @param filterCount The filters count
 * @return Array of ngsCatlogObjectInfo structures. Caller must free this array
 * after using with ngsFree method
 */
ngsCatalogObjectInfo* ngsCatalogObjectQueryMultiFilter(CatalogObjectH object,
                                                       int* filters,
                                                       int filterCount)
{
    // Create filter class from filter value.
    MultiFilter objectFilter;
    for(int i = 0; i < filterCount; ++i) {
        objectFilter.addType(static_cast<enum ngsCatalogObjectType>(filters[i]));
    }

    return catalogObjectQuery(object, objectFilter);
}

/**
 * @brief ngsCatalogObjectDelete Deletes catalog object on specified path
 * @param object The handle of catalog object
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsCatalogObjectDelete(CatalogObjectH object)
{
    Object* catalogObject = static_cast<Object*>(object);
    if(!catalogObject)
        return errorMessage(COD_INVALID, _("The object handle is null"));

    // Check can delete
    if(catalogObject->canDestroy())
        return catalogObject->destroy() ? COD_SUCCESS : COD_DELETE_FAILED;
    return errorMessage(COD_UNSUPPORTED,
                       _("The path cannot be deleted (write protected, locked, etc.)"));
}

/**
 * @brief ngsCatalogObjectCreate Creates new catalog object
 * @param object The handle of catalog object
 * @param name The new object name
 * @param options The array of create object options. Caller must free this
 * array after function finishes. The common values are:
 * TYPE (required) - The new object type from enum ngsCatalogObjectType
 * CREATE_UNIQUE [ON, OFF] - If name already exists in container, make it unique
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsCatalogObjectCreate(CatalogObjectH object, const char* name, char** options)
{
    Object* catalogObject = static_cast<Object*>(object);
    if(!catalogObject)
        return errorMessage(COD_INVALID, _("The object handle is null"));

    Options createOptions(options);
    enum ngsCatalogObjectType type = static_cast<enum ngsCatalogObjectType>(
                createOptions.intOption("TYPE", CAT_UNKNOWN));
    createOptions.removeOption("TYPE");
    ObjectContainer * const container = dynamic_cast<ObjectContainer*>(catalogObject);
    // Check can create
    if(nullptr != container && container->canCreate(type))
        return container->create(type, name, createOptions) ?
                    COD_SUCCESS : COD_CREATE_FAILED;

    return errorMessage(COD_UNSUPPORTED,
                        _("Cannot create such object type (%d) in path: %s"),
                        type, catalogObject->fullName().c_str());
}

/**
 * @brief ngsCatalogPathFromSystem Finds catalog path
 * (i.e. ngc://Local connections/tmp) correspondent system path (i.e. /home/user/tmp
 * @param path System path
 * @return Catalog path or empty string
 */
const char* ngsCatalogPathFromSystem(const char* path)
{
    CatalogPtr catalog = Catalog::instance();
    ObjectPtr object = catalog->getObjectByLocalPath(path);
    if(object) {
        return CPLSPrintf("%s", object->fullName().c_str());
    }
    return "";
}

/**
 * @brief ngsCatalogObjectLoad Copies or move source dataset to destination dataset
 * @param srcObject The handle of source catalog object
 * @param dstObject The handle of destination destination catalog object.
 * Should be container which is ready to accept source dataset types.
 * @param options The options key-value array specific to operation and
 * destination dataset. The load options can be fetched via
 * ngsCatalogObjectOptions() function. Also load options can combine with
 * layer create options.
 * @param callback The callback function to report or cancel process.
 * @param callbackData The callback function data.
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsCatalogObjectLoad(CatalogObjectH srcObject, CatalogObjectH dstObject,
                         char** options, ngsProgressFunc callback,
                         void* callbackData)
{
    Object* srcCatalogObject = static_cast<Object*>(srcObject);
    Object* dstCatalogObject = static_cast<Object*>(dstObject);
    if(!srcCatalogObject || !dstCatalogObject) {
        return errorMessage(COD_INVALID, _("The object handle is null"));
    }

    ObjectPtr srcCatalogObjectPointer = srcCatalogObject->pointer();
    ObjectPtr dstCatalogObjectPointer = dstCatalogObject->pointer();

    Progress progress(callback, callbackData);
    Options loadOptions(options);
    bool move = loadOptions.boolOption("MOVE", false);
    loadOptions.removeOption("MOVE");

    if(move && !srcCatalogObjectPointer->canDestroy()) {
        return  errorMessage(COD_MOVE_FAILED,
                             _("Cannot move source dataset '%s'"),
                             srcCatalogObjectPointer->fullName().c_str());
    }

    if(srcCatalogObjectPointer->type() == CAT_CONTAINER_SIMPLE) {
        SimpleDataset * const dataset = dynamic_cast<SimpleDataset*>(srcCatalogObject);
        dataset->hasChildren();
        srcCatalogObjectPointer = dataset->internalObject();
    }

    if(!srcCatalogObjectPointer) {
        return errorMessage(COD_INVALID,
                            _("Source dataset type is incompatible"));
    }

    Dataset * const dstDataset = dynamic_cast<Dataset*>(dstCatalogObject);
    if(nullptr == dstDataset) {
        return move ? COD_MOVE_FAILED : COD_COPY_FAILED;
    }
    dstDataset->hasChildren();

    // Check can paster
    if(dstDataset->canPaste(srcCatalogObjectPointer->type())) {
        return dstDataset->paste(srcCatalogObjectPointer, move, loadOptions, progress);
    }

    return errorMessage(move ? COD_MOVE_FAILED : COD_COPY_FAILED,
                        _("Destination dataset '%s' is not container or cannot accept source dataset '%s'"),
                        dstCatalogObjectPointer->fullName().c_str(),
                        srcCatalogObjectPointer->fullName().c_str());

}

/**
 * @brief ngsCatalogObjectCopy Copies or moves catalog object to another location
 * @param srcObject Source catalog object
 * @param dstObjectContainer Destination catalog container
 * @param options Copy options. This is a list of key=value items.
 * The common value is MOVE=ON indicating to move object. The other values depends on
 * destination container.
 * @param callback Progress function (template is ngsProgressFunc) executed
 * periodically to report progress and cancel. If returns 1 the execution will
 * continue, 0 - cancelled.
 * @param callbackData Progress fuction parameter.
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsCatalogObjectCopy(CatalogObjectH srcObject,
                         CatalogObjectH dstObjectContainer, char** options,
                         ngsProgressFunc callback, void* callbackData)
{
    Object* srcCatalogObject = static_cast<Object*>(srcObject);
    ObjectContainer* dstCatalogObjectContainer =
            static_cast<ObjectContainer*>(dstObjectContainer);
    if(!srcCatalogObject || !dstCatalogObjectContainer) {
        return errorMessage(COD_INVALID, _("The object handle is null"));
    }

    ObjectPtr srcCatalogObjectPointer = srcCatalogObject->pointer();

    Progress progress(callback, callbackData);
    Options copyOptions(options);
    bool move = copyOptions.boolOption("MOVE", false);
    copyOptions.removeOption("MOVE");

    if(move && !srcCatalogObjectPointer->canDestroy()) {
        return  errorMessage(COD_MOVE_FAILED,
                             _("Cannot move source dataset '%s'"),
                             srcCatalogObjectPointer->fullName().c_str());
    }

    if(dstCatalogObjectContainer->canPaste(srcCatalogObjectPointer->type())) {
        return dstCatalogObjectContainer->paste(srcCatalogObjectPointer, move,
                                                copyOptions, progress);
    }

    return errorMessage(move ? COD_MOVE_FAILED : COD_COPY_FAILED,
                        _("Destination container '%s' cannot accept source dataset '%s'"),
                        dstCatalogObjectContainer->fullName().c_str(),
                        srcCatalogObjectPointer->fullName().c_str());
}

/**
 * @brief ngsCatalogObjectRename Renames catalog object
 * @param object The handle of catalog object
 * @param newName The new object name. The name should be unique inside object
 * parent container
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsCatalogObjectRename(CatalogObjectH object, const char* newName)
{
    Object* catalogObject = static_cast<Object*>(object);
    if(!catalogObject)
        return errorMessage(COD_INVALID, _("The object handle is null"));

    if(!catalogObject->canRename())
        return errorMessage(COD_RENAME_FAILED,
                            _("Cannot rename catalog object '%s' to '%s'"),
                            catalogObject->fullName().c_str(), newName);

    return catalogObject->rename(newName) ? COD_SUCCESS : COD_RENAME_FAILED;
}

/**
 * @brief ngsCatalogObjectOptions Queries catalog object options.
 * @param object The handle of catalog object
 * @param optionType The one of ngsOptionTypes enum values:
 * OT_CREATE_DATASOURCE, OT_CREATE_RASTER, OT_CREATE_LAYER,
 * OT_CREATE_LAYER_FIELD, OT_OPEN, OT_LOAD
 * @return Options description in xml form.
 */
const char* ngsCatalogObjectOptions(CatalogObjectH object, int optionType)
{
    if(!object) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return "";
    }
    Object* catalogObject = static_cast<Object*>(object);
    if(!Filter::isDatabase(catalogObject->type())) {
        errorMessage(COD_INVALID,
                            _("The input object not a dataset. The type is %d. Options query not supported"),
                     catalogObject->type());
        return "";
    }

    Dataset * const dataset = dynamic_cast<Dataset*>(catalogObject);
    if(nullptr == dataset) {
        errorMessage(COD_INVALID,
                            _("The input object not a dataset. Options query not supported"));
        return "";
    }
    enum ngsOptionType enumOptionType = static_cast<enum ngsOptionType>(optionType);

    return dataset->options(enumOptionType);
}

/**
 * @brief ngsCatalogObjectGet Gets catalog object handle by path
 * @param path Path to the catalog object
 * @return handel or null
 */
CatalogObjectH ngsCatalogObjectGet(const char* path)
{
    CatalogPtr catalog = Catalog::instance();
    ObjectPtr object = catalog->getObject(path);
    return object.get();
}

/**
 * @brief ngsCatalogObjectType Returns input object handle type
 * @param object Object handle
 * @return Object type - the value from ngsCatalogObjectType
 */
enum ngsCatalogObjectType ngsCatalogObjectType(CatalogObjectH object)
{
    if(nullptr == object)
        return CAT_UNKNOWN;
    return static_cast<Object*>(object)->type();
}

/**
 * @brief ngsCatalogObjectName Returns input object handle name
 * @param object Object handle
 * @return Catalog object name
 */
const char* ngsCatalogObjectName(CatalogObjectH object)
{
    if(nullptr == object)
        return "";
    return static_cast<Object*>(object)->name();
}

/**
 * @brief ngsCatalogObjectMetadata Returns catalog object metadata
 * @param object Catalog object the metadata requested
 * @param domain The metadata specific domain or NULL
 * @return The list of key=value items or NULL. The list must be deallocated
 * using ngsFree. The last item of list always NULL.
 */
char** ngsCatalogObjectMetadata(CatalogObjectH object, const char* domain)
{
    Object* catalogObject = static_cast<Object*>(object);
    if(!catalogObject) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return nullptr;
    }

    DatasetBase* datasetBase = dynamic_cast<DatasetBase*>(catalogObject);
    if(!datasetBase) {
        errorMessage(COD_INVALID, _("Source dataset type is incompatible"));
        return nullptr;
    }

    return datasetBase->getMetadata(domain);
}

//------------------------------------------------------------------------------
// Feature class
//------------------------------------------------------------------------------

FeatureClass* getFeatureClassFromHandle(CatalogObjectH object)
{
    Object* catalogObject = static_cast<Object*>(object);
    if(!catalogObject) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return nullptr;
    }

    ObjectPtr catalogObjectPointer = catalogObject->pointer();
    if(catalogObjectPointer->type() == CAT_CONTAINER_SIMPLE) {
        SimpleDataset * const dataset = dynamic_cast<SimpleDataset*>(catalogObject);
        dataset->hasChildren();
        catalogObjectPointer = dataset->internalObject();
    }

    if(!catalogObjectPointer) {
        errorMessage(COD_INVALID, _("Source dataset type is incompatible"));
        return nullptr;
    }

    if(!Filter::isFeatureClass(catalogObjectPointer->type())) {
        errorMessage(COD_INVALID, _("Source dataset type is incompatible"));
        return nullptr;
    }

    return ngsDynamicCast(FeatureClass, catalogObjectPointer);
}

/**
 * @brief ngsFeatureClassCreateOverviews Creates Gl optimized vector tiles
 * @param object Catalog object handle. Must be feature class or simple datasource.
 * @param options The options key-value array specific to operation.
 * @param callback The callback function to report or cancel process.
 * @param callbackData The callback function data.
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsFeatureClassCreateOverviews(CatalogObjectH object, char** options,
                                   ngsProgressFunc callback, void* callbackData)
{
    FeatureClass* featureClass = getFeatureClassFromHandle(object);
    if(!featureClass) {
        return errorMessage(COD_INVALID,
                            _("Source dataset type is incompatible"));
    }

    Options createOptions(options);
    Progress createProgress(callback, callbackData);
    return featureClass->createOverviews(createProgress, createOptions);
}

/**
 * @brief ngsFeatureClassCreateFeature Creates new feature
 * @param object Handle to FeatureClass or SimpleDataset catalog object
 * @return Feature handle or NULL
 */
FeatureH ngsFeatureClassCreateFeature(CatalogObjectH object)
{
    FeatureClass* featureClass = getFeatureClassFromHandle(object);
    if(!featureClass) {
        errorMessage(COD_INVALID, _("Source dataset type is incompatible"));
        return nullptr;
    }

    FeaturePtr feature = featureClass->createFeature();
    return new FeaturePtr(feature);
}

int ngsFeatureClassInsertFeature(CatalogObjectH object, FeatureH feature)
{
    FeatureClass* featureClass = getFeatureClassFromHandle(object);
    if(!featureClass) {
        return errorMessage(COD_INVALID,
                            _("Source dataset type is incompatible"));
    }

    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    return featureClass->insertFeature(*featurePtrPointer) ? COD_SUCCESS :
                                                             COD_INSERT_FAILED;
}

int ngsFeatureClassUpdateFeature(CatalogObjectH object, FeatureH feature)
{
    FeatureClass* featureClass = getFeatureClassFromHandle(object);
    if(!featureClass) {
        return errorMessage(COD_INVALID,
                            _("Source dataset type is incompatible"));
    }

    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    return featureClass->updateFeature(*featurePtrPointer) ? COD_SUCCESS :
                                                             COD_UPDATE_FAILED;
}

int ngsFeatureClassDeleteFeature(CatalogObjectH object, long long id)
{
    FeatureClass* featureClass = getFeatureClassFromHandle(object);
    if(!featureClass) {
        return errorMessage(COD_INVALID,
                            _("Source dataset type is incompatible"));
    }
    return featureClass->deleteFeature(id) ? COD_SUCCESS : COD_DELETE_FAILED;
}

long long ngsFeatureClassCount(CatalogObjectH object)
{
    FeatureClass* featureClass = getFeatureClassFromHandle(object);
    if(!featureClass) {
        errorMessage(COD_INVALID, _("Source dataset type is incompatible"));
        return 0;
    }
    return featureClass->featureCount();
}

void ngsFeatureFree(FeatureH feature)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(featurePtrPointer)
        delete featurePtrPointer;
}

int ngsFeatureFieldCount(FeatureH feature)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(!featurePtrPointer) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return 0;
    }
    return (*featurePtrPointer)->GetFieldCount();
}

int ngsFeatureIsFieldSet(FeatureH feature, int fieldIndex)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(!featurePtrPointer) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return false;
    }
    return (*featurePtrPointer)->IsFieldSetAndNotNull(fieldIndex) ? 1 : 0;

}

long long ngsFeatureGetId(FeatureH feature)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(!featurePtrPointer) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return 0;
    }
    return (*featurePtrPointer)->GetFID();
}

GeometryH ngsFeatureGetGeometry(FeatureH feature)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(!featurePtrPointer) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return nullptr;
    }
    return (*featurePtrPointer)->GetGeometryRef();
}

int ngsFeatureGetFieldAsInteger(FeatureH feature, int field)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(!featurePtrPointer) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return 0;
    }
    return (*featurePtrPointer)->GetFieldAsInteger(field);

}

double ngsFeatureGetFieldAsDouble(FeatureH feature, int field)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(!featurePtrPointer) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return 0.0;
    }
    return (*featurePtrPointer)->GetFieldAsDouble(field);
}

const char*ngsFeatureGetFieldAsString(FeatureH feature, int field)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(!featurePtrPointer) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return "";
    }
    return (*featurePtrPointer)->GetFieldAsString(field);
}

int ngsFeatureGetFieldAsDateTime(FeatureH feature, int field, int* year,
                                 int* month, int* day, int* hour,
                                 int* minute, float* second, int* TZFlag)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(!featurePtrPointer) {
        return errorMessage(COD_INVALID, _("The object handle is null"));
    }
    return (*featurePtrPointer)->GetFieldAsDateTime(field, year, month, day,
        hour, minute, second, TZFlag) == 1 ? COD_SUCCESS : COD_GET_FAILED;
}

void ngsFeatireSetId(FeatureH feature, long long id)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(!featurePtrPointer) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return;
    }
    (*featurePtrPointer)->SetFID(id);
}

void ngsFeatureSetGeometry(FeatureH feature, GeometryH geometry)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(!featurePtrPointer) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return;
    }
    (*featurePtrPointer)->SetGeometryDirectly(static_cast<OGRGeometry*>(
                                                   geometry));
}

void ngsFeatureSetFieldInteger(FeatureH feature, int field, int value)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(!featurePtrPointer) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return;
    }
    (*featurePtrPointer)->SetField(field, value);
}

void ngsFeatureSetFieldDouble(FeatureH feature, int field, double value)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(!featurePtrPointer) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return;
    }
    (*featurePtrPointer)->SetField(field, value);
}

void ngsFeatureSetFieldString(FeatureH feature, int field, const char* value)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(!featurePtrPointer) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return;
    }
    (*featurePtrPointer)->SetField(field, value);
}

void ngsFeatureSetFieldDateTime(FeatureH feature, int field, int year, int month,
                                int day, int hour, int minute, float second,
                                int TZFlag)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(!featurePtrPointer) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return;
    }
    (*featurePtrPointer)->SetField(field, year, month, day, hour, minute, second,
                                   TZFlag);
}

GeometryH ngsFeatureCreateGeometry(FeatureH feature)
{
    FeaturePtr* featurePtrPointer = static_cast<FeaturePtr*>(feature);
    if(!featurePtrPointer) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return nullptr;
    }
    OGRGeomFieldDefn *defn = (*featurePtrPointer)->GetGeomFieldDefnRef(0);
    return OGRGeometryFactory::createGeometry(defn->GetType());
}

GeometryH ngsFeatureCreateGeometryFromJson(JsonObjectH geometry)
{
    CPLJSONObject* jsonGeometry = static_cast<CPLJSONObject*>(geometry);
    if(!jsonGeometry) {
        errorMessage(COD_INVALID, _("The object handle is null"));
        return nullptr;
    }
}

/**
 * @brief ngsGeometryFree Free geometry handle. Only usefull if geometry created,
 * but not addet to feature
 * @param geometry Geometry handle
 */
void ngsGeometryFree(GeometryH geometry)
{
    OGRGeometry* geom = static_cast<OGRGeometry*>(geometry);
    if(geom)
        OGRGeometryFactory::destroyGeometry(geom);
}

//------------------------------------------------------------------------------
// Map
//------------------------------------------------------------------------------

/**
 * @brief ngsMapCreate Creates new empty map
 * @param name Map name
 * @param description Map description
 * @param epsg EPSG code
 * @param minX minimum X coordinate
 * @param minY minimum Y coordinate
 * @param maxX maximum X coordinate
 * @param maxY maximum Y coordinate
 * @return 0 if create failed or map identificator.
 */
unsigned char ngsMapCreate(const char* name, const char* description,
                 unsigned short epsg, double minX, double minY,
                 double maxX, double maxY)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore)
        return MapStore::invalidMapId();
    Envelope bound(minX, minY, maxX, maxY);
    return mapStore->createMap(name, description, epsg, bound);
}

/**
 * @brief ngsMapOpen Opens existing map from file
 * @param path Path to map file inside catalog in form ngc://some path/
 * @return 0 if open failed or map id.
 */
unsigned char ngsMapOpen(const char* path)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore)
        return MapStore::invalidMapId();
    CatalogPtr catalog = Catalog::instance();
    ObjectPtr object = catalog->getObject(path);
    MapFile * const mapFile = ngsDynamicCast(MapFile, object);
    return mapStore->openMap(mapFile);
}

/**
 * @brief ngsMapSave Saves map to file
 * @param mapId Map identificator to save
 * @param path Path to store map data
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsMapSave(unsigned char mapId, const char* path)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        return MapStore::invalidMapId();
    }
    CatalogPtr catalog = Catalog::instance();
    ObjectPtr mapFileObject = catalog->getObject(path);
    MapFile * mapFile;
    if(mapFileObject) {
        mapFile = ngsDynamicCast(MapFile, mapFileObject);
    }
    else { // create new MapFile
        const char* newPath = CPLResetExtension(path, MapFile::getExtension());
        const char* saveFolder = CPLGetPath(newPath);
        const char* saveName = CPLGetFilename(newPath);
        ObjectPtr object = catalog->getObject(saveFolder);
        ObjectContainer * const container = ngsDynamicCast(ObjectContainer, object);
        mapFile = new MapFile(container, saveName,
                        CPLFormFilename(object->path(), saveName, nullptr));
        mapFileObject = ObjectPtr(mapFile);
    }

    if(!mapFile || !mapStore->saveMap(mapId, mapFile)) {
        return COD_SAVE_FAILED;
    }

    return COD_SUCCESS;
}

/**
 * @brief ngsMapClose Closes map and free resources
 * @param mapId Map identificator to close
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsMapClose(unsigned char mapId)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore)
        return errorMessage(COD_CLOSE_FAILED, _("MapStore is not initialized"));
    return mapStore->closeMap(mapId) ? COD_SUCCESS : COD_CLOSE_FAILED;
}


/**
 * @brief ngsMapSetSize Sets map size in pixels
 * @param mapId Map identificator received from create or open map functions
 * @param width Output image width
 * @param height Output image height
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsMapSetSize(unsigned char mapId, int width, int height, int isYAxisInverted)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore)
        return errorMessage(COD_CLOSE_FAILED,
                            _("MapStore is not initialized"));
    return mapStore->setMapSize(mapId, width, height, isYAxisInverted == 1 ?
                                    true : false) ? COD_SUCCESS :
                                                    COD_SET_FAILED;
}

/**
 * @brief ngsDrawMap Starts drawing map in specified (in ngsInitMap) extent
 * @param mapId Map identificator received from create or open map functions
 * @param state Draw state (NORMAL, PRESERVED, REDRAW)
 * @param callback Progress function
 * @param callbackData Progress function arguments
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsMapDraw(unsigned char mapId, enum ngsDrawState state,
               ngsProgressFunc callback, void* callbackData)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore)
        return errorMessage(COD_DRAW_FAILED,
                            _("MapStore is not initialized"));
    Progress progress(callback, callbackData);
    return mapStore->drawMap(mapId, state, progress) ? COD_SUCCESS :
                                                       COD_DRAW_FAILED;
}

/**
 * @brief ngsGetMapBackgroundColor Map background color
 * @param mapId Map identificator received from create or open map functions
 * @return map background color struct
 */
ngsRGBA ngsMapGetBackgroundColor(unsigned char mapId)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        errorMessage(COD_GET_FAILED, _("MapStore is not initialized"));
        return {0,0,0,0};
    }
    return mapStore->getMapBackgroundColor(mapId);
}

/**
 * @brief ngsSetMapBackgroundColor Sets map background color
 * @param mapId Map identificator received from create or open map functions
 * @param color Background color
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsMapSetBackgroundColor(unsigned char mapId, ngsRGBA color)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        return errorMessage(COD_SET_FAILED, _("MapStore is not initialized"));
    }
    return mapStore->setMapBackgroundColor(mapId, color) ?
                COD_SUCCESS : COD_SET_FAILED;
}

/**
 * @brief ngsMapSetCenter Sets new map center coordinates
 * @param mapId Map identificator
 * @param x X coordinate
 * @param y Y coordinate
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsMapSetCenter(unsigned char mapId, double x, double y)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        return errorMessage(COD_SET_FAILED,  _("MapStore is not initialized"));
    }
    return mapStore->setMapCenter(mapId, x, y) ? COD_SUCCESS : COD_SET_FAILED;
}

/**
 * @brief ngsMapGetCenter Gets map center for current view (extent)
 * @param mapId Map identificator
 * @return Coordinate structure. If error occurred all coordinates set to 0.0
 */
ngsCoordinate ngsMapGetCenter(unsigned char mapId)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        errorMessage(COD_GET_FAILED, _("MapStore is not initialized"));
        return {0, 0, 0};
    }
    return mapStore->getMapCenter(mapId);
}

/**
 * @brief ngsMapGetCoordinate Geographic coordinates for display position
 * @param mapId Map identificator
 * @param x X position
 * @param y Y position
 * @return Geographic coordinates
 */
ngsCoordinate ngsMapGetCoordinate(unsigned char mapId, double x, double y)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        errorMessage(COD_GET_FAILED, _("MapStore is not initialized"));
        return {0, 0, 0};
    }
    return mapStore->getMapCoordinate(mapId, x, y);
}

/**
 * @brief ngsMapSetScale Sets current map scale
 * @param mapId Map identificator
 * @param scale value to set
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsMapSetScale(unsigned char mapId, double scale)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        return errorMessage(COD_SET_FAILED, _("MapStore is not initialized"));
    }
    return mapStore->setMapScale(mapId, scale);
}

/**
 * @brief ngsMapGetScale Returns current map scale
 * @param mapId Map identificator
 * @return Current map scale or 1
 */
double ngsMapGetScale(unsigned char mapId)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        errorMessage(COD_GET_FAILED, _("MapStore is not initialized"));
        return 1.0;
    }
    return mapStore->getMapScale(mapId);
}

/**
 * @brief ngsMapCreateLayer Creates new layer in map
 * @param mapId Map identificator
 * @param name Layer name
 * @param path Path to map file inside catalog in form ngc://some path/
 * @return Layer Id or -1
 */
int ngsMapCreateLayer(unsigned char mapId, const char* name, const char* path)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        errorMessage(COD_CREATE_FAILED, _("MapStore is not initialized"));
        return NOT_FOUND;
    }

    CatalogPtr catalog = Catalog::instance();
    ObjectPtr object = catalog->getObject(path);
    if(!object) {
        errorMessage(COD_INVALID, _("Source dataset '%s' not found"), path);
        return NOT_FOUND;
    }

    return mapStore->createLayer(mapId, name, object);
}

/**
 * @brief ngsMapLayerReorder Reorders layers in map
 * @param mapId Map identificator
 * @param beforeLayer Before this layer insert movedLayer. May be null. In that
 * case layer will be moved to the end of map
 * @param movedLayer Layer to move
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsMapLayerReorder(unsigned char mapId, LayerH beforeLayer, LayerH movedLayer)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        return errorMessage(COD_INVALID, _("MapStore is not initialized"));
    }
    return mapStore->reorderLayers(mapId, static_cast<Layer*>(beforeLayer),
                                   static_cast<Layer*>(movedLayer)) ?
                COD_SUCCESS : COD_MOVE_FAILED;
}

/**
 * @brief ngsMapSetRotate Sets map rotate
 * @param mapId Map identificator
 * @param dir Rotate direction. May be X, Y or Z
 * @param rotate value to set
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsMapSetRotate(unsigned char mapId, ngsDirection dir, double rotate)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        return errorMessage(COD_INVALID,
                     _("MapStore is not initialized"));
    }
    return mapStore->setMapRotate(mapId, dir, rotate);
}

/**
 * @brief ngsMapGetRotate Returns map rotate value
 * @param mapId Map identificator
 * @param dir Rotate direction. May be X, Y or Z
 * @return rotate value or 0 if error occured
 */
double ngsMapGetRotate(unsigned char mapId, ngsDirection dir)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        errorMessage(COD_GET_FAILED, _("MapStore is not initialized"));
        return 0.0;
    }
    return mapStore->getMapRotate(mapId, dir);
}

/**
 * @brief ngsMapGetDistance Map distance from display length
 * @param mapId Map identificator
 * @param w Width
 * @param h Height
 * @return ngsCoordinate where X distance along x axis and Y along y axis
 */
ngsCoordinate ngsMapGetDistance(unsigned char mapId, double w, double h)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        errorMessage(COD_GET_FAILED, _("MapStore is not initialized"));
        return {0.0, 0.0, 0.0};
    }
    return mapStore->getMapDistance(mapId, w, h);
}

/**
 * @brief ngsMapLayerCount Returns layer count in map
 * @param mapId Map identificator
 * @return Layer count in map
 */
int ngsMapLayerCount(unsigned char mapId)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        errorMessage(COD_GET_FAILED, _("MapStore is not initialized"));
        return 0;
    }
    return static_cast<int>(mapStore->getLayerCount(mapId));
}

/**
 * @brief ngsMapLayerGet Returns map layer handle
 * @param mapId Map identificator
 * @param layerId Layer index
 * @return Layer handle. The caller should not delete it.
 */
LayerH ngsMapLayerGet(unsigned char mapId, int layerId)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        errorMessage(COD_GET_FAILED, _("MapStore is not initialized"));
        return nullptr;
    }
    return mapStore->getLayer(mapId, layerId).get();
}

/**
 * @brief ngsMapLayerDelete Deletes layer from map
 * @param mapId Map identificator
 * @param layer Layer handle get from ngsMapLayerGet() function.
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsMapLayerDelete(unsigned char mapId, LayerH layer)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        return errorMessage(COD_DELETE_FAILED, _("MapStore is not initialized"));
    }
    return mapStore->deleteLayer(mapId, static_cast<Layer*>(layer)) ?
                COD_SUCCESS : COD_DELETE_FAILED;
}

/**
 * @brief ngsMapSetZoomIncrement Add value to calculated zoom level for current
 * map scale. Usually needed if raster tiles content are very small.
 * @param mapId Map identificator
 * @param extraZoom A value to add. May be negative too.
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsMapSetZoomIncrement(unsigned char mapId, char extraZoom)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        return errorMessage(COD_DELETE_FAILED, _("MapStore is not initialized"));
    }
    return mapStore->setZoomIncrement(mapId, extraZoom) ?
                COD_SUCCESS : COD_SET_FAILED;
}

/**
 * @brief ngsMapSetExtentLimits Set limits to prevent pan out of them.
 * @param mapId Map identificator
 * @param minX Minimum X coordinate
 * @param minY Minimum Y coordinate
 * @param maxX Maximum X coordinate
 * @param maxY Maximum Y coordinate
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsMapSetExtentLimits(unsigned char mapId, double minX, double minY,
                                      double maxX, double maxY)
{
    MapStore* const mapStore = MapStore::getInstance();
    if(nullptr == mapStore) {
        return errorMessage(COD_DELETE_FAILED, _("MapStore is not initialized"));
    }

    Envelope extentLimits(minX, minY, maxX, maxY);
    return mapStore->setExtentLimits(mapId, extentLimits) ?
                COD_SUCCESS : COD_SET_FAILED;
}

//------------------------------------------------------------------------------
// Layer
//------------------------------------------------------------------------------

/**
 * @brief ngsLayerGetName Returns layer name
 * @param layer Layer handle
 * @return Layer name
 */
const char* ngsLayerGetName(LayerH layer)
{
    if(nullptr == layer) {
        errorMessage(COD_GET_FAILED, _("Layer pointer is null"));
        return "";
    }
    return (static_cast<Layer*>(layer))->getName();
}

/**
 * @brief ngsLayerSetName Sets new layer name
 * @param layer Layer handle
 * @param name New name
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsLayerSetName(LayerH layer, const char* name)
{
    if(nullptr == layer) {
        return errorMessage(COD_SET_FAILED, _("Layer pointer is null"));
    }
    (static_cast<Layer*>(layer))->setName(name);
    return COD_SUCCESS;
}

/**
 * @brief ngsLayerGetVisible Returns layer visible state
 * @param layer Layer handle
 * @return true if visible or false
 */
char ngsLayerGetVisible(LayerH layer)
{
    if(nullptr == layer) {
        return static_cast<char>(errorMessage(COD_GET_FAILED,
                                              _("Layer pointer is null")));
    }
    return (static_cast<Layer*>(layer))->visible();
}

/**
 * @brief ngsLayerSetVisible Sets layer visibility
 * @param layer Layer handle
 * @param visible
 * @return ngsCode value - COD_SUCCESS if everything is OK
 */
int ngsLayerSetVisible(LayerH layer, char visible)
{
    if(nullptr == layer) {
        return errorMessage(COD_SET_FAILED, _("Layer pointer is null"));
    }
    (static_cast<Layer*>(layer))->setVisible(visible);
    return COD_SUCCESS;
}

/**
 * @brief ngsLayerGetDataSource Layer datasource
 * @param layer Layer handle
 * @return Layer datasource catalog object or NULL
 */
CatalogObjectH ngsLayerGetDataSource(LayerH layer)
{
    if(nullptr == layer) {
        errorMessage(COD_SET_FAILED, _("Layer pointer is null"));
        return nullptr;
    }

    return (static_cast<Layer*>(layer))->datasource().get();
}

int ngsLayerCreateGeometry(unsigned char mapId, LayerH layer)
{
    if (nullptr == layer) {
        return errorMessage( COD_CREATE_FAILED, _("Layer pointer is null"));
    }

    Layer* pLayer = static_cast<Layer*>(layer);
    FeatureClassPtr datasource =
            std::dynamic_pointer_cast<FeatureClass>(pLayer->datasource());
    if (!datasource) {
        return errorMessage(COD_CREATE_FAILED, _("Layer datasource is null"));
    }

    MapStore* const mapStore = MapStore::getInstance();
    if (nullptr == mapStore) {
        return errorMessage(COD_CREATE_FAILED, _("MapStore is not initialized"));
    }

    MapViewPtr mapView = mapStore->getMap(mapId);
    if (!mapView) {
        return errorMessage(COD_CREATE_FAILED, _("MapView pointer is null"));
    }

    OverlayPtr overlay = mapView->getOverlay(MOT_EDIT);
    if (!overlay) {
        return errorMessage(COD_CREATE_FAILED, _("Overlay pointer is null"));
    }

    EditLayerOverlay* editOverlay = ngsDynamicCast(EditLayerOverlay, overlay);
    if (!editOverlay) {
        return errorMessage(COD_CREATE_FAILED, _("Edit overlay pointer is null"));
    }

    const CPLString& layerName = pLayer->getName();
    const OGRwkbGeometryType geometryType = datasource->geometryType();
    const OGRRawPoint mapCenter = mapView->getCenter();

    GeometryPtr geometry =
            EditLayerOverlay::createGeometry(geometryType, mapCenter);
    if (!geometry) {
        return errorMessage(COD_CREATE_FAILED, _("Geometry pointer is null"));
    }

    editOverlay->setVisible(true);
    editOverlay->setLayerName(layerName);
    editOverlay->setGeometry(geometry);

    return COD_SUCCESS;
}

//------------------------------------------------------------------------------
// Overlay
//------------------------------------------------------------------------------

int ngsOverlaySetVisible(
        unsigned char mapId, ngsMapOverlyType typeMask, char visible)
{
    MapStore* const mapStore = MapStore::getInstance();
    if (nullptr == mapStore) {
        return errorMessage(COD_DELETE_FAILED, _("MapStore is not initialized"));
    }
    return mapStore->setOverlayVisible(mapId, typeMask, visible)
            ? COD_SUCCESS : COD_SET_FAILED;
}


/**
 * @brief ngsDisplayGetPosition Display position for geographic coordinates
 * @param mapId Map id
 * @param x X coordinate
 * @param y Y coordinate
 * @return Display position
 */
//ngsPosition ngsDisplayGetPosition(unsigned char mapId, double x, double y)
//{
//    initMapStore();
//    return gMapStore->getDisplayPosition (mapId, x, y);
//}

/**
 * @brief ngsDisplayGetLength Display length from map distance
 * @param mapId Map id
 * @param w Width
 * @param h Height
 * @return ngsPosition where X length along x axis and Y along y axis
 */
//ngsPosition ngsDisplayGetLength(unsigned char mapId, double w, double h)
//{
//    initMapStore();
//    return gMapStore->getDisplayLength (mapId, w, h);
//}


