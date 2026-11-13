#include "render/BloomRenderer.hpp"
#include "render/FrameInterpolationRenderer.hpp"
#include "render/GlStateGuard.hpp"
#include "render/PostProcessPipeline.hpp"
#include "render/PostProcessRenderer.hpp"
#include "render/SmaaRenderer.hpp"
#include "render/SgsrRenderer.hpp"
#include "shaders/PostProcessShaders.hpp"
#include "shaders/aa/SmaaShader.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Hook.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCEGLViewProtocol.hpp>
#include <Geode/modify/CCNode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/platform/cplatform.h>
#include <Geode/ui/PopupManager.hpp>
#ifdef GEODE_IS_ANDROID
    #include <EGL/egl.h>
    #include <EGL/eglext.h>
#endif
#ifdef GEODE_IS_WINDOWS
    #include <Geode/modify/CCEGLView.hpp>
#endif
#ifdef GEODE_IS_MOBILE
    #include <Geode/modify/AppDelegate.hpp>
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <optional>
#include <string_view>

using namespace geode::prelude;

namespace {

    enum class AntiAliasingMethod {
        Off,
        Fxaa,
        Ssaa150,
        Ssaa200,
        Ssaa300,
        SmaaHigh,
        SmaaUltra,
    };

    float ssaaFactor(AntiAliasingMethod method) {
        switch (method) {
            case AntiAliasingMethod::Ssaa150: return 1.5f;
            case AntiAliasingMethod::Ssaa200: return 2.f;
            case AntiAliasingMethod::Ssaa300: return 3.f;
            default: return 1.f;
        }
    }

    enum class UpscaleMethod {
        Nearest,
        Bilinear,
        Fsr,
        Sgsr,
    };

    struct PostProcessConfig {
        AntiAliasingMethod aa = AntiAliasingMethod::Off;
        bool cas = false;
        bool bloom = false;
        float bloomThreshold = 0.7f;
        float bloomIntensity = 0.3f;
        float bloomRadius = 8.f;
        bool grayscale = false;
        bool pixelate = false;
        bool invert = false;
        bool flip = false;
        bool dithering = false;
        bool vhs = false;
        bool crt = false;
        UpscaleMethod upscaling = UpscaleMethod::Nearest;

        bool operator==(PostProcessConfig const&) const = default;

        int effectCount() const {
            // SSAA is performed by the final presentation/downsample pass, not
            // by renderEffects(). Counting it here can leave an unpresented
            // capture when the combined render scale resolves to native size.
            auto const hasPostProcessAa =
                aa == AntiAliasingMethod::Fxaa || aa == AntiAliasingMethod::SmaaHigh ||
                aa == AntiAliasingMethod::SmaaUltra;
            return hasPostProcessAa + cas + bloom + grayscale + pixelate + invert + flip +
                dithering + vhs + crt;
        }
    };

    struct PostProcessKey {
        PostProcessConfig config;
        GLsizei width = 0;
        GLsizei height = 0;
        bool needsPresentation = false;
        bool needsDownsample = false;

        bool operator==(PostProcessKey const&) const = default;
    };

    struct PostProcessFailureKey {
        PostProcessKey postProcess;
        GLuint framebuffer = 0;
        std::array<GLint, 4> viewport = {};

        bool operator==(PostProcessFailureKey const&) const = default;
    };

    std::atomic<AntiAliasingMethod> g_antiAliasingMethod = AntiAliasingMethod::SmaaHigh;
    std::atomic<UpscaleMethod> g_upscaleMethod = UpscaleMethod::Nearest;
    // Zero selects the Custom base (1.0); positive values are the base
    // source-resolution ratios for the FSR 1 quality presets.
    std::atomic<float> g_fsrPresetScale = 0.f;
    std::atomic<float> g_sgsrPresetScale = 0.f;
    std::atomic<float> g_renderScale = 1.f;
    std::atomic<bool> g_casEnabled = false;
    std::atomic<float> g_casSharpness = 0.f;
    std::atomic<float> g_fsrReconstructionBlend = 1.f;
    std::atomic<bool> g_bloomEnabled = false;
    std::atomic<float> g_bloomThreshold = 0.7f;
    std::atomic<float> g_bloomIntensity = 0.3f;
    std::atomic<float> g_bloomRadius = 8.f;
    std::atomic<bool> g_grayscaleEnabled = false;
    std::atomic<bool> g_pixelateEnabled = false;
    std::atomic<bool> g_invertEnabled = false;
    std::atomic<bool> g_flipEnabled = false;
    std::atomic<bool> g_ditheringEnabled = false;
    std::atomic<bool> g_vhsEnabled = false;
    std::atomic<bool> g_crtEnabled = false;
    std::atomic<bool> g_modEnabled = true;
    std::atomic<bool> g_funEnabled = true;
    std::atomic<bool> g_fullSceneEnabled = false;
    geode::Hook* g_sceneVisitHook = nullptr;
    geode::Hook* g_gameLayerVisitHook = nullptr;
    std::atomic<bool> g_temporarilyDisabled = false;
    std::atomic<bool> g_disablePopupShown = false;
    bv::render::PostProcessPipeline g_postProcessPipeline;
    bv::render::PostProcessRenderer g_fxaaRenderer;
    bv::render::PostProcessRenderer g_ssaaRenderer;
    bv::render::PostProcessRenderer g_renderScaleRenderer;
    bv::render::PostProcessRenderer g_fsrRenderer;
    bv::render::SgsrRenderer g_sgsrRenderer;
    bv::render::PostProcessRenderer g_casRenderer;
    bv::render::BloomRenderer g_bloomRenderer;
    bv::render::PostProcessRenderer g_grayscaleRenderer;
    bv::render::PostProcessRenderer g_pixelateRenderer;
    bv::render::PostProcessRenderer g_invertRenderer;
    bv::render::PostProcessRenderer g_flipRenderer;
    bv::render::PostProcessRenderer g_ditheringRenderer;
    bv::render::PostProcessRenderer g_vhsRenderer;
    bv::render::PostProcessRenderer g_crtRenderer;
    bv::render::SmaaRenderer g_smaaRenderer;
    std::optional<PostProcessKey> g_preparedPostProcessKey;
    std::optional<PostProcessFailureKey> g_failedPostProcessKey;
    bool g_isSceneVisitActive = false;
    bool g_isSceneCaptureActive = false;
    GLsizei g_captureWidth = 0;
    GLsizei g_captureHeight = 0;
    GLsizei g_captureFullWidth = 0;
    GLsizei g_captureFullHeight = 0;

    void applyCaptureViewport() {
        if (g_isSceneCaptureActive) {
            glViewport(0, 0, g_captureWidth, g_captureHeight);
        }
    }

    void scaleCaptureScissor(float& x, float& y, float& w, float& h) {
        if (g_isSceneCaptureActive && g_captureFullWidth > 0 && g_captureWidth != g_captureFullWidth) {
            auto const scaleX = static_cast<float>(g_captureWidth) / g_captureFullWidth;
            auto const scaleY = static_cast<float>(g_captureHeight) / g_captureFullHeight;
            x *= scaleX;
            y *= scaleY;
            w *= scaleX;
            h *= scaleY;
        }
    }

