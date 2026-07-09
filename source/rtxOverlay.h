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

// Here we build the RTX overlay USD (camera + RenderProduct + render settings) and
// composites Maya proxy-shape USD stage(s) into the ovrtx renderer.

#include "ovrtxRendererSession.h"

#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/usd/stage.h>

#include <QString>
#include <string>
#include <vector>

using Settings = OVRTXMAYA_NS::OvrtxRendererSession::Settings;

namespace OVRTXMAYA_NS
{

constexpr const char* kRtxOverlayRoot = "/OVRTX_MAYA";
constexpr const char* kRtxRenderProductPath = "/OVRTX_MAYA/RenderProduct";

// Build the camera + RenderProduct + RenderVar overlay authored on top of the
// scene before rendering.
std::string buildRtxOverlayUsda(const PXR_NS::GfCamera& cam,
                                int32_t width,
                                int32_t height,
                                const Settings& settings);

struct RtxStage
{
    PXR_NS::UsdStageRefPtr stage;
    PXR_NS::GfMatrix4d stageToWorld;
};

bool openStagesForRtx(OvrtxRendererSession& renderer,
                      const std::vector<RtxStage>& stages,
                      QString* error);

} // namespace OVRTXMAYA_NS