#include "mayaSceneBridge.h"

#include <mayaUsd/mayaUsd.h>
#include <mayaUsd/nodes/proxyShapeBase.h>

#include <maya/M3dView.h>
#include <maya/MDagPath.h>
#include <maya/MDistance.h>
#include <maya/MFnCamera.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MObject.h>
#include <maya/MSelectionList.h>

#include <pxr/base/gf/range1f.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdLux/lightAPI.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

PXR_NS::MayaUsdProxyShapeBase* asProxyShape(const MObject& node)
{
    if (node.isNull()) {
        return nullptr;
    }
    MStatus            status;
    MFnDependencyNode  depFn(node, &status);
    if (!status) {
        return nullptr;
    }
    return dynamic_cast<PXR_NS::MayaUsdProxyShapeBase*>(depFn.userNode());
}

MMatrix worldMatrixOf(const MObject& shape)
{
    MDagPath dp;
    if (MDagPath::getAPathTo(shape, dp) == MS::kSuccess) {
        return dp.inclusiveMatrix();
    }
    return MMatrix::identity;
}

MObject resolveProxyShapeFromName(const MString& nodeName)
{
    MSelectionList sel;
    if (!sel.add(nodeName)) {
        return MObject::kNullObj;
    }

    MObject node;
    if (!sel.getDependNode(0, node) || node.isNull()) {
        return MObject::kNullObj;
    }

    if (asProxyShape(node)) {
        return node;
    }

    MStatus status;
    MFnDagNode dagFn(node, &status);
    if (status) {
        for (uint32_t i = 0; i < dagFn.childCount(); ++i) {
            const MObject child = dagFn.child(i);
            if (asProxyShape(child)) {
                return child;
            }
        }
    }
    return MObject::kNullObj;
}

GfMatrix4d toGf(const MMatrix& m)
{
    GfMatrix4d g;
    for (int32_t r = 0; r < 4; ++r) {
        for (int32_t c = 0; c < 4; ++c) {
            g[r][c] = m(r, c);
        }
    }
    return g;
}

} // namespace

