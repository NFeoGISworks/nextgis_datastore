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
#include "tile.h"

namespace ngs {

GlTile::GlTile(unsigned short tileSize, const TileItem& tileItem) : GlObject(),
    m_tileItem(tileItem),
    m_id(0),
    m_filled(false)
{
    m_image.setImage(nullptr, tileSize, tileSize);
    m_image.setSmooth(true);

    m_sceneMatrix.ortho(tileItem.env.minX(), tileItem.env.maxX(),
                        tileItem.env.minY(), tileItem.env.maxY(),
                        DEFAULT_BOUNDS.minX(), DEFAULT_BOUNDS.maxX());
    m_invViewMatrix.ortho(0, tileSize, 0, tileSize, -1.0, 1.0);


    Envelope env = tileItem.env;
    env.move(tileItem.tile.crossExtent * DEFAULT_BOUNDS.width(), 0.0);
    m_tile.addVertex(static_cast<float>(env.minX()));
    m_tile.addVertex(static_cast<float>(env.minY()));
    m_tile.addVertex(0.0f);
    m_tile.addVertex(0.0f);
    m_tile.addVertex(0.0f);
    m_tile.addIndex(0);
    m_tile.addVertex(static_cast<float>(env.minX()));
    m_tile.addVertex(static_cast<float>(env.maxY()));
    m_tile.addVertex(0.0f);
    m_tile.addVertex(0.0f);
    m_tile.addVertex(1.0f);
    m_tile.addIndex(1);
    m_tile.addVertex(static_cast<float>(env.maxX()));
    m_tile.addVertex(static_cast<float>(env.maxY()));
    m_tile.addVertex(0.0f);
    m_tile.addVertex(1.0f);
    m_tile.addVertex(1.0f);
    m_tile.addIndex(2);
    m_tile.addVertex(static_cast<float>(env.maxX()));
    m_tile.addVertex(static_cast<float>(env.minY()));
    m_tile.addVertex(0.0f);
    m_tile.addVertex(1.0f);
    m_tile.addVertex(0.0f);
    m_tile.addIndex(0);
    m_tile.addIndex(2);
    m_tile.addIndex(3);
}


void GlTile::bind()
{
    if (m_bound)
        return;

    ngsCheckGLError(glGenFramebuffers(1, &m_id));
    // Set up the FBO with one texture attachment
    ngsCheckGLError(glBindFramebuffer(GL_FRAMEBUFFER, m_id));
    m_image.bind();
    ngsCheckGLError(glFramebufferTexture2D(GL_FRAMEBUFFER,
                                           GL_COLOR_ATTACHMENT0,
                                           GL_TEXTURE_2D, m_image.id(), 0));
    ngsCheckGLError(glCheckFramebufferStatus(GL_FRAMEBUFFER));

    m_tile.bind();
}

void GlTile::rebind() const
{
    ngsCheckGLError(glBindFramebuffer(GL_FRAMEBUFFER, m_id));
    m_image.rebind();
    ngsCheckGLError(glFramebufferTexture2D(GL_FRAMEBUFFER,
                                           GL_COLOR_ATTACHMENT0,
                                           GL_TEXTURE_2D, m_image.id(), 0));
    m_tile.rebind();
}

void GlTile::destroy()
{
    if (m_bound) {
        ngsCheckGLError(glDeleteFramebuffers(1, &m_id));
    }
    m_image.destroy();
    m_tile.destroy();
}

} // namespace ngs
