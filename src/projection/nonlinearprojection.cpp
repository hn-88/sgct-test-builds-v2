/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2025                                                               *
 * For conditions of distribution and use, see copyright notice in LICENSE.md            *
 ****************************************************************************************/

#include <sgct/projection/nonlinearprojection.h>

#include <sgct/callbackdata.h>
#include <sgct/clustermanager.h>
#include <sgct/engine.h>
#include <sgct/format.h>
#include <sgct/log.h>
#include <sgct/offscreenbuffer.h>
#include <sgct/opengl.h>
#include <sgct/profiling.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <sgct/format_compat.h>

namespace sgct {

NonLinearProjection::NonLinearProjection(const Window& parent)
    : _subViewports{
        BaseViewport(parent),
        BaseViewport(parent),
        BaseViewport(parent),
        BaseViewport(parent),
        BaseViewport(parent),
        BaseViewport(parent)
    }
{}

NonLinearProjection::~NonLinearProjection() {
    glDeleteTextures(1, &_textures.cubeMapColor);
    glDeleteTextures(1, &_textures.cubeMapDepth);
    glDeleteTextures(1, &_textures.cubeMapNormals);
    glDeleteTextures(1, &_textures.cubeMapPositions);
    glDeleteTextures(1, &_textures.colorSwap);
    glDeleteTextures(1, &_textures.depthSwap);
    glDeleteTextures(1, &_textures.cubeFaceRight);
    glDeleteTextures(1, &_textures.cubeFaceLeft);
    glDeleteTextures(1, &_textures.cubeFaceBottom);
    glDeleteTextures(1, &_textures.cubeFaceTop);
    glDeleteTextures(1, &_textures.cubeFaceFront);
    glDeleteTextures(1, &_textures.cubeFaceBack);
}

void NonLinearProjection::initialize(unsigned int internalFormat, unsigned int format,
                                     unsigned int type, int nSamples)
{
    initViewports();
    initTextures(internalFormat, format, type);
    initFBO(internalFormat, nSamples);
    initVBO();
    initShaders();
}

void NonLinearProjection::updateFrustums(FrustumMode mode, float nearClip, float farClip)
{
    ZoneScoped;

    if (_subViewports.right.isEnabled()) {
        _subViewports.right.calculateNonLinearFrustum(mode, nearClip, farClip);
    }
    if (_subViewports.left.isEnabled()) {
        _subViewports.left.calculateNonLinearFrustum(mode, nearClip, farClip);
    }
    if (_subViewports.bottom.isEnabled()) {
        _subViewports.bottom.calculateNonLinearFrustum(mode, nearClip, farClip);
    }
    if (_subViewports.top.isEnabled()) {
        _subViewports.top.calculateNonLinearFrustum(mode, nearClip, farClip);
    }
    if (_subViewports.front.isEnabled()) {
        _subViewports.front.calculateNonLinearFrustum(mode, nearClip, farClip);
    }
    if (_subViewports.back.isEnabled()) {
        _subViewports.back.calculateNonLinearFrustum(mode, nearClip, farClip);
    }
}

void NonLinearProjection::setCubemapResolution(int resolution) {
    _cubemapResolution.x = resolution;
    _cubemapResolution.y = resolution;
}

void NonLinearProjection::setInterpolationMode(InterpolationMode im) {
    _interpolationMode = im;
}

void NonLinearProjection::setUseDepthTransformation(bool state) {
    _useDepthTransformation = state;
}

void NonLinearProjection::setStereo(bool state) {
    _isStereo = state;
}

void NonLinearProjection::setUser(User& user) {
    _subViewports.right.setUser(user);
    _subViewports.left.setUser(user);
    _subViewports.bottom.setUser(user);
    _subViewports.top.setUser(user);
    _subViewports.front.setUser(user);
    _subViewports.back.setUser(user);
}

ivec2 NonLinearProjection::cubemapResolution() const {
    return _cubemapResolution;
}

void NonLinearProjection::initTextures(unsigned int internalFormat, unsigned int format,
                                       unsigned int type)
{
    generateCubeMap(_textures.cubeMapColor, internalFormat, format, type);
    Log::Debug(sgctcompat::format(
        "{}x{} color cube map texture (id: {}) generated",
        _cubemapResolution.x, _cubemapResolution.y, _textures.cubeMapColor
    ));

    if (Engine::instance().settings().useDepthTexture) {
        generateCubeMap(
            _textures.cubeMapDepth,
            GL_DEPTH_COMPONENT32,
            GL_DEPTH_COMPONENT,
            GL_FLOAT
        );
        Log::Debug(sgctcompat::format(
            "{}x{} depth cube map texture (id: {}) generated",
            _cubemapResolution.x, _cubemapResolution.y, _textures.cubeMapDepth
        ));

        if (_useDepthTransformation) {
            // generate swap textures
            generateMap(
                _textures.depthSwap,
                GL_DEPTH_COMPONENT32,
                GL_DEPTH_COMPONENT,
                GL_FLOAT
            );
            Log::Debug(sgctcompat::format(
                "{}x{} depth swap map texture (id: {}) generated",
                _cubemapResolution.x, _cubemapResolution.y, _textures.depthSwap
            ));

            generateMap(_textures.colorSwap, internalFormat, format, type);
            Log::Debug(sgctcompat::format(
                "{}x{} color swap map texture (id: {}) generated",
                _cubemapResolution.x, _cubemapResolution.y, _textures.colorSwap
            ));
        }
    }

    if (Engine::instance().settings().useNormalTexture) {
        generateCubeMap(_textures.cubeMapNormals, GL_RGB32F, GL_RGB, GL_FLOAT);
        Log::Debug(sgctcompat::format(
            "{}x{} normal cube map texture (id: {}) generated",
            _cubemapResolution.x, _cubemapResolution.y, _textures.cubeMapNormals
        ));
    }

    if (Engine::instance().settings().usePositionTexture) {
        generateCubeMap(_textures.cubeMapPositions, GL_RGB32F, GL_RGB, GL_FLOAT);
        Log::Debug(sgctcompat::format(
            "{}x{} position cube map texture ({}) generated",
            _cubemapResolution.x, _cubemapResolution.y, _textures.cubeMapPositions
        ));
    }
}

void NonLinearProjection::initFBO(unsigned int internalFormat, int nSamples) {
    _cubeMapFbo = std::make_unique<OffScreenBuffer>(internalFormat);
    _cubeMapFbo->createFBO(_cubemapResolution.x, _cubemapResolution.y, nSamples);
}

void NonLinearProjection::setupViewport(const BaseViewport& vp) const {
    ivec4 vpCoords = ivec4 {
        static_cast<int>(std::floor(vp.position().x * _cubemapResolution.x + 0.5f)),
        static_cast<int>(std::floor(vp.position().y * _cubemapResolution.y + 0.5f)),
        static_cast<int>(std::floor(vp.size().x * _cubemapResolution.x + 0.5f)),
        static_cast<int>(std::floor(vp.size().y * _cubemapResolution.y + 0.5f))
    };

    glViewport(vpCoords.x, vpCoords.y, vpCoords.z, vpCoords.w);
    glScissor(vpCoords.x, vpCoords.y, vpCoords.z, vpCoords.w);
}

void NonLinearProjection::generateMap(unsigned int& texture, unsigned int internalFormat,
                                      unsigned int format, unsigned int type)
{
    glDeleteTextures(1, &texture);

    GLint maxMapRes = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxMapRes);
    if (_cubemapResolution.x > maxMapRes) {
        Log::Error(sgctcompat::format(
            "Requested size is too big ({} > {})", _cubemapResolution.x, maxMapRes
        ));
    }
    if (_cubemapResolution.y > maxMapRes) {
        Log::Error(sgctcompat::format(
            "Requested size is too big ({} > {})", _cubemapResolution.y, maxMapRes
        ));
    }


