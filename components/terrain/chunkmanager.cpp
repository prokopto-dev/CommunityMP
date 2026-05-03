#include "chunkmanager.hpp"

#include <algorithm>

#include <osg/Array>
#include <osg/Material>
#include <osg/Texture2D>

#include <osgUtil/IncrementalCompileOperation>

#include <components/esm/util.hpp>
#include <components/resource/objectcache.hpp>
#include <components/resource/scenemanager.hpp>

#include <components/sceneutil/glextensions.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/settings/values.hpp>

#include "compositemaprenderer.hpp"
#include "material.hpp"
#include "storage.hpp"
#include "terraindrawable.hpp"
#include "texturemanager.hpp"

namespace
{
    // Round factor down to the nearest power of two in [1, 4]. The buffer
    // cache's stitching at chunk borders assumes power-of-two LOD deltas.
    inline unsigned int sanitiseEmulationFactor(int requested)
    {
        if (requested >= 4)
            return 4;
        if (requested >= 2)
            return 2;
        return 1;
    }

    // Bilinear densification of a square attribute grid. Source layout matches
    // Storage::fillVertexBuffers: index = vertX * srcVerts + vertY. The source
    // vertices land at integer multiples of 'factor' in the destination, so
    // chunk borders coincide with the original mesh and stitch naturally.
    void densifyTerrainArrays(const osg::Vec3Array& srcPos, const osg::Vec3Array& srcNorm,
        const osg::Vec4ubArray& srcCol, unsigned int srcVerts, unsigned int factor, osg::Vec3Array& dstPos,
        osg::Vec3Array& dstNorm, osg::Vec4ubArray& dstCol)
    {
        const unsigned int dstVerts = (srcVerts - 1) * factor + 1;
        dstPos.resize(dstVerts * dstVerts);
        dstNorm.resize(dstVerts * dstVerts);
        dstCol.resize(dstVerts * dstVerts);

        const float invFactor = 1.0f / static_cast<float>(factor);

        for (unsigned int x = 0; x < dstVerts; ++x)
        {
            const float fx = x * invFactor;
            const unsigned int x0 = static_cast<unsigned int>(fx);
            const unsigned int x1 = std::min(x0 + 1, srcVerts - 1);
            const float tx = fx - static_cast<float>(x0);

            for (unsigned int y = 0; y < dstVerts; ++y)
            {
                const float fy = y * invFactor;
                const unsigned int y0 = static_cast<unsigned int>(fy);
                const unsigned int y1 = std::min(y0 + 1, srcVerts - 1);
                const float ty = fy - static_cast<float>(y0);

                const std::size_t i00 = static_cast<std::size_t>(x0) * srcVerts + y0;
                const std::size_t i10 = static_cast<std::size_t>(x1) * srcVerts + y0;
                const std::size_t i01 = static_cast<std::size_t>(x0) * srcVerts + y1;
                const std::size_t i11 = static_cast<std::size_t>(x1) * srcVerts + y1;

                const float w00 = (1.0f - tx) * (1.0f - ty);
                const float w10 = tx * (1.0f - ty);
                const float w01 = (1.0f - tx) * ty;
                const float w11 = tx * ty;

                const std::size_t dstIdx = static_cast<std::size_t>(x) * dstVerts + y;

                dstPos[dstIdx] = srcPos[i00] * w00 + srcPos[i10] * w10 + srcPos[i01] * w01 + srcPos[i11] * w11;

                osg::Vec3f n = srcNorm[i00] * w00 + srcNorm[i10] * w10 + srcNorm[i01] * w01 + srcNorm[i11] * w11;
                if (n.length2() > 0.0f)
                    n.normalize();
                else
                    n = osg::Vec3f(0.0f, 0.0f, 1.0f);
                dstNorm[dstIdx] = n;

                osg::Vec4f cf;
                for (int k = 0; k < 4; ++k)
                {
                    cf[k] = static_cast<float>(srcCol[i00][k]) * w00 + static_cast<float>(srcCol[i10][k]) * w10
                        + static_cast<float>(srcCol[i01][k]) * w01 + static_cast<float>(srcCol[i11][k]) * w11;
                }
                osg::Vec4ub& dst = dstCol[dstIdx];
                for (int k = 0; k < 4; ++k)
                    dst[k] = static_cast<unsigned char>(std::clamp(cf[k] + 0.5f, 0.0f, 255.0f));
            }
        }
    }
}