    void resetRenderResources() {
        g_postProcessPipeline.reset();
        g_fxaaRenderer.reset();
        g_ssaaRenderer.reset();
        g_renderScaleRenderer.reset();
        g_fsrRenderer.reset();
        g_sgsrRenderer.reset();
        g_casRenderer.reset();
        g_bloomRenderer.reset();
        g_grayscaleRenderer.reset();
        g_pixelateRenderer.reset();
        g_invertRenderer.reset();
        g_flipRenderer.reset();
        g_ditheringRenderer.reset();
        g_vhsRenderer.reset();
        g_crtRenderer.reset();
        g_smaaRenderer.reset();
        g_preparedPostProcessKey.reset();
        g_failedPostProcessKey.reset();
    }

    void teardownDisabledPostProcess() {
        if ((!g_modEnabled.load(std::memory_order_relaxed) ||
             g_temporarilyDisabled.load(std::memory_order_relaxed)) &&
            (g_preparedPostProcessKey || g_failedPostProcessKey)) {
            resetRenderResources();
        }
    }

    void showSsaaWarning() {
        auto popup = PopupManager::get().quickPopup(
            "SSAA Warning",
            "SSAA renders the game at a higher resolution before downsampling, "
            "which is extremely demanding. "
            "Use it only for showcasing or on top-of-the-line GPUs!!!",
            "Ok"
        );
        popup->show();
    }

    void updateAntiAliasingMethod(std::string_view value) {
        if (value == "SMAA High") {
            g_antiAliasingMethod.store(AntiAliasingMethod::SmaaHigh, std::memory_order_relaxed);
            return;
        }
        if (value == "SMAA Ultra") {
            g_antiAliasingMethod.store(AntiAliasingMethod::SmaaUltra, std::memory_order_relaxed);
            return;
        }
        if (value == "SSAA 1.5x") {
            g_antiAliasingMethod.store(AntiAliasingMethod::Ssaa150, std::memory_order_relaxed);
            return;
        }
        if (value == "SSAA 2x") {
            g_antiAliasingMethod.store(AntiAliasingMethod::Ssaa200, std::memory_order_relaxed);
            return;
        }
        if (value == "SSAA 3x") {
            g_antiAliasingMethod.store(AntiAliasingMethod::Ssaa300, std::memory_order_relaxed);
            return;
        }
        if (value == "FXAA") {
            g_antiAliasingMethod.store(AntiAliasingMethod::Fxaa, std::memory_order_relaxed);
            return;
        }

        if (value != "Off") {
            log::warn("Unknown AA method '{}', disabling AA", value);
        }
        g_antiAliasingMethod.store(AntiAliasingMethod::Off, std::memory_order_relaxed);
    }

    void updateUpscaleMethod(std::string_view value) {
        if (value == "Bilinear") {
            g_upscaleMethod.store(UpscaleMethod::Bilinear, std::memory_order_relaxed);
            return;
        }
        if (value == "FSR 1") {
            g_upscaleMethod.store(UpscaleMethod::Fsr, std::memory_order_relaxed);
            return;
        }
        if (value == "SGSR 1") {
            g_upscaleMethod.store(UpscaleMethod::Sgsr, std::memory_order_relaxed);
            return;
        }

        if (value != "Nearest") {
            log::warn("Unknown upscale method '{}', using nearest neighbour", value);
        }
        g_upscaleMethod.store(UpscaleMethod::Nearest, std::memory_order_relaxed);
    }

    void updateFsrQualityPreset(std::string_view value) {
        float scale = 0.f;
        if (value == "Ultra Quality") scale = 0.77f;
        else if (value == "Quality") scale = 0.67f;
        else if (value == "Balanced") scale = 0.59f;
        else if (value == "Performance") scale = 0.50f;
        else if (value == "Ultra Performance") scale = 0.33f;
        else if (value != "Custom") {
            log::warn("Unknown FSR 1 quality preset '{}', using Custom", value);
        }
        g_fsrPresetScale.store(scale, std::memory_order_relaxed);
    }

    void updateSgsrQualityPreset(std::string_view value) {
        float scale = 0.f;
        if (value == "Ultra Quality") scale = 0.77f;
        else if (value == "Quality") scale = 0.67f;
        else if (value == "Balanced") scale = 0.59f;
        else if (value == "Performance") scale = 0.50f;
        else if (value == "Ultra Performance") scale = 0.33f;
        else if (value != "Custom") {
            log::warn("Unknown SGSR 1 quality preset '{}', using Custom", value);
        }
        g_sgsrPresetScale.store(scale, std::memory_order_relaxed);
    }

    void bindBoolSetting(char const* name, std::atomic<bool>& value) {
        value.store(Mod::get()->getSettingValue<bool>(name), std::memory_order_relaxed);
        listenForSettingChanges<bool>(name, [&value](bool updated) {
            value.store(updated, std::memory_order_relaxed);
        });
    }

    void bindDoubleSetting(
        char const* name, std::atomic<float>& value,
        float minimum, float maximum, float fallback
    ) {
        auto update = [&value, minimum, maximum, fallback](double input) {
            auto const converted = static_cast<float>(input);
            auto const safeValue = std::isfinite(converted) ?
                std::clamp(converted, minimum, maximum) : fallback;
            value.store(safeValue, std::memory_order_relaxed);
        };
        update(Mod::get()->getSettingValue<double>(name));
        listenForSettingChanges<double>(name, [update](double input) {
            update(input);
        });
    }

    void bindStringSetting(char const* name, void (*update)(std::string_view)) {
        update(Mod::get()->getSettingValue<std::string>(name));
        listenForSettingChanges<std::string>(name, [update](std::string value) {
            update(value);
        });
    }

    GLsizei scaledDimension(GLsizei output, float scale) {
        return static_cast<GLsizei>(
            std::max<long>(1, std::lround(static_cast<double>(output) * scale))
        );
    }


    PostProcessConfig selectedPostProcessConfig() {
        bool const fun = g_funEnabled.load(std::memory_order_relaxed);
        return {
            g_antiAliasingMethod.load(std::memory_order_relaxed),
            g_casEnabled.load(std::memory_order_relaxed),
            fun && g_bloomEnabled.load(std::memory_order_relaxed),
            g_bloomThreshold.load(std::memory_order_relaxed),
            g_bloomIntensity.load(std::memory_order_relaxed),
            g_bloomRadius.load(std::memory_order_relaxed),
            fun && g_grayscaleEnabled.load(std::memory_order_relaxed),
            fun && g_pixelateEnabled.load(std::memory_order_relaxed),
            fun && g_invertEnabled.load(std::memory_order_relaxed),
            fun && g_flipEnabled.load(std::memory_order_relaxed),
            fun && g_ditheringEnabled.load(std::memory_order_relaxed),
            fun && g_vhsEnabled.load(std::memory_order_relaxed),
            fun && g_crtEnabled.load(std::memory_order_relaxed),
            g_upscaleMethod.load(std::memory_order_relaxed),
        };
    }