    // set up texture target
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    // Disable mipmaps
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        internalFormat,
        _cubemapResolution.x,
        _cubemapResolution.y,
        0,
        format,
        type,
        nullptr
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void NonLinearProjection::generateCubeMap(unsigned int& texture,
                                          unsigned int internalFormat,
                                          unsigned int format, unsigned int type)
{
    glDeleteTextures(1, &texture);

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    GLint maxCubeMapRes = 0;
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maxCubeMapRes);
    if (_cubemapResolution.x > maxCubeMapRes) {
        _cubemapResolution.x = maxCubeMapRes;
        Log::Debug(sgctcompat::format("Cubemap size set to max size: {}", maxCubeMapRes));
    }
    if (_cubemapResolution.y > maxCubeMapRes) {
        _cubemapResolution.y = maxCubeMapRes;
        Log::Debug(sgctcompat::format("Cubemap size set to max size: {}", maxCubeMapRes));
    }


    // set up texture target
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture);

    // Disable mipmaps
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    glTexImage2D(
        GL_TEXTURE_CUBE_MAP_POSITIVE_X,
        0,
        internalFormat,
        _cubemapResolution.x,
        _cubemapResolution.y,
        0,
        format,
        type,
        nullptr
    );
    glTexImage2D(
        GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        0,
        internalFormat,
        _cubemapResolution.x,
        _cubemapResolution.y,
        0,
        format,
        type,
        nullptr
    );
    glTexImage2D(
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
        0,
        internalFormat,
        _cubemapResolution.x,
        _cubemapResolution.y,
        0,
        format,
        type,
        nullptr
    );
    glTexImage2D(
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        0,
        internalFormat,
        _cubemapResolution.x,
        _cubemapResolution.y,
        0,
        format,
        type,
        nullptr
    );
    glTexImage2D(
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
        0,
        internalFormat,
        _cubemapResolution.x,
        _cubemapResolution.y,
        0,
        format,
        type,
        nullptr
    );
    glTexImage2D(
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
        0,
        internalFormat,
        _cubemapResolution.x,
        _cubemapResolution.y,
        0,
        format,
        type,
        nullptr
    );

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
}

