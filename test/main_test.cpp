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

#include "gtest/gtest.h"
#include "api.h"
#include "version.h"

TEST(BasicTests, TestVersions) {
    EXPECT_EQ(NGM_VERSION_NUM, ngsGetVersion());
    EXPECT_STREQ(NGM_VERSION, ngsGetVersionString());
}

TEST(BasicTests, TestCreate) {

}


TEST(BasicTests, TestOpen) {
    EXPECT_EQ(ngsInit(nullptr, nullptr), ngsErrorCodes::PATH_NOT_SPECIFIED);
}