namespace OVRTXMAYA_NS
{

bool activeViewportSize(int32_t* width, int32_t* height, MString* error)
{
    MStatus status;
    M3dView view = M3dView::active3dView(&status);
    if (!status) {
        if (error) {
            *error = "No active 3D viewport. Hover over a viewport and retry.";
        }
        return false;
    }
    if (width) {
        *width = static_cast<int32_t>(view.portWidth());
    }
    if (height) {
        *height = static_cast<int32_t>(view.portHeight());
    }
    return true;
}

bool activeViewportCamera(double renderAspect, GfCamera* outCam, MString* error)
{
    auto fail = [&](const char* msg) {
        if (error) {
            *error = msg;
        }
        return false;
    };

    if (!outCam) {
        return fail("Internal error: null output camera.");
    }
    if (renderAspect <= 0.0) {
        renderAspect = 1.0;
    }

    MStatus status;
    M3dView view = M3dView::active3dView(&status);
    if (!status) {
        return fail("No active 3D viewport. Hover over a viewport and retry.");
    }

    MDagPath camPath;
    if (!view.getCamera(camPath)) {
        return fail("Could not read the active viewport camera.");
    }

    MFnCamera camFn(camPath, &status);
    if (!status) {
        return fail("Active viewport object is not a camera.");
    }

    // World transform in Maya space (cm internal units). Maya MMatrix and
    // GfMatrix4d are both row-major with the row-vector convention.
    const GfMatrix4d xform = toGf(camPath.inclusiveMatrix());
    const double focalLength = camFn.focalLength();   // mm
    const double nearClip    = camFn.nearClippingPlane();
    const double farClip     = camFn.farClippingPlane();

    outCam->SetTransform(xform);
    outCam->SetClippingRange(GfRange1f(static_cast<float>(nearClip), static_cast<float>(farClip)));

    double left = 0, right = 0, bottom = 0, top = 0;
    camFn.getViewingFrustum(renderAspect, left, right, bottom, top,
                            /*applyOverscan=*/true,
                            /*applySqueeze=*/true,
                            /*applyPanZoom=*/true);

    if (camFn.isOrtho()) {
        // Orthographic: the frustum is a world-space box; (top-bottom) is its
        // height. Let GfCamera derive the width from the render aspect.
        outCam->SetProjection(GfCamera::Orthographic);
        outCam->SetOrthographicFromAspectRatioAndSize(
            static_cast<float>(renderAspect),
            static_cast<float>(top - bottom),
            GfCamera::FOVVertical);
        return true;
    }

    // Perspective: convert the frustum window into GfCamera aperture + offset:
    //   aperture = focalLength * (window_size)   / near
    //   offset   = focalLength * (window_center) / near
    // (units cancel in the window/near ratio; aperture stays in mm).

    const double inv = (nearClip != 0.0) ? (1.0 / nearClip) : 0.0;
    const double apH  = focalLength * (right - left) * inv;
    const double apV  = focalLength * (top - bottom) * inv;
    const double offH = focalLength * (right + left) * 0.5 * inv;
    const double offV = focalLength * (top + bottom) * 0.5 * inv;

    outCam->SetProjection(GfCamera::Perspective);
    outCam->SetFocalLength(static_cast<float>(focalLength));
    outCam->SetHorizontalAperture(static_cast<float>(apH));
    outCam->SetVerticalAperture(static_cast<float>(apV));
    outCam->SetHorizontalApertureOffset(static_cast<float>(offH));
    outCam->SetVerticalApertureOffset(static_cast<float>(offV));
    return true;
}

bool stageHasLight(const UsdStageRefPtr& stage)
{
    if (!stage) {
        return false;
    }
    for (const UsdPrim& prim : stage->Traverse()) {
        if (prim.HasAPI<UsdLuxLightAPI>()) {
            return true;
        }
    }
    return false;
}

GfMatrix4d usdStageToMayaWorld(const UsdStageRefPtr& stage,
                               const MMatrix&        proxyWorld)
{
    const GfMatrix4d proxyGf = toGf(proxyWorld);

    if (stage) {
        const TfToken up = UsdGeomGetStageUpAxis(stage);
        const double  mpu = UsdGeomGetStageMetersPerUnit(stage);
        MString info("[ovrtxRender] stage upAxis=");
        info += up.GetText();
        info += ", metersPerUnit=";
        info += mpu;
        info += ", maya up=";
        info += (MGlobal::isYAxisUp() ? "Y" : (MGlobal::isZAxisUp() ? "Z" : "?"));
        MGlobal::displayInfo(info);

        MString m("[ovrtxRender] proxyWorld rows: ");
        for (int32_t r = 0; r < 4; ++r) {
            m += "(";
            for (int32_t c = 0; c < 4; ++c) {
                if (c) m += ", ";
                m += proxyGf[r][c];
            }
            m += ") ";
        }
        MGlobal::displayInfo(m);
    }

    return proxyGf;
}

UsdStageRefPtr findProxyShapeStage(const MString& nodeName,
                                   MMatrix*       proxyWorldOut,
                                   MString*       error)
{
    auto fail = [&](const char* msg) -> UsdStageRefPtr {
        if (error) {
            *error = msg;
        }
        return UsdStageRefPtr();
    };

    if (nodeName.length() > 0) {
        const MObject shape = resolveProxyShapeFromName(nodeName);
        if (auto* proxy = asProxyShape(shape)) {
            UsdStageRefPtr stage = proxy->getUsdStage();
            if (stage) {
                if (proxyWorldOut) {
                    *proxyWorldOut = worldMatrixOf(shape);
                }
                return stage;
            }
            return fail("Named proxy shape has no composed USD stage yet.");
        }
        return fail("Named node is not a mayaUsd proxy shape.");
    }

    MStatus            status;
    MItDependencyNodes it(MFn::kPluginShape, &status);
    UsdStageRefPtr     found;
    MObject            foundShape;
    int32_t            proxyCount = 0;
    for (; status && !it.isDone(); it.next()) {
        const MObject node = it.thisNode();
        if (auto* proxy = asProxyShape(node)) {
            ++proxyCount;
            if (UsdStageRefPtr stage = proxy->getUsdStage()) {
                if (!found) {
                    found = stage;
                    foundShape = node;
                }
            }
        }
    }

    if (found) {
        if (proxyWorldOut) {
            *proxyWorldOut = worldMatrixOf(foundShape);
        }
        return found;
    }
    if (proxyCount > 0) {
        return fail("Found mayaUsd proxy shape(s) but none has a composed "
                    "USD stage. Load/define a stage first.");
    }
    return fail("No mayaUsd proxy shape found in the scene. Create a USD "
                "stage (e.g. Create > USD Stage) first.");
}

std::vector<ProxyStageEntry> findAllProxyShapeStages(MString* error)
{
    std::vector<ProxyStageEntry> entries;

    MStatus status;
    MItDependencyNodes it(MFn::kPluginShape, &status);
    for (; status && !it.isDone(); it.next()) {
        const MObject node = it.thisNode();
        if (auto* proxy = asProxyShape(node)) {
            if (UsdStageRefPtr stage = proxy->getUsdStage()) {
                entries.push_back({stage, worldMatrixOf(node)});
            }
        }
    }

    if (entries.empty() && error) {
        *error = "No mayaUsd proxy shape with a composed USD stage was found. "
                 "Create/load a USD stage first.";
    }
    return entries;
}

} // namespace OVRTXMAYA_NS