void NonLinearProjection::attachTextures(int face) const {
    if (Engine::instance().settings().useDepthTexture) {
        _cubeMapFbo->attachDepthTexture(_textures.depthSwap);
        _cubeMapFbo->attachColorTexture(_textures.colorSwap, GL_COLOR_ATTACHMENT0);
    }
    else {
        _cubeMapFbo->attachCubeMapTexture(
            _textures.cubeMapColor,
            face,
            GL_COLOR_ATTACHMENT0
        );
    }

    if (Engine::instance().settings().useNormalTexture) {
        _cubeMapFbo->attachCubeMapTexture(
            _textures.cubeMapNormals,
            face,
            GL_COLOR_ATTACHMENT1
        );
    }

    if (Engine::instance().settings().usePositionTexture) {
        _cubeMapFbo->attachCubeMapTexture(
            _textures.cubeMapPositions,
            face,
            GL_COLOR_ATTACHMENT2
        );
    }
}

void NonLinearProjection::blitCubeFace(int face) const {
    // copy AA-buffer to "regular"/non-AA buffer
    _cubeMapFbo->bindBlit();
    attachTextures(face);
    _cubeMapFbo->blit();
}

void NonLinearProjection::renderCubeFace(const BaseViewport& vp, int idx,
                                         FrustumMode mode) const
{
    if (!vp.isEnabled()) {
        return;
    }

    _cubeMapFbo->bind();
    if (!_cubeMapFbo->isMultiSampled()) {
        attachTextures(idx);
    }

    const RenderData renderData = {
        vp.window(),
        vp,
        mode,
        ClusterManager::instance().sceneTransform(),
        vp.projection(mode).viewMatrix(),
        vp.projection(mode).projectionMatrix(),
        vp.projection(mode).viewProjectionMatrix() *
            ClusterManager::instance().sceneTransform(),
        _cubemapResolution
    };
    glLineWidth(1.f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glDepthFunc(GL_LESS);

    glEnable(GL_SCISSOR_TEST);
    setupViewport(vp);

    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_SCISSOR_TEST);
    Engine::instance().drawFunction()(renderData);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // blit MSAA fbo to texture
    if (_cubeMapFbo->isMultiSampled()) {
        blitCubeFace(idx);
    }
}

} // namespace sgct