    bool preparePostProcess(PostProcessKey const& key) {
        if (g_preparedPostProcessKey == key) {
            return true;
        }

        bv::render::GlStateGuard prepareState;
        auto const& config = key.config;
        bool prepared = g_postProcessPipeline.prepare(key.width, key.height);
        switch (config.aa) {
            case AntiAliasingMethod::Fxaa:
                g_smaaRenderer.reset();
                prepared = prepared &&
                    g_fxaaRenderer.prepare(bv::shaders::kFxaaShader, key.width, key.height);
                break;
            case AntiAliasingMethod::Ssaa150:
            case AntiAliasingMethod::Ssaa200:
            case AntiAliasingMethod::Ssaa300:
                g_fxaaRenderer.reset();
                g_smaaRenderer.reset();
                break;
            case AntiAliasingMethod::SmaaHigh:
                g_fxaaRenderer.reset();
                prepared = prepared &&
                    g_smaaRenderer.prepare(
                        bv::shaders::smaa::kSmaaHighShaderSet, key.width, key.height
                    );
                break;
            case AntiAliasingMethod::SmaaUltra:
                g_fxaaRenderer.reset();
                prepared = prepared &&
                    g_smaaRenderer.prepare(
                        bv::shaders::smaa::kSmaaUltraShaderSet, key.width, key.height
                    );
                break;
            case AntiAliasingMethod::Off:
                g_fxaaRenderer.reset();
                g_smaaRenderer.reset();
                break;
        }
        if (prepared && config.cas) {
            prepared = g_casRenderer.prepare(bv::shaders::kCasShader, key.width, key.height);
        }
        else if (!config.cas) {
            g_casRenderer.reset();
        }
        if (prepared && config.bloom) {
            prepared = g_bloomRenderer.prepare(key.width, key.height);
            if (prepared) {
                g_bloomRenderer.setParams(
                    config.bloomThreshold, config.bloomIntensity, config.bloomRadius
                );
            }
        }
        else if (!config.bloom) {
            g_bloomRenderer.reset();
        }

        struct Effect {
            bool enabled;
            bv::render::PostProcessRenderer* renderer;
            bv::render::PostProcessShader const* shader;
        };

        std::array<Effect, 7> const effects{{
            {config.grayscale, &g_grayscaleRenderer, &bv::shaders::kGrayscaleShader},
            {config.pixelate, &g_pixelateRenderer, &bv::shaders::kPixelateShader},
            {config.invert, &g_invertRenderer, &bv::shaders::kInvertShader},
            {config.flip, &g_flipRenderer, &bv::shaders::kFlipShader},
            {config.dithering, &g_ditheringRenderer, &bv::shaders::kDitheringShader},
            {config.vhs, &g_vhsRenderer, &bv::shaders::kVhsShader},
            {config.crt, &g_crtRenderer, &bv::shaders::kCrtShader},
        }};
        for (auto const& effect : effects) {
            if (prepared && effect.enabled) {
                prepared = effect.renderer->prepare(*effect.shader, key.width, key.height);
            }
            else if (!effect.enabled) {
                effect.renderer->reset();
            }
        }

        if (prepared && key.needsPresentation) {
            if (key.needsDownsample) {
                g_fsrRenderer.reset();
                g_renderScaleRenderer.reset();
                g_ssaaRenderer.reset();
                auto const& ssaaShader = config.aa == AntiAliasingMethod::Ssaa300 ?
                    bv::shaders::kSsaa3xShader :
                    bv::shaders::kSsaaShader;
                prepared = g_ssaaRenderer.prepare(ssaaShader, key.width, key.height);
            }
            else if (config.upscaling == UpscaleMethod::Fsr) {
                g_renderScaleRenderer.reset();
                g_ssaaRenderer.reset();
                prepared = g_fsrRenderer.prepare(bv::shaders::kFsrShader, key.width, key.height);
            }
            else {
                g_fsrRenderer.reset();
                g_ssaaRenderer.reset();
                prepared = g_renderScaleRenderer.prepare(
                    bv::shaders::kRenderScaleShader, key.width, key.height
                );
            }
        }
        else if (!key.needsPresentation) {
            g_renderScaleRenderer.reset();
            g_fsrRenderer.reset();
            g_ssaaRenderer.reset();
        }

        if (prepared) {
            g_preparedPostProcessKey = key;
        }
        return prepared;
    }

    void renderEffects(
        PostProcessConfig const& config, GLfloat casSharpness,
        bv::render::RenderTarget const& frameTarget, bool needsPresentation
    ) {
        auto remaining = config.effectCount();
        assert(remaining > 0);

        auto runStage = [&](auto&& render) {
            auto const terminal = --remaining == 0;
            auto const writeToCaller = terminal && !needsPresentation;
            auto const target = writeToCaller ? frameTarget : g_postProcessPipeline.nextTarget();
            render(target);
            if (!writeToCaller) {
                g_postProcessPipeline.advanceStage();
            }
        };
        auto runPost = [&](bv::render::PostProcessRenderer& renderer, GLfloat scalar = 0.f) {
            runStage([&](bv::render::RenderTarget const& target) {
                glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
                glViewport(target.x, target.y, target.width, target.height);
                renderer.apply(g_postProcessPipeline.currentTexture(), scalar);
            });
        };

        switch (config.aa) {
            case AntiAliasingMethod::Fxaa: runPost(g_fxaaRenderer); break;
            case AntiAliasingMethod::SmaaHigh:
            case AntiAliasingMethod::SmaaUltra:
                runStage([&](bv::render::RenderTarget const& target) {
                    g_smaaRenderer.apply(g_postProcessPipeline.currentTexture(), target);
                });
                break;
            case AntiAliasingMethod::Ssaa150:
            case AntiAliasingMethod::Ssaa200:
            case AntiAliasingMethod::Ssaa300:
            case AntiAliasingMethod::Off: break;
        }
        if (config.cas) {
            runPost(g_casRenderer, casSharpness);
        }
        if (config.bloom) {
            runStage([&](bv::render::RenderTarget const& target) {
                g_bloomRenderer.apply(g_postProcessPipeline.currentTexture(), target);
            });
        }
        if (config.grayscale) {
            runPost(g_grayscaleRenderer);
        }
        if (config.pixelate) {
            runPost(g_pixelateRenderer);
        }
        if (config.invert) {
            runPost(g_invertRenderer);
        }
        if (config.flip) {
            runPost(g_flipRenderer);
        }
        if (config.dithering) {
            runPost(g_ditheringRenderer);
        }
        if (config.vhs) {
            static auto const clockStart = std::chrono::steady_clock::now();
            auto const elapsed =
                std::chrono::duration<GLfloat>(std::chrono::steady_clock::now() - clockStart).count();
            runPost(g_vhsRenderer, elapsed);
        }
        if (config.crt) {
            runPost(g_crtRenderer);
        }
    }