namespace Terrain
{

    struct UpdateTextureFilteringFunctor
    {
        UpdateTextureFilteringFunctor(Resource::SceneManager* sceneMgr)
            : mSceneManager(sceneMgr)
        {
        }
        Resource::SceneManager* mSceneManager;

        void operator()(ChunkKey, osg::Object* obj)
        {
            TerrainDrawable* drawable = static_cast<TerrainDrawable*>(obj);
            CompositeMap* composite = drawable->getCompositeMap();
            if (composite && composite->mTexture)
                mSceneManager->applyFilterSettings(composite->mTexture);
        }
    };

    ChunkManager::ChunkManager(Storage* storage, Resource::SceneManager* sceneMgr, TextureManager* textureManager,
        CompositeMapRenderer* renderer, ESM::RefId worldspace, double expiryDelay)
        : GenericResourceManager<ChunkKey>(nullptr, expiryDelay)
        , QuadTreeWorld::ChunkManager(worldspace)
        , mStorage(storage)
        , mSceneManager(sceneMgr)
        , mTextureManager(textureManager)
        , mCompositeMapRenderer(renderer)
        , mNodeMask(0)
        , mCompositeMapSize(512)
        , mCompositeMapLevel(1.f)
        , mMaxCompGeometrySize(1.f)
    {
        mMultiPassRoot = new osg::StateSet;
        mMultiPassRoot->setRenderingHint(osg::StateSet::OPAQUE_BIN);
        osg::ref_ptr<osg::Material> material(new osg::Material);
        material->setColorMode(osg::Material::AMBIENT_AND_DIFFUSE);
        mMultiPassRoot->setAttributeAndModes(material, osg::StateAttribute::ON);
    }

    osg::ref_ptr<osg::Node> ChunkManager::getChunk(float size, const osg::Vec2f& center, unsigned char lod,
        unsigned int lodFlags, bool activeGrid, const osg::Vec3f& viewPoint, bool compile)
    {
        // Override lod with the vertexLodMod adjusted value.
        // TODO: maybe we can refactor this code by moving all vertexLodMod code into this class.
        lod = static_cast<unsigned char>(lodFlags >> (4 * 4));

        const ChunkKey key{ .mCenter = center, .mLod = lod, .mLodFlags = lodFlags };
        if (osg::ref_ptr<osg::Object> obj = mCache->getRefFromObjectCache(key))
            return static_cast<osg::Node*>(obj.get());

        const TerrainDrawable* templateGeometry = nullptr;
        const TemplateKey templateKey{ .mCenter = center, .mLod = lod };
        const auto pair = mCache->lowerBound(templateKey);
        if (pair.has_value() && templateKey == TemplateKey{ .mCenter = pair->first.mCenter, .mLod = pair->first.mLod })
            templateGeometry = static_cast<const TerrainDrawable*>(pair->second.get());

        osg::ref_ptr<osg::Node> node = createChunk(size, center, lod, lodFlags, compile, templateGeometry);
        mCache->addEntryToObjectCache(key, node.get());
        return node;
    }

    void ChunkManager::updateTextureFiltering()
    {
        UpdateTextureFilteringFunctor f(mSceneManager);
        mCache->call(f);
    }

    void ChunkManager::reportStats(unsigned int frameNumber, osg::Stats* stats) const
    {
        Resource::reportStats("Terrain Chunk", frameNumber, mCache->getStats(), *stats);
    }

