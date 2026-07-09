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

// Utility functions to locates proxy-shape stages, queries the active viewport
// camera/size, and computes stage-to-Maya-world transforms.

#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/usd/stage.h>

#include <maya/MMatrix.h>
#include <maya/MString.h>

#include <vector>

namespace OVRTXMAYA_NS
{

struct ProxyStageEntry
{
    PXR_NS::UsdStageRefPtr stage;
    MMatrix proxyWorld;
};

bool activeViewportCamera(double renderAspect, PXR_NS::GfCamera* outCam, MString* error);

bool activeViewportSize(int32_t* width, int32_t* height, MString* error);

bool stageHasLight(const PXR_NS::UsdStageRefPtr& stage);

PXR_NS::GfMatrix4d usdStageToMayaWorld(const PXR_NS::UsdStageRefPtr& stage, const MMatrix& proxyWorld);

PXR_NS::UsdStageRefPtr findProxyShapeStage(const MString& nodeName, MMatrix* proxyWorldOut, MString* error);

std::vector<ProxyStageEntry> findAllProxyShapeStages(MString* error);

} // namespace OVRTXMAYA_NS