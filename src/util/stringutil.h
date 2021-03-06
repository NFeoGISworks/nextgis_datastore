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
#ifndef NGSSTRINGUTIL_H
#define NGSSTRINGUTIL_H

#include "cpl_string.h"

namespace ngs {

int constexpr length(const char* str)
{
    return *str ? 1 + length(str + 1) : 0;
}

CPLString stripUnicode(const CPLString &str, const char replaceChar = 'x');
CPLString normalize(const CPLString &str, const CPLString &lang = "");

}
#endif // NGSSTRINGUTIL_H
