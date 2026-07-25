#include "rtxOverlay.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range1f.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdUtils/dependencies.h>

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

// Writes a single layer's own opinions to a standalone .usda temp file that
// ovrtx can load by path, and returns that path.
QString serializeSingleLayer(const SdfLayerHandle& layer,
                             int32_t renderId,
                             int32_t index,
                             QString* error)
{
    auto fail = [&](QString msg) -> QString {
        if (error) *error = std::move(msg);
        return QString();
    };

    if (!layer) {
        return fail(QStringLiteral("null layer"));
    }

    SdfLayerRefPtr copy = SdfLayer::CreateAnonymous(".usda");
    if (!copy) {
        return fail(QStringLiteral("failed to create scratch layer"));
    }
    copy->TransferContent(layer);
    copy->SetSubLayerPaths({});

    // Rewrite every asset path (references, payloads, textures) to absolute so
    // they still resolve after the file moves to %TEMP%.
    UsdUtilsModifyAssetPaths(copy, [layer](const std::string& assetPath) -> std::string {
        if (assetPath.empty()) {
            return assetPath;
        }
        const std::string abs = SdfComputeAssetPathRelativeToLayer(layer, assetPath);
        return abs.empty() ? assetPath : abs;
    });

    const QString path = QDir::tempPath()
        + QStringLiteral("/ovrtxmaya_%1_%2.usda").arg(renderId).arg(index);
    if (!copy->Export(path.toStdString())) {
        return fail(QStringLiteral("failed to write temp layer: %1").arg(path));
    }
    return path;
}

struct LayerStackFeed
{
    std::vector<std::string> layerRefs;   
    std::string              singleCleanRoot;
    std::vector<QString>     tempFiles;
    bool                     ok {false};
    QString                  error;
};

// Prepares the set of USD files that describe "stage", to hand to ovrtx for
LayerStackFeed buildLayerStackFeed(const UsdStageRefPtr& stage, int32_t renderId)
{
    LayerStackFeed feed;
    auto fail = [&](QString msg) -> LayerStackFeed {
        feed.error = std::move(msg);
        feed.ok = false;
        return feed;
    };

    const SdfLayerHandle root = stage ? stage->GetRootLayer() : SdfLayerHandle();
    if (!root) {
        return fail(QStringLiteral("stage has no root layer"));
    }

    std::vector<SdfLayerHandle> kept;
    for (const SdfLayerHandle& layer : stage->GetLayerStack(/*includeSessionLayers=*/true)) {
        if (layer && !layer->IsEmpty()) {
            kept.push_back(layer);
        }
    }

    auto cleanOnDisk = [](const SdfLayerHandle& l) {
        return l && !l->IsAnonymous() && !l->IsDirty() && !l->GetRealPath().empty();
    };

    // If there are no unsaved/anonymous edits anywhere then hand ovrtx the on-disk root directly
    const SdfLayerHandle session = stage->GetSessionLayer();
    const bool sessionActive = session && !session->IsEmpty();
    bool anyEdits = false;
    bool anyDirtyOnDisk = false;
    for (const SdfLayerHandle& l : kept) {
        const bool anon = l->IsAnonymous();
        const bool dirty = l->IsDirty();
        anyEdits |= (anon || dirty);
        anyDirtyOnDisk |= (!anon && dirty);
    }
    if (!anyEdits && !sessionActive && !root->GetRealPath().empty()) {
        feed.singleCleanRoot = root->GetRealPath();
        feed.ok = true;
        return feed;
    }

    // Otherwise emit each layer strong->weak.
    int32_t index = 0;
    for (const SdfLayerHandle& layer : kept) {
        const bool canRefByPath = cleanOnDisk(layer)
            && !(anyDirtyOnDisk && !layer->GetSubLayerPaths().empty());
        if (canRefByPath) {
            feed.layerRefs.push_back(
                usdAssetPath(QString::fromStdString(layer->GetRealPath())));
            continue;
        }
        QString err;
        const QString temp = serializeSingleLayer(layer, renderId, index++, &err);
        if (temp.isEmpty()) {
            return fail(err);
        }
        feed.tempFiles.push_back(temp);
        feed.layerRefs.push_back(usdAssetPath(temp));
    }

    feed.ok = true;
    return feed;
}

std::string buildSubLayerWrapper(const std::vector<std::string>& layerRefs)
{
    std::string wrapper = "#usda 1.0\n(\n    subLayers = [\n";
    for (const std::string& ref : layerRefs) {
        wrapper += "        @";
        wrapper += ref;
        wrapper += "@,\n";
    }
    wrapper += "    ]\n)\n";
    return wrapper;
}