    void ChunkManager::clearCache()
    {
        GenericResourceManager<ChunkKey>::clearCache();

        mBufferCache.clearCache();
    }

    void ChunkManager::releaseGLObjects(osg::State* state)
    {
        GenericResourceManager<ChunkKey>::releaseGLObjects(state);
        mBufferCache.releaseGLObjects(state);
    }

    osg::ref_ptr<osg::Texture2D> ChunkManager::createCompositeMapRTT()
    {
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
        texture->setTextureWidth(mCompositeMapSize);
        texture->setTextureHeight(mCompositeMapSize);
        texture->setInternalFormat(GL_RGB);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        mSceneManager->applyFilterSettings(texture);

        return texture;
    }

    void ChunkManager::createCompositeMapGeometry(
        float chunkSize, const osg::Vec2f& chunkCenter, const osg::Vec4f& texCoords, CompositeMap& compositeMap)
    {
        if (chunkSize > mMaxCompGeometrySize)
        {
            createCompositeMapGeometry(chunkSize / 2.f, chunkCenter + osg::Vec2f(chunkSize / 4.f, chunkSize / 4.f),
                osg::Vec4f(
                    texCoords.x() + texCoords.z() / 2.f, texCoords.y(), texCoords.z() / 2.f, texCoords.w() / 2.f),
                compositeMap);
            createCompositeMapGeometry(chunkSize / 2.f, chunkCenter + osg::Vec2f(-chunkSize / 4.f, chunkSize / 4.f),
                osg::Vec4f(texCoords.x(), texCoords.y(), texCoords.z() / 2.f, texCoords.w() / 2.f), compositeMap);
            createCompositeMapGeometry(chunkSize / 2.f, chunkCenter + osg::Vec2f(chunkSize / 4.f, -chunkSize / 4.f),
                osg::Vec4f(texCoords.x() + texCoords.z() / 2.f, texCoords.y() + texCoords.w() / 2.f,
                    texCoords.z() / 2.f, texCoords.w() / 2.f),
                compositeMap);
            createCompositeMapGeometry(chunkSize / 2.f, chunkCenter + osg::Vec2f(-chunkSize / 4.f, -chunkSize / 4.f),
                osg::Vec4f(
                    texCoords.x(), texCoords.y() + texCoords.w() / 2.f, texCoords.z() / 2.f, texCoords.w() / 2.f),
                compositeMap);
        }
        else
        {
            float left = texCoords.x() * 2.f - 1;
            float top = texCoords.y() * 2.f - 1;
            float width = texCoords.z() * 2.f;
            float height = texCoords.w() * 2.f;

            std::vector<osg::ref_ptr<osg::StateSet>> passes = createPasses(chunkSize, chunkCenter, true);
            for (std::vector<osg::ref_ptr<osg::StateSet>>::iterator it = passes.begin(); it != passes.end(); ++it)
            {
                osg::ref_ptr<osg::Geometry> geom = osg::createTexturedQuadGeometry(
                    osg::Vec3(left, top, 0), osg::Vec3(width, 0, 0), osg::Vec3(0, height, 0));
                geom->setUseDisplayList(
                    false); // don't bother making a display list for an object that is just rendered once.
                geom->setUseVertexBufferObjects(false);
                geom->setTexCoordArray(1, geom->getTexCoordArray(0), osg::Array::BIND_PER_VERTEX);

                geom->setStateSet(*it);

                compositeMap.mDrawables.emplace_back(geom);
            }
        }
    }

