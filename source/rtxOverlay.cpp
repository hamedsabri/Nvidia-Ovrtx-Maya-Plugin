#include "rtxOverlay.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range1f.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>

#include <QDir>
#include <QFile>
#include <QStringList>

#include <algorithm>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

std::string fmt(double value)
{
    return QString::number(value, 'g', 17).toStdString();
}

template <class... Args>
void line(std::string& out, std::format_string<Args...> f, Args&&... args)
{
    std::format_to(std::back_inserter(out), f, std::forward<Args>(args)...);
    out += '\n';
}

std::string usdAssetPath(const QString& path)
{
    QString p = QDir::fromNativeSeparators(path);
    p.replace(QStringLiteral("@"), QStringLiteral("@@"));
    return p.toStdString();
}

std::string matrixLiteral(const GfMatrix4d& m)
{
    std::string s = "(";
    for (int r = 0; r < 4; ++r) {
        s += std::format("{}({}, {}, {}, {})",
                         r ? ", " : "",
                         fmt(m[r][0]), fmt(m[r][1]), fmt(m[r][2]), fmt(m[r][3]));
    }
    s += ")";
    return s;
}

bool writeTextFile(const QString& path, const std::string& text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(text.data(), qsizetype(text.size()))
        == qsizetype(text.size());
}

// Simple bool post passes: each needs one apiSchema and one attribute line.
// Multi-parameter passes (DoF, tonemap, compositing) are handled inline.
struct PostEffect
{
    bool Settings::* flag;
    std::string_view apiSchema;
    std::string_view attr;
};

constexpr PostEffect kPostEffects[] = {
    {&Settings::bloomEnabled, "OmniRtxPostBloomPhysicalAPI_1",
     "        bool omni:rtx:post:bloom:enabled = 1\n"},
    {&Settings::colorGradingEnabled, "OmniRtxPostColorGradingAPI_1",
     "        bool omni:rtx:post:grade:enabled = 1\n"},
    {&Settings::chromaticAberrationEnabled, "OmniRtxPostChromaticAberrationAPI_1",
     "        bool omni:rtx:post:chromab:enabled = 1\n"},
    {&Settings::motionBlurEnabled, "OmniRtxPostMotionBlurAPI_1",
     "        bool omni:rtx:post:motionblur:enabled = 1\n"},
};

std::string_view tonemapOperator(Settings::Tonemap tm)
{
    using TM = Settings::Tonemap;
    switch (tm) {
        case TM::Raw:               return "raw";
        case TM::None:              return "none";
        case TM::Reinhard:          return "reinhard";
        case TM::ModifiedReinhard:  return "modifiedReinhard";
        case TM::HejiHableAlu:      return "hejiHableAlu";
        case TM::HableUc2:          return "hableUc2";
        case TM::AcesApproximation: return "acesApproximation";
        case TM::Iray:              return "iray";
        case TM::Default:           break;
    }
    return "acesApproximation";
}

QString exportStageWithDefaultPrim(const UsdStageRefPtr& stage,
                                   int32_t id,
                                   QString* error)
{
    auto fail = [&](QString msg) -> QString {
        if (error) *error = std::move(msg);
        return QString();
    };

    std::string flat;
    if (!stage->ExportToString(&flat, /*addSourceFileComment=*/false)
        || flat.empty()) {
        return fail(QStringLiteral("UsdStage::ExportToString() failed."));
    }

    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    if (!layer || !layer->ImportFromString(flat)) {
        return fail(QStringLiteral("Failed to parse flattened stage."));
    }

    // Referencing relies on a defaultPrim; fall back to the first root prim.
    if (!layer->HasDefaultPrim()) {
        for (const SdfPrimSpecHandle& root : layer->GetRootPrims()) {
            if (root) {
                layer->SetDefaultPrim(root->GetNameToken());
                break;
            }
        }
    }
    const QString path = QDir::tempPath() + QStringLiteral("/ovrtxmaya_multi_%1.usda").arg(id);
    if (!layer->Export(path.toStdString())) {
        return fail(QStringLiteral("Failed to write temporary stage file."));
    }
    return path;
}