    void renderSceneWithPostProcessing(auto&& visitNext) {
        auto const config = selectedPostProcessConfig();
        GLint callerFramebuffer = 0;
        std::array<GLint, 4> callerViewport = {};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &callerFramebuffer);
        glGetIntegerv(GL_VIEWPORT, callerViewport.data());
        auto const width = static_cast<GLsizei>(callerViewport[2]);
        auto const height = static_cast<GLsizei>(callerViewport[3]);
        if (width <= 0 || height <= 0) {
            visitNext();
            return;
        }

        auto const effectCount = config.effectCount();
        auto const configuredScale = g_renderScale.load(std::memory_order_relaxed);
        auto const fsrPresetScale = config.upscaling == UpscaleMethod::Fsr ?
            g_fsrPresetScale.load(std::memory_order_relaxed) : 0.f;
        auto const sgsrPresetScale = config.upscaling == UpscaleMethod::Sgsr ?
            g_sgsrPresetScale.load(std::memory_order_relaxed) : 0.f;
        auto const presetScale = fsrPresetScale > 0.f ? fsrPresetScale : sgsrPresetScale;
        // The quality preset selects the base input resolution. Render Scale is
        // an independent multiplier on top of that base (100% = unchanged).
        // FSR 1 is an upscaler, so never ask EASU to downscale above native.
        auto const requestedScale =
            (presetScale > 0.f ? presetScale : 1.f) * configuredScale;
        auto const usesSpatialUpscaler =
            config.upscaling == UpscaleMethod::Fsr || config.upscaling == UpscaleMethod::Sgsr;
        auto const renderScale = std::clamp(requestedScale, 0.25f, usesSpatialUpscaler ? 1.f : 1.2f);
        // A selected FSR quality preset owns the base resolution. SSAA's
        // supersampling multiplier would otherwise push it above native and
        // route presentation through the SSAA downsampler instead of EASU.
        auto const aaScale = presetScale > 0.f ? 1.f : ssaaFactor(config.aa);
        auto const effectiveScale = renderScale * aaScale;
        GLint maxTextureSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
        if (maxTextureSize <= 0) {
            visitNext();
            return;
        }
        // Apply the hardware texture limit as one uniform scale factor.
        // Clamping width and height independently distorts the scene aspect ratio
        // whenever only one dimension exceeds GL_MAX_TEXTURE_SIZE.
        auto const maxDimensionScale = std::min(
            static_cast<float>(maxTextureSize) / static_cast<float>(width),
            static_cast<float>(maxTextureSize) / static_cast<float>(height)
        );
        auto const boundedScale = std::min(effectiveScale, maxDimensionScale);
        auto const internalWidth = scaledDimension(width, boundedScale);
        auto const internalHeight = scaledDimension(height, boundedScale);
        auto const needsPresentation = internalWidth != width || internalHeight != height;
        auto const needsDownsample = internalWidth > width || internalHeight > height;
        if (effectCount == 0 && !needsPresentation) {
            if (g_preparedPostProcessKey || g_failedPostProcessKey) {
                resetRenderResources();
            }
            visitNext();
            return;
        }

        PostProcessKey const key{
            config, internalWidth, internalHeight, needsPresentation, needsDownsample
        };
        PostProcessFailureKey const failureKey{
            key,
            static_cast<GLuint>(callerFramebuffer),
            callerViewport,
        };

        if (g_failedPostProcessKey && *g_failedPostProcessKey == failureKey) {
            visitNext();
            return;
        }
        if (!preparePostProcess(key)) {
            resetRenderResources();
            g_failedPostProcessKey = failureKey;
            log::warn("Post-processing disabled for this configuration");
            visitNext();
            return;
        }

        bv::render::RenderTarget const callerTarget{
            static_cast<GLuint>(callerFramebuffer),
            callerViewport[0],
            callerViewport[1],
            width,
            height,
        };

        g_postProcessPipeline.beginSceneCapture();
        g_isSceneCaptureActive = true;
        g_captureWidth = internalWidth;
        g_captureHeight = internalHeight;
        g_captureFullWidth = width;
        g_captureFullHeight = height;
        visitNext();
        g_isSceneCaptureActive = false;

        {
            bv::render::GlStateGuard postSceneState;
            glDisable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_CULL_FACE);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            g_postProcessPipeline.bindQuad();

            if (effectCount > 0) {
                renderEffects(
                    config,
                    config.cas ? g_casSharpness.load(std::memory_order_relaxed) : 0.f,
                    callerTarget,
                    needsPresentation
                );
            }

            if (needsPresentation) {
                auto const sourceTexture = g_postProcessPipeline.currentTexture();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, sourceTexture);

                // Texture parameters belong to the texture object, not the
                // active texture unit. Preserve the actual values instead of
                // assuming every render target uses GL_LINEAR.
                GLint previousMinFilter = GL_LINEAR;
                GLint previousMagFilter = GL_LINEAR;
                glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &previousMinFilter);
                glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &previousMagFilter);

                auto const linearSampling =
                    needsDownsample || config.upscaling == UpscaleMethod::Bilinear;
                glTexParameteri(
                    GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linearSampling ? GL_LINEAR : GL_NEAREST
                );
                glTexParameteri(
                    GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linearSampling ? GL_LINEAR : GL_NEAREST
                );
                glBindFramebuffer(GL_FRAMEBUFFER, callerTarget.framebuffer);
                glViewport(callerTarget.x, callerTarget.y, callerTarget.width, callerTarget.height);
                if (needsDownsample) {
                    g_ssaaRenderer.apply(sourceTexture);
                }
                else if (config.upscaling == UpscaleMethod::Fsr) {
                    g_fsrRenderer.apply(sourceTexture, g_fsrReconstructionBlend.load(std::memory_order_relaxed));
                }
                else if (config.upscaling == UpscaleMethod::Sgsr) {
                    if (!g_sgsrRenderer.apply(sourceTexture, internalWidth, internalHeight, width, height)) {
                        g_renderScaleRenderer.apply(sourceTexture);
                    }
                }
                else {
                    g_renderScaleRenderer.apply(sourceTexture);
                }

                glBindTexture(GL_TEXTURE_2D, sourceTexture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, previousMinFilter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, previousMagFilter);
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, callerTarget.framebuffer);
        glViewport(callerTarget.x, callerTarget.y, callerTarget.width, callerTarget.height);
        g_failedPostProcessKey.reset();
    }

} // namespace

#ifdef GEODE_IS_ANDROID
namespace {
    std::atomic<bool> g_eglSwapHookReached = false;
    std::atomic<bool> g_eglSwapContextMismatchLogged = false;
    std::atomic<bool> g_eglPresentationDiagnosticsLogged = false;
    std::atomic<bool> g_frameHistoryEnabled = false;
    std::atomic<bool> g_framePacingEnabled = false;
    std::atomic<bool> g_lowLatencyEnabled = false;
    EGLDisplay g_swapPolicyDisplay = EGL_NO_DISPLAY;
    EGLint g_appliedSwapInterval = -1;