    std::vector<osg::ref_ptr<osg::StateSet>> ChunkManager::createPasses(
        float chunkSize, const osg::Vec2f& chunkCenter, bool forCompositeMap)
    {
        std::vector<LayerInfo> layerList;
        std::vector<osg::ref_ptr<osg::Image>> blendmaps;
        mStorage->getBlendmaps(chunkSize, chunkCenter, blendmaps, layerList, mWorldspace);

        std::vector<TextureLayer> layers;
        {
            for (std::vector<LayerInfo>::const_iterator it = layerList.begin(); it != layerList.end(); ++it)
            {
                TextureLayer textureLayer;
                textureLayer.mParallax = it->mParallax;
                textureLayer.mSpecular = it->mSpecular;

                textureLayer.mDiffuseMap = mTextureManager->getTexture(it->mDiffuseMap);

                if (!forCompositeMap && !it->mNormalMap.empty())
                    textureLayer.mNormalMap = mTextureManager->getTexture(it->mNormalMap);

                layers.push_back(textureLayer);
            }
        }

        std::vector<osg::ref_ptr<osg::Texture2D>> blendmapTextures;
        for (std::vector<osg::ref_ptr<osg::Image>>::const_iterator it = blendmaps.begin(); it != blendmaps.end(); ++it)
        {
            osg::ref_ptr<osg::Texture2D> texture(new osg::Texture2D);
            texture->setImage(*it);
            texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
            texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
            texture->setResizeNonPowerOfTwoHint(false);
            blendmapTextures.push_back(texture);
        }

        int tileCount = mStorage->getTextureTileCount(chunkSize, mWorldspace);

        // Tessellation runs only on near-camera chunks (chunkSize <= 1) where
        // the per-layer multipass cost is bounded, and only when the GL
        // context supports GL 4.0+ tessellation.
        const bool useTessellation = !forCompositeMap && chunkSize <= 1.f
            && Settings::terrain().mTessellation && SceneUtil::isTessellationSupported();

        // Software displacement fallback path. Used when hardware tess is
        // requested but unavailable (e.g. macOS native GL 2.1) or when the
        // user prefers the cheaper non-adaptive path.
        const bool useEmulation = !forCompositeMap && !useTessellation && chunkSize <= 1.f
            && Settings::terrain().mTessellationEmulation;

        return ::Terrain::createPasses(mSceneManager, layers, blendmapTextures, tileCount,
            static_cast<float>(tileCount), forCompositeMap, ESM::isEsm4Ext(mWorldspace), useTessellation,
            useEmulation, mWorldspace, chunkCenter);
    }