// Anonymous, non-empty override layers plus the flattened session layer —
// the unsaved edits that must be serialized on top of the base scene.
std::vector<SdfLayerRefPtr> collectEditLayers(const UsdStageRefPtr& stage,
                                              const SdfLayerHandle& root,
                                              const SdfLayerHandle& sessionLayer)
{
    std::vector<SdfLayerRefPtr> layers;

    auto consider = [&](const SdfLayerRefPtr& layer) {
        if (!layer || layer == root || layer == sessionLayer) {
            return;
        }
        if (!layer->IsAnonymous() || layer->IsEmpty()) {
            return;
        }
        if (std::ranges::find(layers, layer) == layers.end()) {
            layers.push_back(layer);
        }
    };

    for (const auto& weak : stage->GetUsedLayers(/*includeClipLayers=*/false)) {
        consider(SdfLayerRefPtr(weak));
    }

    if (sessionLayer && !sessionLayer->IsEmpty()) {
        if (UsdStageRefPtr sessionStage =
                UsdStage::Open(sessionLayer, UsdStage::LoadNone)) {
            if (SdfLayerRefPtr flat = UsdUtilsFlattenLayerStack(sessionStage);
                flat && !flat->IsEmpty()) {
                layers.push_back(flat);
            }
        }
    }

    return layers;
}

// The base scene reference: the on-disk identifier when the root is clean,
// otherwise the whole stage exported to a temp file. Empty string on failure.
std::string serializeBaseLayer(const UsdStageRefPtr& stage,
                               const SdfLayerHandle& root,
                               bool rootClean,
                               int32_t renderId,
                               QString* error)
{
    if (rootClean) {
        return root->GetIdentifier();
    }

    std::string flat;
    if (!stage->ExportToString(&flat, /*addSourceFileComment=*/false)
        || flat.empty()) {
        if (error) *error = QStringLiteral("UsdStage::ExportToString() failed.");
        return {};
    }

    const QString basePath = QDir::tempPath()
        + QStringLiteral("/ovrtxmaya_base_%1.usda").arg(renderId);
    if (!writeTextFile(basePath, flat)) {
        if (error) {
            *error = QStringLiteral("Failed to write temporary RTX base stage.");
        }
        return {};
    }
    return basePath.toStdString();
}

bool openStageForRtx(OVRTXMAYA_NS::OvrtxRendererSession& renderer,
                     const UsdStageRefPtr& stage,
                     QString* error)
{
    auto fail = [&](QString msg) {
        if (error) *error = std::move(msg);
        return false;
    };

    if (!stage) {
        return fail(QStringLiteral("RTX render needs a loaded stage."));
    }

    const SdfLayerHandle root         = stage->GetRootLayer();
    const SdfLayerHandle sessionLayer = stage->GetSessionLayer();
    const bool rootClean = root && !root->IsAnonymous()
        && !root->GetIdentifier().empty() && !root->IsDirty();

    // Unique per render so temp stage files never collide — a reused path would
    // be served from USD's layer cache, re-rendering the previous scene.
    static int sRenderCounter {0};
    const int renderId = ++sRenderCounter;

    const std::vector<SdfLayerRefPtr> editLayers =
        collectEditLayers(stage, root, sessionLayer);

    const std::string baseRef =
        serializeBaseLayer(stage, root, rootClean, renderId, error);
    if (baseRef.empty()) {
        return false;
    }

    if (editLayers.empty()) {
        if (!renderer.openUsdFromFile(QString::fromStdString(baseRef))) {
            return fail(renderer.lastError());
        }
        qInfo("[ovrtxMaya] RTX opened base: %s", baseRef.c_str());
        return true;
    }

    QStringList editPaths;
    for (size_t i = 0; i < editLayers.size(); ++i) {
        const QString editPath = QDir::tempPath()
            + QStringLiteral("/ovrtxmaya_edits_%1_%2.usda").arg(renderId).arg(i);
        if (editLayers[i]->Export(editPath.toStdString())) {
            editPaths << editPath;
        }
    }

    // subLayers are strongest-first: edits override the base scene.
    std::string wrapper = "#usda 1.0\n(\n    subLayers = [\n";
    for (const QString& editPath : editPaths) {
        wrapper += "        @";
        wrapper += usdAssetPath(editPath);
        wrapper += "@,\n";
    }
    wrapper += "        @";
    wrapper += usdAssetPath(QString::fromStdString(baseRef));
    wrapper += "@,\n    ]\n)\n";

    // Fed to the renderer as a string; the file is only a debugging artifact.
    writeTextFile(QDir::tempPath() + QStringLiteral("/ovrtxmaya_stage.usda"),
                  wrapper);

    if (!renderer.openUsdFromString(wrapper)) {
        return fail(renderer.lastError());
    }
    qInfo("[ovrtxMaya] RTX opened with %d edit layer(s) over %s",
          int32_t(editPaths.size()), baseRef.c_str());
    return true;
}

} // namespace