    bool hasEglExtension(char const* extensions, std::string_view name) {
        if (!extensions || name.empty()) return false;
        std::string_view list(extensions);
        std::size_t position = 0;
        while ((position = list.find(name, position)) != std::string_view::npos) {
            auto const leftBoundary = position == 0 || list[position - 1] == ' ';
            auto const end = position + name.size();
            auto const rightBoundary = end == list.size() || list[end] == ' ';
            if (leftBoundary && rightBoundary) return true;
            position = end;
        }
        return false;
    }

    void logAndroidPresentationCapabilities(EGLDisplay display) {
        if (g_eglPresentationDiagnosticsLogged.exchange(true, std::memory_order_relaxed)) {
            return;
        }

        auto const extensions = eglQueryString(display, EGL_EXTENSIONS);
        auto const hasPresentationTime =
            hasEglExtension(extensions, "EGL_ANDROID_presentation_time");
        auto const hasFrameTimestamps =
            hasEglExtension(extensions, "EGL_ANDROID_get_frame_timestamps");
        auto const presentationTimeProc =
            eglGetProcAddress("eglPresentationTimeANDROID") != nullptr;
        auto const frameTimestampsProc =
            eglGetProcAddress("eglGetFrameTimestampsANDROID") != nullptr;

        log::info(
            "[FrameGeneration] EGL pacing capabilities: presentation-time extension={}, "
            "entry-point={}, frame-timestamps extension={}, entry-point={}",
            hasPresentationTime, presentationTimeProc,
            hasFrameTimestamps, frameTimestampsProc
        );
    }

    void applyAndroidSwapPolicy(EGLDisplay display) {
        bool const lowLatency = g_lowLatencyEnabled.load(std::memory_order_relaxed);
        bool const framePacing = g_framePacingEnabled.load(std::memory_order_relaxed);
        EGLint const requestedInterval = lowLatency ? 0 : (framePacing ? 1 : -1);

        if (requestedInterval < 0) {
            if (g_appliedSwapInterval >= 0 && g_swapPolicyDisplay == display) {
                if (eglSwapInterval(display, 1) == EGL_TRUE) {
                    g_appliedSwapInterval = -1;
                    g_swapPolicyDisplay = EGL_NO_DISPLAY;
                    log::info("[Presentation] Restored default EGL swap interval");
                }
            }
            return;
        }

        if (g_swapPolicyDisplay == display && g_appliedSwapInterval == requestedInterval) {
            return;
        }
        if (eglSwapInterval(display, requestedInterval) == EGL_TRUE) {
            g_swapPolicyDisplay = display;
            g_appliedSwapInterval = requestedInterval;
            log::info("[Presentation] EGL swap interval set to {} ({})",
                requestedInterval, lowLatency ? "low-latency request" : "frame-pacing request");
        }
        else {
            log::warn("[Presentation] eglSwapInterval({}) failed: {}",
                requestedInterval, eglGetError());
        }
    }

    class AndroidGpuTimer final {
    public:
        void begin() {
            initialize();
            if (!m_supported || m_active >= 0) return;
            poll();
            auto const index = m_next;
            if (m_pending[index]) return;
            m_beginQuery(0x88BF, m_queries[index]);
            m_active = static_cast<int>(index);
        }
        void abandonContext() {
            // Query names belong to the lost GL context; do not delete them in a replacement context.
            m_queries = {};
            m_pending = {};
            m_checked = false;
            m_supported = false;
            m_active = -1;
            m_next = 0;
            m_samples = 0;
            m_totalNs = 0;
            m_genQueries = nullptr;
            m_deleteQueries = nullptr;
            m_beginQuery = nullptr;
            m_endQuery = nullptr;
            m_getQueryObjectuiv = nullptr;
            m_getQueryObjectui64v = nullptr;
        }
        void end() {
            if (m_active < 0) return;
            m_endQuery(0x88BF);
            m_pending[static_cast<std::size_t>(m_active)] = true;
            m_next = (static_cast<std::size_t>(m_active) + 1) % m_queries.size();
            m_active = -1;
        }
    private:
        using GenQueries = void (*)(GLsizei, GLuint*);
        using DeleteQueries = void (*)(GLsizei, GLuint const*);
        using BeginQuery = void (*)(GLenum, GLuint);
        using EndQuery = void (*)(GLenum);
        using GetQueryObjectuiv = void (*)(GLuint, GLenum, GLuint*);
        using GetQueryObjectui64v = void (*)(GLuint, GLenum, GLuint64*);
        void initialize() {
            if (m_checked) return;
            m_checked = true;
            auto const raw = reinterpret_cast<char const*>(glGetString(GL_EXTENSIONS));
            if (!raw || std::string_view(raw).find("GL_EXT_disjoint_timer_query") == std::string_view::npos) {
                log::info("[FrameGeneration][GPU] EXT_disjoint_timer_query unavailable; GPU timing disabled");
                return;
            }
            m_genQueries = reinterpret_cast<GenQueries>(eglGetProcAddress("glGenQueries" "EXT"));
            m_deleteQueries = reinterpret_cast<DeleteQueries>(eglGetProcAddress("glDeleteQueries" "EXT"));
            m_beginQuery = reinterpret_cast<BeginQuery>(eglGetProcAddress("glBeginQuery" "EXT"));
            m_endQuery = reinterpret_cast<EndQuery>(eglGetProcAddress("glEndQuery" "EXT"));
            m_getQueryObjectuiv = reinterpret_cast<GetQueryObjectuiv>(eglGetProcAddress("glGetQueryObjectuiv" "EXT"));
            m_getQueryObjectui64v = reinterpret_cast<GetQueryObjectui64v>(eglGetProcAddress("glGetQueryObjectui64v" "EXT"));
            if (!m_genQueries || !m_deleteQueries || !m_beginQuery || !m_endQuery ||
                !m_getQueryObjectuiv || !m_getQueryObjectui64v) {
                log::warn("[FrameGeneration][GPU] Timer-query entry points incomplete");
                return;
            }
            m_genQueries(static_cast<GLsizei>(m_queries.size()), m_queries.data());
            for (auto query : m_queries) {
                if (!query) {
                    if (m_queries[0]) m_deleteQueries(static_cast<GLsizei>(m_queries.size()), m_queries.data());
                    m_queries = {};
                    log::warn("[FrameGeneration][GPU] Unable to allocate timer queries");
                    return;
                }
            }
            m_supported = true;
            log::info("[FrameGeneration][GPU] Disjoint timer queries enabled");
        }
        void poll() {
            for (std::size_t i = 0; i < m_queries.size(); ++i) {
                if (!m_pending[i]) continue;
                GLuint available = GL_FALSE;
                m_getQueryObjectuiv(m_queries[i], 0x8867, &available);
                if (!available) continue;
                GLint disjoint = GL_FALSE;
                glGetIntegerv(0x8FBB, &disjoint);
                if (!disjoint) {
                    GLuint64 elapsedNs = 0;
                    m_getQueryObjectui64v(m_queries[i], 0x8866, &elapsedNs);
                    m_totalNs += elapsedNs;
                    ++m_samples;
                    if ((m_samples % 120) == 0) {
                        log::info("[FrameGeneration][GPU] average frame capture + interpolation GPU time over 120 samples: {} us",
                            (m_totalNs / 120) / 1000);
                        m_totalNs = 0;
                    }
                }
                m_pending[i] = false;
            }
        }
        bool m_checked = false;
        bool m_supported = false;
        int m_active = -1;
        std::size_t m_next = 0;
        std::array<GLuint, 4> m_queries = {};
        std::array<bool, 4> m_pending = {};
        std::uint64_t m_samples = 0;
        std::uint64_t m_totalNs = 0;
        GenQueries m_genQueries = nullptr;
        DeleteQueries m_deleteQueries = nullptr;
        BeginQuery m_beginQuery = nullptr;
        EndQuery m_endQuery = nullptr;
        GetQueryObjectuiv m_getQueryObjectuiv = nullptr;
        GetQueryObjectui64v m_getQueryObjectui64v = nullptr;
    };

