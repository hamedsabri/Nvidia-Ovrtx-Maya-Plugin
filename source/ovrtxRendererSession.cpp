#include "ovrtxRendererSession.h"

#include <QByteArray>
#include <QDebug>

#include <ovrtx/ovrtx.h>
#include <ovrtx/ovrtx_types.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>

namespace OVRTXMAYA_NS
{

namespace
{
constexpr ovrtx_timeout_t kNoWait {0};
constexpr ovrtx_timeout_t kShortWait {10'000'000};       // 10 ms
constexpr ovrtx_timeout_t kLogFlushTimeout {1'000'000'000}; // 1 s

ovx_string_t toOvx(const QByteArray& bytes)
{
    return {bytes.constData(), static_cast<size_t>(bytes.size())};
}

ovx_string_t toOvx(const std::string& text)
{
    return {text.data(), text.size()};
}

QString ovxToQString(ovx_string_t text)
{
    if (!text.ptr || text.length == 0) {
        return {};
    }
    return QString::fromUtf8(text.ptr, qsizetype(text.length));
}

QString lastOvrtxError()
{
    const QString error = ovxToQString(ovrtx_get_last_error());
    return error.isEmpty() ? QStringLiteral("unknown ovrtx error") : error;
}

void ovrtxLogToQt(ovrtx_log_severity_t severity, double, ovx_string_t message,void*)
{
    const QByteArray text = ovxToQString(message).toUtf8();
    switch (severity) {
        case OVRTX_LOG_INFO:    qInfo("[ovrtx] %s", text.constData());     break;
        case OVRTX_LOG_WARNING: qWarning("[ovrtx] %s", text.constData());  break;
        case OVRTX_LOG_ERROR:
        case OVRTX_LOG_FATAL:   qCritical("[ovrtx] %s", text.constData()); break;
    }
}

QString lastOpError(ovrtx_op_id_t opId)
{
    const QString error = ovxToQString(ovrtx_get_last_op_error(opId));
    return error.isEmpty()
        ? QStringLiteral("operation %1 failed").arg(opId)
        : error;
}

// Joins the per-op error messages of a completed wait into one string.
QString collectOpErrors(const ovrtx_op_wait_result_t& wait)
{
    QStringList failures;
    for (const ovrtx_op_id_t opId : std::span(wait.error_op_ids, wait.num_error_ops)) {
        failures.push_back(lastOpError(opId));
    }
    return failures.join(QStringLiteral("; "));
}

bool waitOp(ovrtx_renderer_t* renderer, ovrtx_op_id_t opId, QString* error)
{
    if (!renderer || opId == OVRTX_INVALID_HANDLE) {
        if (error) {
            *error = QStringLiteral("invalid ovrtx operation handle");
        }
        return false;
    }

    ovrtx_op_wait_result_t wait {};
    const ovrtx_result_t result =
        ovrtx_wait_op(renderer, opId, ovrtx_timeout_infinite, &wait);

    if (result.status != OVRTX_API_SUCCESS) {
        if (error) {
            *error = QStringLiteral("ovrtx_wait_op failed: %1")
                .arg(lastOvrtxError());
        }
        return false;
    }

    if (wait.num_error_ops > 0 && wait.error_op_ids) {
        if (error) {
            *error = collectOpErrors(wait);
        }
        return false;
    }

    return true;
}

ovrtx_render_var_output_handle_t findRenderVar(const ovrtx_render_product_set_outputs_t& outputs,
                                               std::string_view renderVarName)
{
    for (const auto& product : std::span(outputs.outputs, outputs.output_count)) {
        for (const auto& frame : std::span(product.output_frames, product.output_frame_count)) {
            for (const auto& var : std::span(frame.output_render_vars, frame.render_var_count)) {
                if (!var.render_var_name.ptr) {
                    continue;
                }
                const std::string_view name(var.render_var_name.ptr, var.render_var_name.length);
                if (name == renderVarName) {
                    return var.output_handle;
                }
            }
        }
    }

    return OVRTX_INVALID_HANDLE;
}

// Format-specific tensor → QImage converters. All return RGBA8888
// images sized (W, H) where W = tensor.shape[1], H = tensor.shape[0].
// Returned image is null if the tensor doesn't match the expected
// layout for that AOV.
// LdrColor / DiffuseAlbedoSD: passthrough copy of uint8 [H,W,4] RGBA.
QImage decodeRgbaU8(const DLTensor& t)
{
    if (t.ndim != 3 || !t.shape || t.shape[2] != 4
        || t.dtype.code != kDLUInt || t.dtype.bits != 8
        || t.dtype.lanes != 1) {
        return QImage();
    }
    const int32_t W = int32_t(t.shape[1]);
    const int32_t H = int32_t(t.shape[0]);
    QImage img(W, H, QImage::Format_RGBA8888);
    const uchar* src = static_cast<const uchar*>(t.data);
    const size_t rowBytes = size_t(W) * 4u;
    for (int32_t y = 0; y < H; ++y) {
        std::memcpy(img.scanLine(y), src + y * rowBytes, rowBytes);
    }
    return img;
}

// NormalSD: float32 [H,W,4] world-space (nx, ny, nz, _). Standard
// (n + 1) * 0.5 encoding into RGB, alpha forced to 255. Pixels with
// no surface (zero-length normal) come out grey.
QImage decodeNormalsF32(const DLTensor& t)
{
    if (t.ndim != 3 || !t.shape || t.shape[2] != 4
        || t.dtype.code != kDLFloat || t.dtype.bits != 32
        || t.dtype.lanes != 1) {
        return QImage();
    }
    const int32_t W = int32_t(t.shape[1]);
    const int32_t H = int32_t(t.shape[0]);
    QImage img(W, H, QImage::Format_RGBA8888);
    const float* src = static_cast<const float*>(t.data);
    for (int32_t y = 0; y < H; ++y) {
        uchar* dst = img.scanLine(y);
        for (int32_t x = 0; x < W; ++x) {
            const float* n = src + ((size_t(y) * W + x) * 4);
            auto enc = [](float v) {
                const float c = (v * 0.5f + 0.5f) * 255.0f;
                return uchar(std::clamp(int32_t(c), 0, 255));
            };
            dst[x * 4 + 0] = enc(n[0]);
            dst[x * 4 + 1] = enc(n[1]);
            dst[x * 4 + 2] = enc(n[2]);
            dst[x * 4 + 3] = 255;
        }
    }
    return img;
}

QImage decodeAovTensor(OvrtxRendererSession::Aov aov, const DLTensor& t)
{
    using A = OvrtxRendererSession::Aov;
    switch (aov) {
        case A::LdrColor:
        case A::DiffuseAlbedoSD:      return decodeRgbaU8(t);
        case A::NormalSD:             return decodeNormalsF32(t);
    }
    return QImage();
}

} // namespace

OvrtxRendererSession::AovDesc OvrtxRendererSession::describeAov(Aov aov)
{
    switch (aov) {
    case Aov::LdrColor:
        return {"LdrColor", "LdrColor", "PathTracing"};
    case Aov::DiffuseAlbedoSD:
        return {"DiffuseAlbedoSD", "DiffuseAlbedoSD", "PathTracing"};
    case Aov::NormalSD:
        return {"NormalSD", "NormalSD", "PathTracing"};
    }
    return {"LdrColor", "LdrColor", "PathTracing"};
}

OvrtxRendererSession::~OvrtxRendererSession()
{
    shutdown();
}

bool OvrtxRendererSession::initialize()
{
    clearError();

    if (isReady()) {
        return true;
    }

    const ovrtx_config_t ovrtxConfig {};
    ovrtx_renderer_t* renderer = nullptr;
    const ovrtx_result_t result =
        ovrtx_create_renderer(&ovrtxConfig, &renderer);
    if (result.status != OVRTX_API_SUCCESS) {
        m_lastError = QStringLiteral("ovrtx_create_renderer failed: %1")
            .arg(lastOvrtxError());
        qWarning("[OvrtxRendererSession] %s", m_lastError.toUtf8().constData());
        return false;
    }

    m_renderer = renderer;

    uint32_t major = 0, minor = 0, patch = 0;
    ovrtx_get_version(&major, &minor, &patch);
    qInfo("[OvrtxRendererSession] ovrtx %u.%u.%u", major, minor, patch);

    // System is initialized once the renderer exists; route ovrtx's log to Qt.
    ovrtx_set_log_callback(OVRTX_LOG_WARNING, nullptr, &ovrtxLogToQt, nullptr);
    return true;
}

void OvrtxRendererSession::shutdown()
{
    if (!m_renderer) {
        return;
    }

    // Detach the log callback before teardown (per ovrtx guidance).
    ovrtx_flush_log(kLogFlushTimeout);
    ovrtx_set_log_callback(OVRTX_LOG_WARNING, nullptr, nullptr, nullptr);

    const ovrtx_result_t result = ovrtx_destroy_renderer(m_renderer);
    if (result.status != OVRTX_API_SUCCESS) {
        m_lastError = QStringLiteral("ovrtx_destroy_renderer failed: %1")
            .arg(lastOvrtxError());
        qWarning("[OvrtxRendererSession] %s", m_lastError.toUtf8().constData());
    }
    m_renderer = nullptr;
}

bool OvrtxRendererSession::isReady() const
{
    return m_renderer != nullptr;
}

bool OvrtxRendererSession::openUsdFromFile(const QString& filePath)
{
    clearError();

    if (!isReady() && !initialize()) {
        return false;
    }

    const QByteArray pathBytes = filePath.toUtf8();
    const ovrtx_enqueue_result_t enqueue =
        ovrtx_open_usd_from_file(m_renderer, toOvx(pathBytes));
    if (enqueue.status != OVRTX_API_SUCCESS) {
        m_lastError = QStringLiteral("ovrtx_open_usd_from_file failed: %1")
            .arg(lastOvrtxError());
        return false;
    }

    return waitOp(m_renderer, enqueue.op_index, &m_lastError);
}

bool OvrtxRendererSession::openUsdFromString(const std::string& usdaContent)
{
    clearError();

    if (!isReady() && !initialize()) {
        return false;
    }

    const ovrtx_enqueue_result_t enqueue = ovrtx_open_usd_from_string(m_renderer, toOvx(usdaContent));
    if (enqueue.status != OVRTX_API_SUCCESS) {
        m_lastError = QStringLiteral("ovrtx_open_usd_from_string failed: %1")
            .arg(lastOvrtxError());
        return false;
    }

    return waitOp(m_renderer, enqueue.op_index, &m_lastError);
}

bool OvrtxRendererSession::addUsdReferenceFromString(
    const std::string& usdaContent,
    const QString& prefixPath)
{
    clearError();

    if (usdaContent.empty()) {
        m_lastError = QStringLiteral("empty ovrtx USD reference content");
        return false;
    }
    if (prefixPath.isEmpty()) {
        m_lastError = QStringLiteral("empty ovrtx USD reference prefix");
        return false;
    }
    if (!isReady() && !initialize()) {
        return false;
    }

    const QByteArray prefixBytes = prefixPath.toUtf8();
    const ovrtx_enqueue_result_t enqueue = ovrtx_add_usd_reference_from_string(m_renderer,
                                                                              toOvx(usdaContent),
                                                                              toOvx(prefixBytes),
                                                                              &m_overlayHandle);
    if (enqueue.status != OVRTX_API_SUCCESS) {
        m_lastError =
            QStringLiteral("ovrtx_add_usd_reference_from_string failed: %1")
                .arg(lastOvrtxError());
        return false;
    }

    if (!waitOp(m_renderer, enqueue.op_index, &m_lastError)) {
        m_lastError =
            QStringLiteral("ovrtx_add_usd_reference_from_string failed: %1")
                .arg(m_lastError);
        return false;
    }
    return true;
}

bool OvrtxRendererSession::resetSimulation(double time)
{
    clearError();

    if (!isReady()) {
        return true;
    }

    const ovrtx_enqueue_result_t enqueue = ovrtx_reset(m_renderer, time);
    if (enqueue.status != OVRTX_API_SUCCESS) {
        m_lastError = QStringLiteral("ovrtx_reset failed: %1")
            .arg(lastOvrtxError());
        return false;
    }

    return waitOp(m_renderer, enqueue.op_index, &m_lastError);
}

bool OvrtxRendererSession::resetStage()
{
    clearError();

    if (!isReady()) {
        return true;
    }

    const ovrtx_enqueue_result_t enqueue = ovrtx_reset_stage(m_renderer);
    if (enqueue.status != OVRTX_API_SUCCESS) {
        m_lastError = QStringLiteral("ovrtx_reset_stage failed: %1")
            .arg(lastOvrtxError());
        return false;
    }

    return waitOp(m_renderer, enqueue.op_index, &m_lastError);
}

bool OvrtxRendererSession::beginRender(const QString& renderProductPath,
                                       Aov aov,
                                       double deltaSeconds)
{
    clearError();

    if (renderProductPath.isEmpty()) {
        m_lastError = QStringLiteral("empty ovrtx render product path");
        return false;
    }
    if (!isReady() && !initialize()) {
        return false;
    }

    endRender();
    m_image = QImage();
    m_aov = aov;
    m_converged = false;
    m_progress = 0.0;
    m_phase = Phase::Idle;
    m_outputs = {};
    m_outputHandle = OVRTX_INVALID_HANDLE;

    m_renderProductPathBytes = renderProductPath.toUtf8();
    m_renderProductPath = toOvx(m_renderProductPathBytes);
    m_renderProducts = {};
    m_renderProducts.render_products = &m_renderProductPath;
    m_renderProducts.num_render_products = 1;

    const ovrtx_enqueue_result_t step = ovrtx_step(m_renderer,
                                                   m_renderProducts,
                                                   deltaSeconds,
                                                   &m_stepResult);
    if (step.status != OVRTX_API_SUCCESS) {
        m_lastError = QStringLiteral("ovrtx_step enqueue failed: %1").arg(lastOvrtxError());
        m_stepResult = OVRTX_INVALID_HANDLE;
        return false;
    }

    m_stepOpId = step.op_index;
    m_phase = Phase::WaitingStep;
    m_active = true;
    qInfo("[OvrtxRendererSession] ovrtx_step queued: op=%llu result=%llu",
          static_cast<unsigned long long>(m_stepOpId),
          static_cast<unsigned long long>(m_stepResult));
    return true;
}

bool OvrtxRendererSession::pollRender()
{
    clearError();

    if (!isReady()) {
        m_lastError = QStringLiteral("ovrtx renderer is not initialized");
        return false;
    }
    if (!m_active || m_converged) {
        return true;
    }

    if (m_phase == Phase::WaitingStep) {
        // Snapshot progress for the UI; paired release required.
        ovrtx_op_status_t status {};
        if (ovrtx_query_op_status(m_renderer, m_stepOpId, &status).status
            == OVRTX_API_SUCCESS) {
            m_progress = status.progress;
            ovrtx_release_op_status(m_renderer, &status);
        }

        ovrtx_op_wait_result_t wait {};
        const ovrtx_result_t waitResult =
            ovrtx_wait_op(m_renderer,
                          m_stepOpId,
                          kNoWait,
                          &wait);
        if (waitResult.status == OVRTX_API_TIMEOUT) {
            return true;
        }
        if (waitResult.status != OVRTX_API_SUCCESS) {
            m_lastError = QStringLiteral("ovrtx_step failed: %1").arg(lastOvrtxError());
            m_active = false;
            m_phase = Phase::Idle;
            return false;
        }
        if (wait.num_error_ops > 0 && wait.error_op_ids) {
            m_lastError = collectOpErrors(wait);
            m_active = false;
            m_phase = Phase::Idle;
            return false;
        }

        qInfo("[OvrtxRendererSession] ovrtx_step completed");
        m_phase = Phase::FetchingResults;
    }

    if (m_phase == Phase::FetchingResults) {
        const ovrtx_result_t fetch =
            ovrtx_fetch_results(m_renderer,
                                m_stepResult,
                                kShortWait,
                                &m_outputs);
        if (fetch.status == OVRTX_API_TIMEOUT) {
            return true;
        }
        if (fetch.status != OVRTX_API_SUCCESS) {
            m_lastError = QStringLiteral("ovrtx_fetch_results failed: %1").arg(lastOvrtxError());
            m_active = false;
            m_phase = Phase::Idle;
            return false;
        }

        const char* sourceName = describeAov(m_aov).sourceName;
        m_outputHandle = findRenderVar(m_outputs, sourceName);
        if (m_outputHandle == OVRTX_INVALID_HANDLE) {
            m_lastError = QStringLiteral("%1 output not found").arg(QString::fromLatin1(sourceName));
            m_active = false;
            m_phase = Phase::Idle;
            return false;
        }

        qInfo("[OvrtxRendererSession] %s output fetched", sourceName);
        m_phase = Phase::MappingOutput;
    }

    ovrtx_map_output_description_t mapDesc {};
    mapDesc.device_type = OVRTX_MAP_DEVICE_TYPE_CPU;

    ovrtx_render_var_output_t renderVar {};
    const ovrtx_result_t map =
        ovrtx_map_render_var_output(m_renderer,
                                    m_outputHandle,
                                    &mapDesc,
                                    kShortWait,
                                    &renderVar);
    if (map.status == OVRTX_API_TIMEOUT) {
        return true;
    }
    if (map.status != OVRTX_API_SUCCESS) {
        m_lastError = QStringLiteral("ovrtx_map_render_var_output failed: %1")
            .arg(lastOvrtxError());
        m_active = false;
        m_phase = Phase::Idle;
        return false;
    }

    QImage image;
    bool ok = false;
    if (renderVar.num_tensors != 1 || !renderVar.tensors ||
        !renderVar.tensors[0].dl) {
        m_lastError = QStringLiteral(
            "unexpected render output; expected exactly one tensor");
    } else {
        image = decodeAovTensor(m_aov, *renderVar.tensors[0].dl);
        ok = !image.isNull();
        if (!ok) {
            m_lastError = QStringLiteral("failed to decode %1 tensor")
                .arg(QString::fromLatin1(describeAov(m_aov).sourceName));
        }
    }

    ovrtx_cuda_sync_t noSync {};
    const ovrtx_result_t unmap = ovrtx_unmap_render_var_output(m_renderer, renderVar.map_handle, noSync);
    if (unmap.status != OVRTX_API_SUCCESS && ok) {
        m_lastError = QStringLiteral("ovrtx_unmap_render_var_output failed: %1")
            .arg(lastOvrtxError());
        ok = false;
    }

    if (!ok) {
        m_active = false;
        m_phase = Phase::Idle;
        return false;
    }

    m_image = std::move(image);
    m_converged = true;
    m_active = false;
    m_phase = Phase::Idle;
    qInfo("[OvrtxRendererSession] %s mapped to QImage", describeAov(m_aov).sourceName);
    endRender();
    return true;
}

bool OvrtxRendererSession::renderConverged() const
{
    return m_converged;
}

double OvrtxRendererSession::renderProgress() const
{
    return m_progress;
}

QImage OvrtxRendererSession::readImage() const
{
    return m_image;
}

void OvrtxRendererSession::resetRenderState()
{
    m_stepResult = OVRTX_INVALID_HANDLE;
    m_stepOpId = OVRTX_INVALID_HANDLE;
    m_outputs = {};
    m_outputHandle = OVRTX_INVALID_HANDLE;
    m_renderProductPathBytes.clear();
    m_renderProductPath = {};
    m_renderProducts = {};
    m_overlayHandle = OVRTX_INVALID_HANDLE;
    m_phase = Phase::Idle;
    m_progress = -1.0;
    m_active = false;
}

void OvrtxRendererSession::endRender()
{
    if (!m_renderer) {
        return;
    }
    if (m_stepResult != OVRTX_INVALID_HANDLE) {
        ovrtx_destroy_results(m_renderer, m_stepResult);
    }
    resetRenderState();
}

void OvrtxRendererSession::abandonRender()
{
    // ovrtx has no cancel API?
    if (m_renderer && m_stepResult != OVRTX_INVALID_HANDLE) {
        ovrtx_destroy_results(m_renderer, m_stepResult);
    }
    resetRenderState();
    m_converged = false;
    m_lastError = QStringLiteral("RTX render abandoned");
}

} // namespace OVRTXMAYA_NS