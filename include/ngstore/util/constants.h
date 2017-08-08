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

#ifndef NGSCONSTANTS_H
#define NGSCONSTANTS_H

#include <cmath>

constexpr int NOT_FOUND = -1;

/*

#define DEFAULT_TILE_SIZE 256

// Common
#define NAME_FIELD_LIMIT 64
#define ALIAS_FIELD_LIMIT 255
#define DESCRIPTION_FIELD_LIMIT 1024

// Draw
#define NOTIFY_PERCENT 0.1

*/

constexpr double DEG2RAD = M_PI / 180.0;

constexpr double TOLERANCE_PX = 7.0;

#endif // NGSCONSTANTS_H