    struct AndroidEglFenceApi {
        using CreateSync = EGLSyncKHR (EGLAPIENTRYP)(EGLDisplay, EGLenum, EGLint const*);
        using ClientWaitSync = EGLint (EGLAPIENTRYP)(EGLDisplay, EGLSyncKHR, EGLint, EGLTimeKHR);
        using DestroySync = EGLBoolean (EGLAPIENTRYP)(EGLDisplay, EGLSyncKHR);
        CreateSync createSync = nullptr;
        ClientWaitSync clientWaitSync = nullptr;
        DestroySync destroySync = nullptr;
        bool checked = false;
        void initialize() {
            if (checked) return;
            checked = true;
            auto const extensions = eglQueryString(eglGetCurrentDisplay(), EGL_EXTENSIONS);
            if (!extensions || !hasEglExtension(extensions, "EGL_KHR_fence_sync")) return;
            createSync = reinterpret_cast<CreateSync>(eglGetProcAddress("eglCreateSyncKHR"));
            clientWaitSync = reinterpret_cast<ClientWaitSync>(eglGetProcAddress("eglClientWaitSyncKHR"));
            destroySync = reinterpret_cast<DestroySync>(eglGetProcAddress("eglDestroySyncKHR"));
            if (!createSync || !clientWaitSync || !destroySync) {
                createSync = nullptr;
                clientWaitSync = nullptr;
                destroySync = nullptr;
            }
        }
    };
    AndroidEglFenceApi& androidEglFenceApi() {
        static AndroidEglFenceApi api;
        api.initialize();
        return api;
    }

    // Two GPU-resident snapshots of the final EGL window surface, plus an
    // experimental motion-matched midpoint texture. Display pacing is separate.
    class AndroidFrameHistory final {
    public:
        bool capture(EGLDisplay display, EGLSurface surface, EGLContext context) {
            if (context == EGL_NO_CONTEXT) return false;
            // CPU timer measures submission only; GPU time is queried separately.
            auto const cpuSubmitStart = std::chrono::steady_clock::now();

            EGLint width = 0;
            EGLint height = 0;
            if (eglQuerySurface(display, surface, EGL_WIDTH, &width) != EGL_TRUE ||
                eglQuerySurface(display, surface, EGL_HEIGHT, &height) != EGL_TRUE ||
                width <= 0 || height <= 0) {
                return false;
            }
            GLint maxTextureSize = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
            if (maxTextureSize <= 0 || width > maxTextureSize || height > maxTextureSize) {
                return false;
            }
            if (!retirePreviousFence(display)) return false;

            if (m_context != context) {
                m_pendingTexture = 0;
                // GL names from a lost context are invalid in the new context;
                // do not attempt to delete them through the replacement context.
                m_textures = {};
                m_width = 0;
                m_height = 0;
                m_writeIndex = 0;
                m_validFrames = 0;
                m_interpolator.abandonContext();
                m_gpuTimer.abandonContext();
                m_context = context;
            }

            if (width != m_width || height != m_height) {
                m_pendingTexture = 0;
                if (m_textures[0] || m_textures[1]) {
                    glDeleteTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
                }
                m_textures = {};
                m_width = 0;
                m_height = 0;
                m_writeIndex = 0;
                m_validFrames = 0;
                m_interpolator.reset();
            }

            m_gpuTimer.begin();
            GLint previousFramebuffer = 0;
            GLint previousActiveTexture = GL_TEXTURE0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
            glActiveTexture(GL_TEXTURE0);
            GLint previousTexture = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

            if (!m_textures[0] && !m_textures[1]) {
                glGenTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
                if (!m_textures[0] || !m_textures[1]) {
                    if (m_textures[0] || m_textures[1]) {
                        glDeleteTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
                    }
                    m_textures = {};
                }
                else {
                    for (auto texture : m_textures) {
                        glBindTexture(GL_TEXTURE_2D, texture);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                        glTexImage2D(
                            GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                            GL_RGBA, GL_UNSIGNED_BYTE, nullptr
                        );
                    }
                    m_width = width;
                    m_height = height;
                }
            }

            bool captured = false;
            bool generated = false;
            if (m_textures[0] && m_textures[1] && m_width == width && m_height == height) {
                // eglSwapBuffers presents the EGL window surface (default FB).
                // Copy before the original swap invalidates/recycles its contents.
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glBindTexture(GL_TEXTURE_2D, m_textures[m_writeIndex]);
                glCopyTexSubImage2D(
                    GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height
                );
                captured = true;
            }

            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
            glActiveTexture(static_cast<GLenum>(previousActiveTexture));
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));

            if (captured) {
                m_writeIndex = 1 - m_writeIndex;
                if (m_validFrames < 2) ++m_validFrames;
                if (m_validFrames == 2) {
                    auto const previousIndex = m_writeIndex;
                    auto const currentIndex = 1 - m_writeIndex;
                    if (m_interpolator.prepare(width, height) &&
                        m_interpolator.generate(m_textures[previousIndex], m_textures[currentIndex])) {
                        // Keep the interpolated frame off-screen until the native
                        // frame boundary is handled by the EGL swap detour.
                        m_pendingTexture = m_textures[currentIndex];
                        generated = true;
                        if (!m_pairLogged) {
                            m_pairLogged = true;
                            log::info("[FrameGeneration] GPU midpoint generated; queued for alternating presentation");
                        }
                    }
                    else if (!m_generationWarningLogged) {
                        m_generationWarningLogged = true;
                        log::warn("[FrameGeneration] Midpoint reconstruction could not be initialized; capture history remains available");
                    }
                }
            }

