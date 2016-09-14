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
#ifndef API_PRIV_H
#define API_PRIV_H

#include "api.h"
#include "ogr_geometry.h"

#include <memory>
#include <cmath>
#include <limits>


using namespace std;

/**
  * useful functions
  */
inline int ngsRGBA2HEX(const ngsRGBA& color) {
    return ((color.R & 0xff) << 24) + ((color.G & 0xff) << 16) +
            ((color.B & 0xff) << 8) + (color.A & 0xff);
}

inline ngsRGBA ngsHEX2RGBA(int color) {
    ngsRGBA out;
    out.R = (color >> 24) & 0xff;
    out.G = (color >> 16) & 0xff;
    out.B = (color >> 8) & 0xff;
    out.A = (color) & 0xff;
    return out;
}

#define ngsDynamicCast(type, shared) dynamic_cast<type*>(shared.get ())
#define ngsStaticCast(type, shared) static_cast<type*>(shared.get ())

// http://stackoverflow.com/a/15012792
inline bool isEqual(double val1, double val2) {return fabs(val1 - val2) <=
            numeric_limits<double>::epsilon(); };

#define ARRAY_SIZE(array) (sizeof((array))/sizeof((array[0])))

// TODO: use gettext or something same to translate messages

#endif // API_PRIV_H
