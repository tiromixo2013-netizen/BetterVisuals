#include "GlStateGuard.hpp"

#include "FullscreenQuad.hpp"

#include <cassert>
#include <cstddef>

namespace {

    bv::render::VertexAttributeState captureVertexAttribute(GLuint index) {
        bv::render::VertexAttributeState state;
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &state.enabled);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &state.buffer);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &state.size);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &state.stride);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &state.type);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &state.normalized);
        glGetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER, &state.pointer);
        return state;
    }

    void restoreCapability(GLenum capability, GLboolean enabled) {
        if (enabled == GL_TRUE) {
            glEnable(capability);
        }
        else {
            glDisable(capability);
        }
    }

    void restoreVertexAttribute(GLuint index, bv::render::VertexAttributeState const& state) {
        glBindBuffer(GL_ARRAY_BUFFER, state.buffer);
        glVertexAttribPointer(
            index,
            state.size,
            state.type,
            static_cast<GLboolean>(state.normalized),
            state.stride,
            state.pointer
        );

        if (state.enabled == GL_TRUE) {
            glEnableVertexAttribArray(index);
        }
        else {
            glDisableVertexAttribArray(index);
        }
    }

} // namespace

namespace bv::render {

    GlState captureGlState() {
        GlState state;
        glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);

        for (std::size_t index = 0; index < state.textures2D.size(); ++index) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(index));
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures2D[index]);
        }
        glActiveTexture(state.activeTexture);

        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.arrayBuffer);
        glGetIntegerv(GL_VIEWPORT, state.viewport.data());
        glGetIntegerv(GL_SCISSOR_BOX, state.scissorBox.data());
        glGetFloatv(GL_COLOR_CLEAR_VALUE, state.clearColor.data());
        glGetFloatv(GL_BLEND_COLOR, state.blendColor.data());
        glGetBooleanv(GL_COLOR_WRITEMASK, state.colorMask.data());

        state.blend = glIsEnabled(GL_BLEND);
        state.depthTest = glIsEnabled(GL_DEPTH_TEST);
        state.stencilTest = glIsEnabled(GL_STENCIL_TEST);
        state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
        state.cullFace = glIsEnabled(GL_CULL_FACE);

        glGetIntegerv(GL_BLEND_SRC_RGB, &state.blendSrcRGB);
        glGetIntegerv(GL_BLEND_DST_RGB, &state.blendDstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &state.blendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &state.blendDstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &state.blendEquationRGB);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state.blendEquationAlpha);

        glGetIntegerv(GL_DEPTH_FUNC, &state.depthFunc);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depthMask);

        glGetIntegerv(GL_STENCIL_FUNC, &state.stencilFunc);
        glGetIntegerv(GL_STENCIL_REF, &state.stencilRef);
        glGetIntegerv(GL_STENCIL_VALUE_MASK, &state.stencilValueMask);
        glGetIntegerv(GL_STENCIL_FAIL, &state.stencilFail);
        glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &state.stencilDepthFail);
        glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &state.stencilDepthPass);
        glGetIntegerv(GL_STENCIL_WRITEMASK, &state.stencilWriteMask);

        glGetIntegerv(GL_STENCIL_BACK_FUNC, &state.stencilBackFunc);
        glGetIntegerv(GL_STENCIL_BACK_REF, &state.stencilBackRef);
        glGetIntegerv(GL_STENCIL_BACK_VALUE_MASK, &state.stencilBackValueMask);
        glGetIntegerv(GL_STENCIL_BACK_FAIL, &state.stencilBackFail);
        glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_FAIL, &state.stencilBackDepthFail);
        glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_PASS, &state.stencilBackDepthPass);
        glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &state.stencilBackWriteMask);

        glGetIntegerv(GL_CULL_FACE_MODE, &state.cullFaceMode);
        glGetIntegerv(GL_FRONT_FACE, &state.frontFace);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state.framebuffer);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &state.renderbuffer);

        state.attributes[0] = captureVertexAttribute(FullscreenQuad::kPositionAttribute);
        state.attributes[1] = captureVertexAttribute(FullscreenQuad::kTexCoordAttribute);
        return state;
    }

    void restoreGlState(GlState const& state) {
        restoreVertexAttribute(FullscreenQuad::kPositionAttribute, state.attributes[0]);
        restoreVertexAttribute(FullscreenQuad::kTexCoordAttribute, state.attributes[1]);
        glBindBuffer(GL_ARRAY_BUFFER, state.arrayBuffer);

        for (std::size_t index = 0; index < state.textures2D.size(); ++index) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(index));
            glBindTexture(GL_TEXTURE_2D, state.textures2D[index]);
        }
        glActiveTexture(state.activeTexture);
        glUseProgram(state.program);

        glBindFramebuffer(GL_FRAMEBUFFER, state.framebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, state.renderbuffer);
        glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
        glScissor(state.scissorBox[0], state.scissorBox[1], state.scissorBox[2], state.scissorBox[3]);
        glClearColor(
            state.clearColor[0], state.clearColor[1], state.clearColor[2], state.clearColor[3]
        );
        glBlendColor(
            state.blendColor[0], state.blendColor[1], state.blendColor[2], state.blendColor[3]
        );
        glColorMask(state.colorMask[0], state.colorMask[1], state.colorMask[2], state.colorMask[3]);

        glBlendFuncSeparate(
            state.blendSrcRGB, state.blendDstRGB, state.blendSrcAlpha, state.blendDstAlpha
        );
        glBlendEquationSeparate(state.blendEquationRGB, state.blendEquationAlpha);

        glDepthFunc(state.depthFunc);
        glDepthMask(state.depthMask);

        glStencilFuncSeparate(
            GL_FRONT, state.stencilFunc, state.stencilRef, static_cast<GLuint>(state.stencilValueMask)
        );
        glStencilOpSeparate(
            GL_FRONT, state.stencilFail, state.stencilDepthFail, state.stencilDepthPass
        );
        glStencilMaskSeparate(GL_FRONT, static_cast<GLuint>(state.stencilWriteMask));
        glStencilFuncSeparate(
            GL_BACK, state.stencilBackFunc, state.stencilBackRef,
            static_cast<GLuint>(state.stencilBackValueMask)
        );
        glStencilOpSeparate(
            GL_BACK, state.stencilBackFail, state.stencilBackDepthFail, state.stencilBackDepthPass
        );
        glStencilMaskSeparate(GL_BACK, static_cast<GLuint>(state.stencilBackWriteMask));

        glCullFace(state.cullFaceMode);
        glFrontFace(state.frontFace);

        restoreCapability(GL_BLEND, state.blend);
        restoreCapability(GL_DEPTH_TEST, state.depthTest);
        restoreCapability(GL_STENCIL_TEST, state.stencilTest);
        restoreCapability(GL_SCISSOR_TEST, state.scissorTest);
        restoreCapability(GL_CULL_FACE, state.cullFace);
    }

    GlStateGuard::GlStateGuard() : m_state(captureGlState()) {}

    GlStateGuard::~GlStateGuard() {
        restoreGlState(m_state);
#ifndef NDEBUG
        auto const restored = captureGlState();
        assert((restored == m_state) && "Post-processing failed to restore OpenGL state");
#endif
    }

} // namespace bv::render
