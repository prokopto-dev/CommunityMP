#include "material.hpp"

#include <osg/BlendFunc>
#include <osg/Capability>
#include <osg/Depth>
#include <osg/Fog>
#include <osg/Image>
#include <osg/TexMat>
#include <osg/Texture2D>

#include <cmath>

#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/depth.hpp>
#include <components/sceneutil/patchparameter.hpp>
#include <components/sceneutil/util.hpp>
#include <components/settings/values.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/stereo/stereomanager.hpp>

#include <mutex>

namespace
{
    class BlendmapTexMat
    {
    public:
        static const osg::ref_ptr<osg::TexMat>& value(const int blendmapScale)
        {
            static BlendmapTexMat instance;
            return instance.get(static_cast<float>(blendmapScale));
        }

        const osg::ref_ptr<osg::TexMat>& get(const float blendmapScale)
        {
            const std::lock_guard<std::mutex> lock(mMutex);
            auto texMat = mTexMatMap.find(blendmapScale);
            if (texMat == mTexMatMap.end())
            {
                osg::Matrixf matrix;
                float scale = (blendmapScale / (blendmapScale + 1.f));
                matrix.preMultTranslate(osg::Vec3f(0.5f, 0.5f, 0.f));
                matrix.preMultScale(osg::Vec3f(scale, scale, 1.f));
                matrix.preMultTranslate(osg::Vec3f(-0.5f, -0.5f, 0.f));
                // We need to nudge the blendmap to look like vanilla.
                // This causes visible seams unless the blendmap's resolution is doubled, but Vanilla also doubles the
                // blendmap, apparently.
                matrix.preMultTranslate(osg::Vec3f(1.0f / blendmapScale / 4.0f, -1.0f / blendmapScale / 4.0f, 0.f));

                texMat = mTexMatMap.emplace(blendmapScale, new osg::TexMat(matrix)).first;
            }
            return texMat->second;
        }

    private:
        std::mutex mMutex;
        std::map<float, osg::ref_ptr<osg::TexMat>> mTexMatMap;
    };

    class LayerTexMat
    {
    public:
        static const osg::ref_ptr<osg::TexMat>& value(const float layerTileSize)
        {
            static LayerTexMat instance;
            return instance.get(layerTileSize);
        }

        const osg::ref_ptr<osg::TexMat>& get(const float layerTileSize)
        {
            const std::lock_guard<std::mutex> lock(mMutex);
            auto texMat = mTexMatMap.find(layerTileSize);
            if (texMat == mTexMatMap.end())
            {
                texMat = mTexMatMap
                             .insert(std::make_pair(layerTileSize,
                                 new osg::TexMat(osg::Matrix::scale(osg::Vec3f(layerTileSize, layerTileSize, 1.f)))))
                             .first;
            }
            return texMat->second;
        }

    private:
        std::mutex mMutex;
        std::map<float, osg::ref_ptr<osg::TexMat>> mTexMatMap;
    };

    class EqualDepth
    {
    public:
        static const osg::ref_ptr<osg::Depth>& value()
        {
            static EqualDepth instance;
            return instance.mValue;
        }

    private:
        osg::ref_ptr<osg::Depth> mValue;

        EqualDepth()
            : mValue(new SceneUtil::AutoDepth)
        {
            mValue->setFunction(osg::Depth::EQUAL);
        }
    };

    class LequalDepth
    {
    public:
        static const osg::ref_ptr<osg::Depth>& value()
        {
            static LequalDepth instance;
            return instance.mValue;
        }

    private:
        osg::ref_ptr<osg::Depth> mValue;

        LequalDepth()
            : mValue(new SceneUtil::AutoDepth(osg::Depth::LEQUAL))
        {
        }
    };

    class BlendFuncFirst
    {
    public:
        static const osg::ref_ptr<osg::BlendFunc>& value()
        {
            static BlendFuncFirst instance;
            return instance.mValue;
        }

    private:
        osg::ref_ptr<osg::BlendFunc> mValue;

        BlendFuncFirst()
            : mValue(new osg::BlendFunc(osg::BlendFunc::SRC_ALPHA, osg::BlendFunc::ZERO))
        {
        }
    };

    class BlendFunc
    {
    public:
        static const osg::ref_ptr<osg::BlendFunc>& value()
        {
            static BlendFunc instance;
            return instance.mValue;
        }

    private:
        osg::ref_ptr<osg::BlendFunc> mValue;

        BlendFunc()
            : mValue(new osg::BlendFunc(osg::BlendFunc::SRC_ALPHA, osg::BlendFunc::ONE))
        {
        }
    };

    // Singleton procedural bump heightmap. Baked once on first request from
    // a 4-octave value-noise FBM into a 256x256 R8 texture. The result is
    // shared between all terrain passes so the GPU only ever sees it once.
    class ProceduralBumpHeightmap
    {
    public:
        static const osg::ref_ptr<osg::Texture2D>& value()
        {
            static ProceduralBumpHeightmap instance;
            return instance.mTexture;
        }