namespace OVRTXMAYA_NS
{

std::string buildRtxOverlayUsda(const GfCamera& cam,
                                int32_t width,
                                int32_t height,
                                const Settings& settings)
{
    const GfMatrix4d xform = cam.GetTransform();
    const GfRange1f  clip  = cam.GetClippingRange();
    const bool isPerspective = cam.GetProjection() == GfCamera::Perspective;

    std::string usda;
    usda.reserve(4096);
    usda += "#usda 1.0\n(\n    defaultPrim = \"Root\"\n)\n\n";
    usda += "def Xform \"Root\"\n{\n";

    // OmniRtxCameraAutoExposureAPI_1 enables auto-exposure on the path tracer;
    // without it a scene lit by a single default dome renders near-black.
    if (settings.autoExposure) {
        usda += "    def Camera \"Camera\" (\n"
                "        prepend apiSchemas = [\"OmniRtxCameraAutoExposureAPI_1\"]\n"
                "    )\n    {\n";
    } else {
        usda += "    def Camera \"Camera\"\n    {\n";
    }

    // Aperture + offsets come from the caller's frustum math (Film Fit, film
    // offset, overscan, lens squeeze, 2D pan/zoom) — do not re-derive them.
    line(usda, "        float focalLength = {}", fmt(cam.GetFocalLength()));
    line(usda, "        float horizontalAperture = {}", fmt(cam.GetHorizontalAperture()));
    line(usda, "        float verticalAperture = {}", fmt(cam.GetVerticalAperture()));
    line(usda, "        float horizontalApertureOffset = {}", fmt(cam.GetHorizontalApertureOffset()));
    line(usda, "        float verticalApertureOffset = {}", fmt(cam.GetVerticalApertureOffset()));
    line(usda, "        float2 clippingRange = ({}, {})", fmt(clip.GetMin()), fmt(clip.GetMax()));
    line(usda, "        token projection = \"{}\"", isPerspective ? "perspective" : "orthographic");

    // Author fStop/focusDistance only for DoF; otherwise leave a pinhole.
    if (settings.dofEnabled) {
        line(usda, "        float fStop = {}", fmt(settings.dofFStop));
        line(usda, "        float focusDistance = {}", fmt(settings.dofSubjectDistance));
    }

    line(usda, "        matrix4d xformOp:transform = {}", matrixLiteral(xform));
    usda += "        uniform token[] xformOpOrder = [\"xformOp:transform\"]\n";
    usda += "    }\n\n";

    const OVRTXMAYA_NS::OvrtxRendererSession::AovDesc desc =
        OVRTXMAYA_NS::OvrtxRendererSession::describeAov(settings.aov);

    // Collect the apiSchemas the enabled passes require (order matters).
    std::vector<std::string_view> apiSchemas;
    if (settings.transparentBackground) {
        apiSchemas.push_back("OmniRtxPostCompositingAPI_1");
    }
    if (settings.dofEnabled) {
        apiSchemas.push_back("OmniRtxPostDofAPI_1");
    }
    for (const PostEffect& e : kPostEffects) {
        if (settings.*e.flag) {
            apiSchemas.push_back(e.apiSchema);
        }
    }
    if (settings.tonemap != Settings::Tonemap::Default) {
        apiSchemas.push_back("OmniRtxPostTonemapIrayReinhardAPI_1");
    }

    usda += "    def RenderProduct \"RenderProduct\"";
    if (!apiSchemas.empty()) {
        usda += " (\n        prepend apiSchemas = [";
        for (size_t i = 0; i < apiSchemas.size(); ++i) {
            if (i) usda += ", ";
            usda += '"';
            usda += apiSchemas[i];
            usda += '"';
        }
        usda += "]\n    )";
    }
    usda += "\n    {\n";

    line(usda, "        uniform int2 resolution = ({}, {})", width, height);
    usda += "        rel camera = </Root/Camera>\n";
    line(usda, "        rel orderedVars = </Root/RenderProduct/{}>", desc.primName);
    line(usda, "        token omni:rtx:rendermode = \"{}\"", desc.renderMode);
    line(usda, "        int omni:rtx:pt:samplesPerPixel = {}", settings.samplesPerPixel);

    // DoF post pass: fStop drives blur, subjectDistance the focus plane,
    // focalLength is reused from the lens so bokeh scales physically.
    if (settings.dofEnabled) {
        usda += "        bool omni:rtx:post:dof:enabled = 1\n";
        line(usda, "        float omni:rtx:post:dof:fStop = {}", fmt(settings.dofFStop));
        line(usda, "        float omni:rtx:post:dof:subjectDistance = {}", fmt(settings.dofSubjectDistance));
        line(usda, "        float omni:rtx:post:dof:focalLength = {}", fmt(cam.GetFocalLength()));
    }

    for (const PostEffect& e : kPostEffects) {
        if (settings.*e.flag) {
            usda += e.attr;
        }
    }

    if (settings.tonemap != Settings::Tonemap::Default) {
        line(usda, "        token omni:rtx:post:tonemap:operator = \"{}\"",
             tonemapOperator(settings.tonemap));
    }

    // Zero-alpha background matte on LdrColor (see ovrtx post-compositing docs).
    if (settings.transparentBackground) {
        usda += "        bool omni:rtx:post:compositing:enabled = 1\n"
                "        bool omni:rtx:post:compositing:blackBackground = 1\n"
                "        bool omni:rtx:post:compositing:doComposite = 0\n"
                "        bool omni:rtx:post:compositing:outputAlpha = 1\n"
                "        bool omni:rtx:post:compositing:premultiply = 1\n";
    }

    usda += "\n";
    line(usda, "        def RenderVar \"{}\"", desc.primName);
    usda += "        {\n";
    line(usda, "            uniform string sourceName = \"{}\"", desc.sourceName);
    usda += "        }\n";
    usda += "    }\n";
    usda += "}\n";
    return usda;
}

bool openStagesForRtx(OVRTXMAYA_NS::OvrtxRendererSession& renderer,
                      const std::vector<RtxStage>& stages,
                      QString* error)
{
    auto fail = [&](QString msg) {
        if (error) *error = std::move(msg);
        return false;
    };

    if (stages.empty()) {
        return fail(QStringLiteral("No stages to render."));
    }

    // Base stage opened in its own space (proven single-stage path).
    if (!openStageForRtx(renderer, stages[0].stage, error)) {
        return false;
    }
    if (stages.size() == 1) {
        return true;
    }

    // Place every other stage relative to the base:
    // stageToWorld_i * inverse(stageToWorld_0).
    const GfMatrix4d baseWorldInv = stages[0].stageToWorld.GetInverse();

    for (size_t i = 1; i < stages.size(); ++i) {
        QString       exportErr;
        const QString file =
            exportStageWithDefaultPrim(stages[i].stage, int(i), &exportErr);
        if (file.isEmpty()) {
            // Skip an un-exportable stage rather than failing the whole render.
            qWarning("[ovrtxMaya] skipping stage %d: %s",
                     int(i), exportErr.toUtf8().constData());
            continue;
        }

        const GfMatrix4d xform = stages[i].stageToWorld * baseWorldInv;

        std::string wrapper;
        wrapper += "#usda 1.0\n(\n    defaultPrim = \"Root\"\n)\n\n";
        wrapper += "def Xform \"Root\" (\n    references = @";
        wrapper += usdAssetPath(file);
        wrapper += "@\n)\n{\n";
        line(wrapper, "    matrix4d xformOp:transform = {}", matrixLiteral(xform));
        wrapper += "    uniform token[] xformOpOrder = [\"xformOp:transform\"]\n";
        wrapper += "}\n";

        const QString prefix = QStringLiteral("/OVRTX_MAYA_STAGE_%1").arg(i);
        if (!renderer.addUsdReferenceFromString(wrapper, prefix)) {
            return fail(renderer.lastError());
        }
        qInfo("[ovrtxMaya] composited stage %d at %s",
              int(i), prefix.toUtf8().constData());
    }

    return true;
}

} // namespace OVRTXMAYA_NS