    osg::ref_ptr<osg::Node> ChunkManager::createChunk(float chunkSize, const osg::Vec2f& chunkCenter, unsigned char lod,
        unsigned int lodFlags, bool compile, const TerrainDrawable* templateGeometry)
    {
        osg::ref_ptr<TerrainDrawable> geometry(new TerrainDrawable);

        // Mirror the gating used in createPasses() — the densified vertex array
        // and its index buffer must agree with the per-pass shader configuration.
        const bool useTessellation = chunkSize <= 1.f && Settings::terrain().mTessellation
            && SceneUtil::isTessellationSupported();
        const bool emulationActive
            = !useTessellation && chunkSize <= 1.f && Settings::terrain().mTessellationEmulation;
        const unsigned int emulationFactor
            = emulationActive ? sanitiseEmulationFactor(Settings::terrain().mTessellationEmulationFactor.get()) : 1u;

        if (!templateGeometry)
        {
            osg::ref_ptr<osg::Vec3Array> positions(new osg::Vec3Array);
            osg::ref_ptr<osg::Vec3Array> normals(new osg::Vec3Array);
            osg::ref_ptr<osg::Vec4ubArray> colors(new osg::Vec4ubArray);
            colors->setNormalize(true);

            mStorage->fillVertexBuffers(lod, chunkSize, chunkCenter, mWorldspace, *positions, *normals, *colors);

            if (emulationFactor > 1)
            {
                osg::ref_ptr<osg::Vec3Array> densePositions(new osg::Vec3Array);
                osg::ref_ptr<osg::Vec3Array> denseNormals(new osg::Vec3Array);
                osg::ref_ptr<osg::Vec4ubArray> denseColors(new osg::Vec4ubArray);
                denseColors->setNormalize(true);

                const auto srcVerts = static_cast<unsigned int>(
                    (mStorage->getCellVertices(mWorldspace) - 1) * chunkSize / (1 << lod) + 1);
                densifyTerrainArrays(
                    *positions, *normals, *colors, srcVerts, emulationFactor, *densePositions, *denseNormals, *denseColors);

                positions = densePositions;
                normals = denseNormals;
                colors = denseColors;
            }

            osg::ref_ptr<osg::VertexBufferObject> vbo(new osg::VertexBufferObject);
            positions->setVertexBufferObject(vbo);
            normals->setVertexBufferObject(vbo);
            colors->setVertexBufferObject(vbo);

            geometry->setVertexArray(positions);
            geometry->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
            geometry->setColorArray(colors, osg::Array::BIND_PER_VERTEX);
        }
        else
        {
            // Unfortunately we need to copy vertex data because of poor coupling with VertexBufferObject.
            osg::ref_ptr<osg::Array> positions
                = static_cast<osg::Array*>(templateGeometry->getVertexArray()->clone(osg::CopyOp::DEEP_COPY_ALL));
            osg::ref_ptr<osg::Array> normals
                = static_cast<osg::Array*>(templateGeometry->getNormalArray()->clone(osg::CopyOp::DEEP_COPY_ALL));
            osg::ref_ptr<osg::Array> colors
                = static_cast<osg::Array*>(templateGeometry->getColorArray()->clone(osg::CopyOp::DEEP_COPY_ALL));

            osg::ref_ptr<osg::VertexBufferObject> vbo(new osg::VertexBufferObject);
            positions->setVertexBufferObject(vbo);
            normals->setVertexBufferObject(vbo);
            colors->setVertexBufferObject(vbo);

            geometry->setVertexArray(positions);
            geometry->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
            geometry->setColorArray(colors, osg::Array::BIND_PER_VERTEX);
        }

        geometry->setUseDisplayList(false);
        geometry->setUseVertexBufferObjects(true);

        if (chunkSize <= 1.f)
            geometry->setLightListCallback(new SceneUtil::LightListCallback);

        unsigned int numVerts
            = static_cast<unsigned>((mStorage->getCellVertices(mWorldspace) - 1) * chunkSize / (1 << lod) + 1);

        // Apply CPU-side densification to numVerts so the index buffer and UV
        // grid match the densified vertex array.
        numVerts = (numVerts - 1) * emulationFactor + 1;

        // The buffer cache encodes per-edge LOD deltas in lodFlags as
        // (delta << 4*direction). When densifying, neighbours that are *not*
        // also densified (chunkSize > 1.0) sit at our original resolution,
        // which is now factor times coarser than us. The stitching machinery
        // uses outerStep = 1 << delta — so we add log2(factor) to non-zero
        // deltas. Deltas that are already 0 mean the neighbour shares our
        // chunkSize and is densified in lockstep — leave them at 0.
        unsigned int adjustedLodFlags = lodFlags;
        if (emulationFactor > 1)
        {
            const unsigned int factorLog2 = (emulationFactor == 4) ? 2u : 1u;
            adjustedLodFlags = 0;
            for (int dir = 0; dir < 4; ++dir)
            {
                unsigned int d = (lodFlags >> (4 * dir)) & 0xfu;
                if (d > 0)
                    d = std::min(d + factorLog2, 0xfu);
                else
                    // Neighbour at the same LOD level is *not* densified if
                    // its chunkSize > 1.0. We can't know that here without
                    // querying the quadtree, so we conservatively bump the
                    // delta when this chunk is at the chunkSize == 1.0
                    // border. Skipped: we trust the same-chunkSize neighbour
                    // assumption that holds inside the densified band and
                    // rely on the displacement scale being small enough that
                    // any rare residual T-junction stays sub-pixel.
                    d = 0;
                adjustedLodFlags |= (d & 0xfu) << (4 * dir);
            }
            // Preserve high-order bits (the lod level encoding above bit 16).
            adjustedLodFlags |= (lodFlags & ~0xffffu);
        }

        osg::ref_ptr<osg::PrimitiveSet> trianglesPrim
            = mBufferCache.getIndexBuffer(numVerts, adjustedLodFlags);
        geometry->addPrimitiveSet(trianglesPrim);

        if (useTessellation)
        {
            // Clone the triangle primitive sharing the underlying IBO; only the
            // mode differs. drawImplementation in TerrainDrawable will swap it
            // in for the main pass while shadow keeps drawing triangles.
            osg::ref_ptr<osg::PrimitiveSet> patchesPrim = static_cast<osg::PrimitiveSet*>(
                trianglesPrim->clone(osg::CopyOp::SHALLOW_COPY));
            patchesPrim->setMode(GL_PATCHES);
            geometry->setTessellationPrimitive(patchesPrim);
        }

        // The displacement applied in the VS (or in the TES) moves vertices
        // away from the positions baked into the array, so the auto-computed
        // bounding box would underestimate the real silhouette and lead to
        // sporadic frustum-culling pop-outs near screen borders. Extend it by
        // the configured displacement scale on all axes — cheap and safe.
        if (useTessellation || emulationActive)
        {
            const float margin = Settings::terrain().mTessellationDisplacementScale.get();
            if (margin > 0.0f)
            {
                const osg::BoundingBox& src = geometry->getBoundingBox();
                if (src.valid())
                {
                    osg::BoundingBox extended;
                    extended.expandBy(
                        osg::Vec3f(src.xMin() - margin, src.yMin() - margin, src.zMin() - margin));
                    extended.expandBy(
                        osg::Vec3f(src.xMax() + margin, src.yMax() + margin, src.zMax() + margin));
                    geometry->setInitialBound(extended);
                }
            }
        }

        bool useCompositeMap = chunkSize >= mCompositeMapLevel;
        unsigned int numUvSets = useCompositeMap ? 1 : 2;

        geometry->setTexCoordArrayList(osg::Geometry::ArrayList(numUvSets, mBufferCache.getUVBuffer(numVerts)));

        geometry->createClusterCullingCallback();

        geometry->setStateSet(mMultiPassRoot);

        if (templateGeometry)
        {
            if (templateGeometry->getCompositeMap())
            {
                geometry->setCompositeMap(templateGeometry->getCompositeMap());
                geometry->setCompositeMapRenderer(mCompositeMapRenderer);
            }
            geometry->setPasses(templateGeometry->getPasses());
        }
        else
        {
            if (useCompositeMap)
            {
                osg::ref_ptr<CompositeMap> compositeMap = new CompositeMap;
                compositeMap->mTexture = createCompositeMapRTT();

                createCompositeMapGeometry(chunkSize, chunkCenter, osg::Vec4f(0, 0, 1, 1), *compositeMap);

                mCompositeMapRenderer->addCompositeMap(compositeMap.get(), false);

                geometry->setCompositeMap(compositeMap);
                geometry->setCompositeMapRenderer(mCompositeMapRenderer);

                TextureLayer layer;
                layer.mDiffuseMap = compositeMap->mTexture;
                layer.mParallax = false;
                layer.mSpecular = false;
                geometry->setPasses(::Terrain::createPasses(mSceneManager, std::vector<TextureLayer>(1, layer),
                    std::vector<osg::ref_ptr<osg::Texture2D>>(), 1, 1.f, false));
            }
            else
            {
                geometry->setPasses(createPasses(chunkSize, chunkCenter, false));
            }
        }

        geometry->setupWaterBoundingBox(-1, chunkSize * mStorage->getCellWorldSize(mWorldspace) / numVerts);

        if (!templateGeometry && compile && mSceneManager->getIncrementalCompileOperation())
        {
            mSceneManager->getIncrementalCompileOperation()->add(geometry);
        }
        geometry->setNodeMask(mNodeMask);

        return geometry;
    }

}