// Monotonic per-render id; keeps temp layer filenames unique so USD's layer
// cache never serves a stale scene from a reused path.
int32_t nextRenderId()
{
    static int32_t sRenderCounter {0};
    return ++sRenderCounter;
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

    const LayerStackFeed feed = buildLayerStackFeed(stage, nextRenderId());
    if (!feed.ok) {
        return fail(feed.error);
    }

    // Clean scene: hand ovrtx the on-disk root; it composes sublayers,
    // references, variants and MaterialX itself — nothing serialized.
    if (!feed.singleCleanRoot.empty()) {
        if (!renderer.openUsdFromFile(QString::fromStdString(feed.singleCleanRoot))) {
            return fail(renderer.lastError());
        }
        qInfo("[ovrtxMaya] RTX opened on-disk root: %s", feed.singleCleanRoot.c_str());
        return true;
    }

    // Edited scene: compose the local layer stack as a subLayers wrapper of
    // on-disk paths + small per-layer serializations.
    // TODO: for now serialized temp files are intentionally left on disk for debugging.
    const std::string wrapper = buildSubLayerWrapper(feed.layerRefs);
    if (!renderer.openUsdFromString(wrapper)) {
        return fail(renderer.lastError());
    }
    qInfo("[ovrtxMaya] RTX opened %d layer(s) (%d serialized)",
          int32_t(feed.layerRefs.size()), int32_t(feed.tempFiles.size()));
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

QString stageDefaultPrimName(const UsdStageRefPtr& stage)
{
    if (const UsdPrim dp = stage->GetDefaultPrim(); dp && dp.IsValid()) {
        return QString::fromStdString(dp.GetName().GetString());
    }
    for (const UsdPrim& child : stage->GetPseudoRoot().GetChildren()) {
        return QString::fromStdString(child.GetName().GetString());
    }
    return QString();
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
        const int32_t renderId = nextRenderId();

        LayerStackFeed feed = buildLayerStackFeed(stages[i].stage, renderId);
        if (!feed.ok) {
            qWarning("[ovrtxMaya] skipping stage %zu: %s",
                     i, feed.error.toUtf8().constData());
            continue;
        }

        const QString defaultPrim = stageDefaultPrimName(stages[i].stage);
        if (defaultPrim.isEmpty()) {
            qWarning("[ovrtxMaya] skipping stage %zu: no root prim to reference", i);
            continue;
        }

        // A referenceable file with an explicit defaultPrim
        std::vector<std::string> refs = feed.layerRefs;
        if (!feed.singleCleanRoot.empty()) {
            refs.push_back(usdAssetPath(QString::fromStdString(feed.singleCleanRoot)));
        }

        std::string wrapper = "#usda 1.0\n(\n";
        line(wrapper, "    defaultPrim = \"{}\"", defaultPrim.toStdString());
        wrapper += "    subLayers = [\n";
        for (const std::string& ref : refs) {
            wrapper += "        @";
            wrapper += ref;
            wrapper += "@,\n";
        }
        wrapper += "    ]\n)\n";

        const QString wrapperPath = QDir::tempPath()
            + QStringLiteral("/ovrtxmaya_%1_stage.usda").arg(renderId);
        if (!writeTextFile(wrapperPath, wrapper)) {
            qWarning("[ovrtxMaya] skipping stage %zu: cannot write %s",
                     i, wrapperPath.toUtf8().constData());
            continue;
        }

        const QString prefix = QStringLiteral("/OVRTX_MAYA_STAGE_%1").arg(i);
        if (!renderer.addUsdReferenceFromFile(wrapperPath, prefix)) {
            qWarning("[ovrtxMaya] skipping stage %zu: %s",
                     i, renderer.lastError().toUtf8().constData());
            continue;
        }

        // Position the referenced subtree
        const GfMatrix4d xform = stages[i].stageToWorld * baseWorldInv;
        double m[16];
        for (int32_t r = 0; r < 4; ++r) {
            for (int32_t c = 0; c < 4; ++c) {
                m[r * 4 + c] = xform[r][c];
            }
        }
        if (!renderer.setXform(prefix, m)) {
            qWarning("[ovrtxMaya] stage %zu placed at identity: %s",
                     i, renderer.lastError().toUtf8().constData());
        }

        qInfo("[ovrtxMaya] composited stage %zu at %s",
              i, prefix.toUtf8().constData());
    }

    return true;
}

} // namespace OVRTXMAYA_NS