    private:
        osg::ref_ptr<osg::Texture2D> mTexture;

        ProceduralBumpHeightmap()
        {
            constexpr int kSize = 256;
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->allocateImage(kSize, kSize, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE);
            image->setInternalTextureFormat(GL_LUMINANCE);
            unsigned char* data = image->data();

            auto hash21 = [](float x, float y) {
                float fx = x * 123.34f;
                float fy = y * 456.21f;
                fx = fx - std::floor(fx);
                fy = fy - std::floor(fy);
                float dot = fx * (fx + 45.32f) + fy * (fy + 45.32f);
                fx += dot;
                fy += dot;
                float r = (fx - std::floor(fx)) * (fy - std::floor(fy));
                return r - std::floor(r);
            };
            auto smoothstep = [](float t) { return t * t * (3.0f - 2.0f * t); };

            auto vnoise = [&](float x, float y) {
                float ix = std::floor(x), iy = std::floor(y);
                float fx = x - ix, fy = y - iy;
                float a = hash21(ix, iy);
                float b = hash21(ix + 1, iy);
                float c = hash21(ix, iy + 1);
                float d = hash21(ix + 1, iy + 1);
                float ux = smoothstep(fx), uy = smoothstep(fy);
                return ((a * (1 - ux) + b * ux) * (1 - uy) + (c * (1 - ux) + d * ux) * uy);
            };

            // Tileable FBM: hash on float coordinates wrapped into [0, kSize).
            for (int y = 0; y < kSize; ++y)
            {
                for (int x = 0; x < kSize; ++x)
                {
                    float fx = static_cast<float>(x);
                    float fy = static_cast<float>(y);
                    float v = 0.0f;
                    float amp = 0.5f;
                    float freq = 1.0f / 32.0f;
                    for (int o = 0; o < 4; ++o)
                    {
                        v += amp * vnoise(fx * freq, fy * freq);
                        freq *= 2.07f;
                        amp *= 0.5f;
                    }
                    v = std::clamp(v, 0.0f, 1.0f);
                    data[y * kSize + x] = static_cast<unsigned char>(v * 255.0f);
                }
            }

            mTexture = new osg::Texture2D(image);
            mTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
            mTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
            mTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
            mTexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
            mTexture->setMaxAnisotropy(4.0f);
        }
    };

    class UniformCollection
    {
    public:
        static const UniformCollection& value()
        {
            static UniformCollection instance;
            return instance;
        }

        osg::ref_ptr<osg::Uniform> mDiffuseMap;
        osg::ref_ptr<osg::Uniform> mBlendMap;
        osg::ref_ptr<osg::Uniform> mNormalMap;
        osg::ref_ptr<osg::Uniform> mColorMode;
        osg::ref_ptr<osg::Uniform> mProceduralBumpMap;

        UniformCollection()
            : mDiffuseMap(new osg::Uniform("diffuseMap", 0))
            , mBlendMap(new osg::Uniform("blendMap", 1))
            , mNormalMap(new osg::Uniform("normalMap", 2))
            , mColorMode(new osg::Uniform("colorMode", 2))
            , mProceduralBumpMap(new osg::Uniform("terrainProceduralBumpMap", 3))
        {
        }
    };
}

