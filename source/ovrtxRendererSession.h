/*
 *  Copyright (c) 2026 Hamed Sabri. All rights reserved. 
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:

 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.

 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#pragma once

// RAII wrapper around the ovrtx C API: renderer lifecycle, USD loading, and
// asynchronous single-frame AOV rendering decoded to a QImage.

#include <QByteArray>
#include <QImage>
#include <QString>

#include <ovrtx/ovrtx.h>
#include <ovrtx/ovrtx_types.h>

#include <string>

namespace OVRTXMAYA_NS
{

class OvrtxRendererSession final
{
public:
    enum class Aov
    {
        LdrColor,             // uint8
        DiffuseAlbedoSD,      // uint8
        NormalSD,             // float
    };

    struct Settings
    {
        Aov     aov                   {Aov::LdrColor};
        int32_t samplesPerPixel       {64};
        bool    autoExposure          {true};
        // When true, apply OmniRtxPostCompositingAPI_1 to the
        // RenderProduct and turn on output-alpha + premultiply
        bool  transparentBackground {false};

        // Depth-of-field via OmniRtxPostDofAPI_1. 
        bool  dofEnabled         {false};
        float dofFStop           {5.6f};
        float dofSubjectDistance {100.0f};

        // Post passes
        bool  bloomEnabled                {false};
        bool  colorGradingEnabled         {false};
        bool  chromaticAberrationEnabled  {false};
        bool  motionBlurEnabled           {false};

        // Tonemap
        enum class Tonemap {
            Default, Raw, None, Reinhard, ModifiedReinhard,
            HejiHableAlu, HableUc2, AcesApproximation, Iray
        };
        Tonemap tonemap {Tonemap::Default};
    };

    struct AovDesc
    {
        const char* primName;
        const char* sourceName;
        const char* renderMode;
    };
public:
    OvrtxRendererSession() = default;
    ~OvrtxRendererSession();

    OvrtxRendererSession(const OvrtxRendererSession&) = delete;
    OvrtxRendererSession& operator=(const OvrtxRendererSession&) = delete;

    bool initialize();
    void shutdown();

    [[nodiscard]] static AovDesc describeAov(Aov aov);
    [[nodiscard]] bool isReady() const;
    [[nodiscard]] const QString& lastError() const { return m_lastError; }
    void clearError() { m_lastError.clear(); }

    bool openUsdFromFile(const QString& filePath);
    bool openUsdFromString(const std::string& usdaContent);
    bool addUsdReferenceFromString(const std::string& usdaContent, const QString& prefixPath);
    bool resetSimulation(double time);
    bool resetStage();

    // beginRender enqueues one ovrtx_step; 
    bool beginRender(const QString& renderProductPath, Aov aov, double deltaSeconds);

    // pollRender returns true while still in flight and caches the decoded image once complete.
    bool pollRender();
    [[nodiscard]] bool renderConverged() const;
    // In-flight render progress in [0,1], or negative if unknown/indeterminate.
    [[nodiscard]] double renderProgress() const;
    [[nodiscard]] QImage readImage() const;
    void endRender();
    void abandonRender();

private:
    enum class Phase
    {
        Idle,
        WaitingStep,
        FetchingResults,
        MappingOutput
    };

    void resetRenderState();

private:
    ovrtx_renderer_t*                  m_renderer {nullptr};
    ovrtx_usd_handle_t                 m_overlayHandle {OVRTX_INVALID_HANDLE};
    ovrtx_step_result_handle_t         m_stepResult {OVRTX_INVALID_HANDLE};
    ovrtx_op_id_t                      m_stepOpId {OVRTX_INVALID_HANDLE};
    ovrtx_render_product_set_outputs_t m_outputs {};
    ovrtx_render_var_output_handle_t   m_outputHandle {OVRTX_INVALID_HANDLE};
    QByteArray                         m_renderProductPathBytes;
    ovx_string_t                       m_renderProductPath {};
    ovrtx_render_product_set_t         m_renderProducts {};
    QImage                             m_image;
    Aov                                m_aov {Aov::LdrColor};
    Phase                              m_phase {Phase::Idle};
    double                             m_progress {-1.0};
    bool                               m_active {false};
    bool                               m_converged {false};
    QString                            m_lastError;
};

} // namespace OVRTXMAYA_NS