            m_gpuTimer.end();
            ++m_profiledCaptures;
            auto const elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - cpuSubmitStart
            ).count();
            m_cpuSubmitMicros += static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsedUs));
            if ((m_profiledCaptures % 120) == 0) {
                auto const averageUs = m_cpuSubmitMicros / 120;
                log::info(
                    "[FrameGeneration][CPU] avg capture + GPU-submit overhead over 120 frames: {} us; "
                    "GPU execution is asynchronous and is not measured by this CPU timer",
                    averageUs
                );
                m_cpuSubmitMicros = 0;
            }
            return generated;
        }

        bool presentMidpoint() {
            return m_interpolator.present();
        }
        bool presentNativeFrame() {
            if (!m_pendingTexture) return false;
            auto const texture = m_pendingTexture;
            auto const presented = m_interpolator.presentTexture(texture);
            if (presented) m_pendingTexture = 0;
            return presented;
        }
        void finishPresentation(EGLDisplay display) {
            createCompletionFence(display);
        }

    private:
        bool retirePreviousFence(EGLDisplay display) {
            if (m_gpuFence == EGL_NO_SYNC_KHR) return true;
            auto& api = androidEglFenceApi();
            if (!api.clientWaitSync || !api.destroySync) {
                m_gpuFence = EGL_NO_SYNC_KHR;
                return true;
            }
            auto const result = api.clientWaitSync(display, m_gpuFence, 0, 0);
            if (result == EGL_TIMEOUT_EXPIRED_KHR) return false;
            api.destroySync(display, m_gpuFence);
            m_gpuFence = EGL_NO_SYNC_KHR;
            if (result != EGL_CONDITION_SATISFIED_KHR) {
                log::warn("[FrameGeneration][Fence] Non-blocking poll failed; retaining same-context GL ordering");
            }
            return true;
        }
        void createCompletionFence(EGLDisplay display) {
            auto& api = androidEglFenceApi();
            if (!api.createSync) return;
            if (m_gpuFence != EGL_NO_SYNC_KHR && api.destroySync) {
                api.destroySync(display, m_gpuFence);
                m_gpuFence = EGL_NO_SYNC_KHR;
            }
            m_gpuFence = api.createSync(display, EGL_SYNC_FENCE_KHR, nullptr);
            if (m_gpuFence == EGL_NO_SYNC_KHR && !m_fenceWarningLogged) {
                m_fenceWarningLogged = true;
                log::warn("[FrameGeneration][Fence] Fence creation failed; no glFinish fallback will be used");
            }
        }
        std::uint64_t m_profiledCaptures = 0;
        std::uint64_t m_cpuSubmitMicros = 0;
        std::array<GLuint, 2> m_textures = {};
        GLuint m_pendingTexture = 0;
        bv::render::FrameInterpolationRenderer m_interpolator;
        AndroidGpuTimer m_gpuTimer;
        EGLSyncKHR m_gpuFence = EGL_NO_SYNC_KHR;
        bool m_fenceWarningLogged = false;
        EGLContext m_context = EGL_NO_CONTEXT;
        EGLint m_width = 0;
        EGLint m_height = 0;
        std::size_t m_writeIndex = 0;
        std::size_t m_validFrames = 0;
        bool m_pairLogged = false;
        bool m_generationWarningLogged = false;
    };

    AndroidFrameHistory g_androidFrameHistory;
    EGLBoolean betterVisualsEglSwapBuffers(EGLDisplay display, EGLSurface surface) {
        auto const context = eglGetCurrentContext();
        auto const currentDisplay = eglGetCurrentDisplay();
        auto const currentDrawSurface = eglGetCurrentSurface(EGL_DRAW);
        auto const isGamePresentationContext =
            context != EGL_NO_CONTEXT &&
            currentDisplay == display &&
            currentDrawSurface == surface;

        if (isGamePresentationContext) {
            applyAndroidSwapPolicy(display);
        }

        EGLBoolean result = EGL_FALSE;
        bool const frameGenerationEnabled =
            isGamePresentationContext &&
            g_frameHistoryEnabled.load(std::memory_order_relaxed);

        // This opt-in path performs two EGL swaps only when
        // frame-history-capture is enabled by the user.
        if (frameGenerationEnabled) {
            bool const generated = g_androidFrameHistory.capture(display, surface, context);
            if (generated && g_androidFrameHistory.presentMidpoint()) {
                // TulipHook's ARM64 handler advances its per-thread chain index
                // for nested calls. Calling eglSwapBuffers here therefore invokes
                // the next hook/original function, not this detour recursively.
                // Present order is: previous native frame (already on screen),
                // generated midpoint, then the just-captured native frame.
                auto const midpointSwap = eglSwapBuffers(display, surface);
                if (midpointSwap != EGL_TRUE) {
                    result = midpointSwap;
                }
                else if (g_androidFrameHistory.presentNativeFrame()) {
                    g_androidFrameHistory.finishPresentation(display);
                    result = eglSwapBuffers(display, surface);
                }
                else {
                    log::warn("[FrameGeneration] Native-frame restore failed after midpoint swap");
                    result = EGL_FALSE;
                }
            }
            else {
                // If interpolation is unavailable for this pair, keep the
                // original native frame rather than presenting a stale midpoint.
                if (generated) {
                    (void)g_androidFrameHistory.presentNativeFrame();
                }
                result = eglSwapBuffers(display, surface);
            }
        }
        else {
            result = eglSwapBuffers(display, surface);
        }

        if (result == EGL_TRUE && isGamePresentationContext &&
            !g_eglSwapHookReached.exchange(true, std::memory_order_relaxed)) {
            log::info("[FrameGeneration] Android EGL swap reached on the active GL context");
        }
        if (result == EGL_TRUE && isGamePresentationContext) {
            logAndroidPresentationCapabilities(display);
        }
        if (result == EGL_TRUE && !isGamePresentationContext &&
            !g_eglSwapContextMismatchLogged.exchange(true, std::memory_order_relaxed)) {
            log::warn("[FrameGeneration] EGL swap observed without matching current GL context; frame processing remains disabled");
        }
        return result;
    }

    void installAndroidEglSwapHook() {
        auto const address = reinterpret_cast<void*>(eglSwapBuffers);
        if (!address) {
            log::warn("[FrameGeneration] eglSwapBuffers symbol is unavailable; presentation hook skipped");
            return;
        }

        auto result = Mod::get()->hook(
            address,
            &betterVisualsEglSwapBuffers,
            "Android eglSwapBuffers presentation probe",
            tulip::hook::TulipConvention::Cdecl
        );
        if (result.isErr()) {
            log::warn("[FrameGeneration] Unable to install EGL presentation hook: {}", result.unwrapErr());
            return;
        }
        log::info("[FrameGeneration] Android EGL presentation hook installed");
    }
}
#endif