namespace Terrain
{
    std::vector<osg::ref_ptr<osg::StateSet>> createPasses(Resource::SceneManager* sceneManager,
        const std::vector<TextureLayer>& layers, const std::vector<osg::ref_ptr<osg::Texture2D>>& blendmaps,
        int blendmapScale, float layerTileSize, bool isComposite, bool esm4terrain, bool useTessellation,
        bool useDisplacementEmulation)
    {
        auto& shaderManager = sceneManager->getShaderManager();
        std::vector<osg::ref_ptr<osg::StateSet>> passes;

        // Compositing rasterises into a 2D RTT and is not amenable to a 3-stage
        // tessellation pipeline; force-disable tess in that case.
        if (isComposite)
        {
            useTessellation = false;
            useDisplacementEmulation = false;
        }
        // Hardware tess and software emulation are mutually exclusive — the
        // hardware path already does displacement in the TES.
        if (useTessellation)
            useDisplacementEmulation = false;

        unsigned int blendmapIndex = 0;
        for (std::vector<TextureLayer>::const_iterator it = layers.begin(); it != layers.end(); ++it)
        {
            bool firstLayer = (it == layers.begin());

            osg::ref_ptr<osg::StateSet> stateset(new osg::StateSet);

            if (!blendmaps.empty())
            {
                stateset->setMode(GL_BLEND, osg::StateAttribute::ON);
                if (sceneManager->getSupportsNormalsRT())
                    stateset->setAttribute(new osg::Disablei(GL_BLEND, 1));
                stateset->setRenderBinDetails(firstLayer ? 0 : 1, "RenderBin");
                if (!firstLayer)
                {
                    stateset->setAttributeAndModes(BlendFunc::value(), osg::StateAttribute::ON);
                    stateset->setAttributeAndModes(EqualDepth::value(), osg::StateAttribute::ON);
                }
                else
                {
                    stateset->setAttributeAndModes(BlendFuncFirst::value(), osg::StateAttribute::ON);
                    stateset->setAttributeAndModes(LequalDepth::value(), osg::StateAttribute::ON);
                }
            }

            stateset->setTextureAttributeAndModes(0, it->mDiffuseMap);
            stateset->addUniform(UniformCollection::value().mDiffuseMap);

            if (layerTileSize != 1.f)
                stateset->setTextureAttributeAndModes(0, LayerTexMat::value(layerTileSize), osg::StateAttribute::ON);

            if (!blendmaps.empty())
            {
                osg::ref_ptr<osg::Texture2D> blendmap = blendmaps.at(blendmapIndex++);

                stateset->setTextureAttributeAndModes(1, blendmap.get());
                if (!esm4terrain)
                    stateset->setTextureAttributeAndModes(1, BlendmapTexMat::value(blendmapScale));
                stateset->addUniform(UniformCollection::value().mBlendMap);
            }
            if (isComposite)
            {
                stateset->setAttributeAndModes(
                    shaderManager.getProgram("terrain_composite", { { "blendMap", !blendmaps.empty() ? "1" : "0" } }));
            }
            else
            {
                bool parallax = it->mNormalMap && it->mParallax;
                bool reconstructNormalZ = false;

                if (it->mNormalMap)
                {
                    stateset->setTextureAttributeAndModes(2, it->mNormalMap);
                    stateset->addUniform(UniformCollection::value().mNormalMap);

                    // Special handling for red-green normal maps (e.g. BC5 or R8G8).
                    const osg::Image* image = it->mNormalMap->getImage(0);
                    if (image)
                    {
                        switch (SceneUtil::computeUnsizedPixelFormat(image->getPixelFormat()))
                        {
                            case GL_RG:
                            case GL_RG_INTEGER:
                            {
                                reconstructNormalZ = true;
                                parallax = false;
                            }
                        }
                    }
                }

                Shader::ShaderManager::DefineMap defineMap;
                defineMap["normalMap"] = (it->mNormalMap) ? "1" : "0";
                defineMap["blendMap"] = (!blendmaps.empty()) ? "1" : "0";
                defineMap["specularMap"] = it->mSpecular ? "1" : "0";
                defineMap["parallax"] = parallax ? "1" : "0";
                defineMap["writeNormals"] = (it == layers.end() - 1) ? "1" : "0";
                defineMap["reconstructNormalZ"] = reconstructNormalZ ? "1" : "0";
                defineMap["terrainDisplacement"] = useDisplacementEmulation ? "1" : "0";
                const bool useProceduralBump
                    = !isComposite && Settings::terrain().mProceduralBump.get();
                defineMap["terrainProceduralBump"] = useProceduralBump ? "1" : "0";
                Stereo::shaderStereoDefines(defineMap);

                if (useProceduralBump)
                {
                    stateset->setTextureAttributeAndModes(3, ProceduralBumpHeightmap::value());
                    stateset->addUniform(UniformCollection::value().mProceduralBumpMap);
                    stateset->addUniform(new osg::Uniform("terrainProceduralBumpStrength",
                        Settings::terrain().mProceduralBumpStrength.get()));
                    stateset->addUniform(new osg::Uniform("terrainProceduralBumpScale",
                        Settings::terrain().mProceduralBumpScale.get()));
                }

                if (useTessellation)
                {
                    // Force per-pixel lighting under tess; per-vertex lighting
                    // would be applied at coarse pre-subdivision vertices and
                    // the displaced surface would look flat-shaded.
                    defineMap["forcePPL"] = "1";
                    stateset->setAttributeAndModes(
                        shaderManager.getTessellationProgram("core/terrain", defineMap));
                    stateset->setAttributeAndModes(new SceneUtil::PatchParameter(3));
                    stateset->addUniform(new osg::Uniform(
                        "terrainTessMaxLevel",
                        static_cast<float>(Settings::terrain().mTessellationMaxLevel.get())));
                    stateset->addUniform(new osg::Uniform(
                        "terrainTessDisplacementScale",
                        Settings::terrain().mTessellationDisplacementScale.get()));
                    stateset->addUniform(new osg::Uniform(
                        "terrainTessViewDistance",
                        Settings::terrain().mTessellationViewDistance.get()));
                }
                else
                {
                    stateset->setAttributeAndModes(shaderManager.getProgram("terrain", defineMap));
                    if (useDisplacementEmulation)
                    {
                        stateset->addUniform(new osg::Uniform(
                            "terrainTessDisplacementScale",
                            Settings::terrain().mTessellationDisplacementScale.get()));
                    }
                }
                stateset->addUniform(UniformCollection::value().mColorMode);
            }

            passes.push_back(stateset);
        }
        return passes;
    }

}