$on_mod(Loaded) {
#ifdef GEODE_IS_ANDROID
    installAndroidEglSwapHook();
#endif
    bindBoolSetting("enabled", g_modEnabled);
    bindBoolSetting("enable-fun", g_funEnabled);
#ifdef GEODE_IS_ANDROID
    bindBoolSetting("frame-history-capture", g_frameHistoryEnabled);
    bindBoolSetting("frame-pacing-enabled", g_framePacingEnabled);
    bindBoolSetting("low-latency-enabled", g_lowLatencyEnabled);
#endif

    auto applyFullScene = [](bool on) {
        g_fullSceneEnabled.store(on, std::memory_order_relaxed);
        if (g_sceneVisitHook) {
            (void)g_sceneVisitHook->toggle(on);
        }
        if (g_gameLayerVisitHook) {
            (void)g_gameLayerVisitHook->toggle(!on);
        }
    };
    applyFullScene(Mod::get()->getSettingValue<bool>("full-scene-effects"));
    listenForSettingChanges<bool>("full-scene-effects", applyFullScene);
    bindDoubleSetting("render-scale", g_renderScale, 0.25f, 1.2f, 1.f);
    bindStringSetting("upscale-method", updateUpscaleMethod);
    bindStringSetting("fsr-quality-preset", updateFsrQualityPreset);
    bindStringSetting("sgsr-quality-preset", updateSgsrQualityPreset);
    updateAntiAliasingMethod(Mod::get()->getSettingValue<std::string>("aa-method"));
    listenForSettingChanges<std::string>("aa-method", [](std::string value) {
        auto const wasSsaa = ssaaFactor(g_antiAliasingMethod.load(std::memory_order_relaxed)) > 1.f;
        updateAntiAliasingMethod(value);
        auto const isSsaa = ssaaFactor(g_antiAliasingMethod.load(std::memory_order_relaxed)) > 1.f;
        if (!wasSsaa && isSsaa) {
            showSsaaWarning();
        }
    });
    bindBoolSetting("cas-enabled", g_casEnabled);
    bindDoubleSetting("cas-sharpness", g_casSharpness, 0.f, 1.f, 0.f);
    bindDoubleSetting("fsr-sharpness", g_fsrReconstructionBlend, 0.f, 1.f, 1.f);
    bindBoolSetting("bloom-enabled", g_bloomEnabled);
    bindDoubleSetting("bloom-threshold", g_bloomThreshold, 0.f, 1.f, 0.7f);
    bindDoubleSetting("bloom-intensity", g_bloomIntensity, 0.f, 1.5f, 0.3f);
    bindDoubleSetting("bloom-radius", g_bloomRadius, 0.f, 16.f, 8.f);
    bindBoolSetting("grayscale-enabled", g_grayscaleEnabled);
    bindBoolSetting("pixelate-enabled", g_pixelateEnabled);
    bindBoolSetting("invert-enabled", g_invertEnabled);
    bindBoolSetting("flip-enabled", g_flipEnabled);
    bindBoolSetting("dithering-enabled", g_ditheringEnabled);
    bindBoolSetting("vhs-enabled", g_vhsEnabled);
    bindBoolSetting("crt-enabled", g_crtEnabled);
}

class $modify(BetterVisualsDirectorHook, CCDirector) {
    void setProjection(ccDirectorProjection kProjection) {
        CCDirector::setProjection(kProjection);
        applyCaptureViewport();
    }

    void setViewport() {
        CCDirector::setViewport();
        applyCaptureViewport();
    }

};

#ifdef GEODE_IS_MOBILE
class $modify(BetterVisualsScissorHook, CCEGLViewProtocol) {
    void setScissorInPoints(float x, float y, float w, float h) {
        scaleCaptureScissor(x, y, w, h);
        CCEGLViewProtocol::setScissorInPoints(x, y, w, h);
    }
};
#endif

#ifdef GEODE_IS_WINDOWS
class $modify(BetterVisualsEGLViewHook, CCEGLView) {
    void setViewPortInPoints(float x, float y, float w, float h) {
        CCEGLView::setViewPortInPoints(x, y, w, h);
        applyCaptureViewport();
    }
};
#endif

class $modify(BetterVisualsGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("GJBaseGameLayer::visit", Priority::VeryLate)) {
            log::warn("Unable to set GJBaseGameLayer::visit hook priority");
        }
        if (auto hook = self.getHook("GJBaseGameLayer::visit")) {
            g_gameLayerVisitHook = hook.unwrap();
        }
    }

    void visit() {
        if (g_temporarilyDisabled.load(std::memory_order_relaxed) ||
            !g_modEnabled.load(std::memory_order_relaxed) ||
            g_fullSceneEnabled.load(std::memory_order_relaxed) || g_isSceneVisitActive ||
            static_cast<GJBaseGameLayer*>(this) != GJBaseGameLayer::get()) {
            teardownDisabledPostProcess();
            GJBaseGameLayer::visit();
            return;
        }

        g_isSceneVisitActive = true;
        renderSceneWithPostProcessing([this] {
            GJBaseGameLayer::visit();
        });
        g_isSceneVisitActive = false;
    }
};

class $modify(BetterVisualsSceneVisitHook, CCNode) {
    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("cocos2d::CCNode::visit", Priority::VeryLate)) {
            log::warn("Unable to set CCNode::visit hook priority");
        }
        if (auto hook = self.getHook("cocos2d::CCNode::visit")) {
            g_sceneVisitHook = hook.unwrap();
        }
    }

    void visit() {
        if (!g_fullSceneEnabled.load(std::memory_order_relaxed) ||
            g_temporarilyDisabled.load(std::memory_order_relaxed) ||
            !g_modEnabled.load(std::memory_order_relaxed) || g_isSceneVisitActive ||
            static_cast<CCNode*>(this) != CCDirector::get()->getRunningScene()) {
            teardownDisabledPostProcess();
            CCNode::visit();
            return;
        }

        g_isSceneVisitActive = true;
        renderSceneWithPostProcessing([this] {
            CCNode::visit();
        });
        g_isSceneVisitActive = false;
    }
};

#ifdef GEODE_IS_WINDOWS
class $modify(BetterVisualsEGLView, CCEGLView) {
    void toggleFullScreen(bool value, bool borderless, bool fix) {
        if (!g_temporarilyDisabled.exchange(true)) {
            resetRenderResources();
        }
        CCEGLView::toggleFullScreen(value, borderless, fix);
    }
};
#endif

class $modify(BetterVisualsMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        if (g_temporarilyDisabled.load(std::memory_order_relaxed) &&
            !g_disablePopupShown.exchange(true)) {
            queueInMainThread([] {
                createQuickPopup(
                    "Better Visuals and FPS",
                    "Better Visuals and FPS temporary disabled.\n"
                    " Please restart the game to keep fullscreen/windowed mode.\n"
                    "Sorry for the inconvenience!",
                    "OK",
                    nullptr,
                    [](FLAlertLayer*, bool) {}
                );
            });
        }
        return true;
    }
};

#ifdef GEODE_IS_MOBILE
class $modify(BetterVisualsAppDelegate, AppDelegate) {
    void applicationDidEnterBackground() {
        resetRenderResources();
        AppDelegate::applicationDidEnterBackground();
    }
};
#endif
