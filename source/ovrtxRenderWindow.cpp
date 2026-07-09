#include "ovrtxRenderWindow.h"
#include "mayaSceneBridge.h"
#include "rtxOverlay.h"

#include <pxr/base/gf/camera.h>
#include <pxr/usd/usd/stage.h>

#include <maya/MGlobal.h>
#include <maya/MSceneMessage.h>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

// Renderer polling cadence (ms)
constexpr auto kPollIntervalMs = 33;
constexpr auto kWindowWidth = 1024;
constexpr auto kWindowHeight = 800;

namespace OVRTXMAYA_NS
{

OvrtxRenderWindow::OvrtxRenderWindow(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("OvrtxRenderWindow"));
    setWindowTitle(QStringLiteral("ovrtx Render View"));

    // Display area
    m_imageLabel = new QLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setText(QStringLiteral("No image. Press Render."));
    m_imageLabel->setMinimumSize(320, 240);

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(m_imageLabel);
    scroll->setWidgetResizable(false);
    scroll->setAlignment(Qt::AlignCenter);

    // Output row
    m_width = new QSpinBox(this);
    m_width->setRange(16, 16384);
    m_width->setValue(1280);
    m_height = new QSpinBox(this);
    m_height->setRange(16, 16384);
    m_height->setValue(720);

    m_aov = new QComboBox(this);
    m_aov->addItems({QStringLiteral("LdrColor"),
                     QStringLiteral("Diffuse Albedo"),
                     QStringLiteral("Normal")});

    m_samples = new QSpinBox(this);
    m_samples->setRange(1, 8192);
    m_samples->setValue(64);

    auto* fromViewport = new QPushButton(QStringLiteral("From Viewport"), this);
    fromViewport->setToolTip(QStringLiteral(
        "Match width/height to the active 3D viewport size."));

    auto* outRow = new QHBoxLayout;
    outRow->addWidget(new QLabel(QStringLiteral("Width:"), this));
    outRow->addWidget(m_width);
    outRow->addWidget(new QLabel(QStringLiteral("Height:"), this));
    outRow->addWidget(m_height);
    outRow->addWidget(fromViewport);
    outRow->addWidget(new QLabel(QStringLiteral("AOV:"), this));
    outRow->addWidget(m_aov);
    outRow->addWidget(new QLabel(QStringLiteral("Samples:"), this));
    outRow->addWidget(m_samples);
    outRow->addStretch(1);

    // Channel selector — view RGB or an isolated R/G/B/A channel (grayscale).
    auto* channelGroup = new QButtonGroup(this);
    auto* channelRow = new QHBoxLayout;
    channelRow->addWidget(new QLabel(QStringLiteral("Channel:"), this));
    const std::pair<const char*, Channel> channels[] = {
        {"RGB", Channel::RGB}, {"R", Channel::R}, {"G", Channel::G},
        {"B", Channel::B}, {"A", Channel::A},
    };
    for (const auto& item : channels) {
        auto* button = new QRadioButton(QString::fromLatin1(item.first), this);
        button->setChecked(item.second == Channel::RGB);
        channelGroup->addButton(button, int(item.second));
        channelRow->addWidget(button);
    }
    channelRow->addStretch(1);

    // Lighting / exposure
    m_autoExposure = new QCheckBox(QStringLiteral("Auto exposure"), this);
    m_autoExposure->setChecked(true);
    m_transparent = new QCheckBox(QStringLiteral("Transparent bg"), this);

    auto* lightRow = new QHBoxLayout;
    lightRow->addWidget(m_autoExposure);
    lightRow->addWidget(m_transparent);
    lightRow->addStretch(1);

    // Depth of field
    m_dof = new QCheckBox(QStringLiteral("Depth of field"), this);
    m_fstop = new QDoubleSpinBox(this);
    m_fstop->setRange(0.1, 256.0);
    m_fstop->setValue(5.6);
    m_focusDist = new QDoubleSpinBox(this);
    m_focusDist->setRange(0.001, 1000000.0);
    m_focusDist->setValue(100.0);

    auto* dofRow = new QHBoxLayout;
    dofRow->addWidget(m_dof);
    dofRow->addWidget(new QLabel(QStringLiteral("f-stop:"), this));
    dofRow->addWidget(m_fstop);
    dofRow->addWidget(new QLabel(QStringLiteral("focus dist:"), this));
    dofRow->addWidget(m_focusDist);
    dofRow->addStretch(1);

    // Post passes
    m_bloom = new QCheckBox(QStringLiteral("Bloom"), this);
    m_grade = new QCheckBox(QStringLiteral("Color grade"), this);
    m_chromab = new QCheckBox(QStringLiteral("Chromatic ab."), this);
    m_motionBlur = new QCheckBox(QStringLiteral("Motion blur"), this);
    m_tonemap = new QComboBox(this);
    m_tonemap->addItems({QStringLiteral("Default"),
                         QStringLiteral("Raw"),
                         QStringLiteral("None"),
                         QStringLiteral("Reinhard"),
                         QStringLiteral("Modified Reinhard"),
                         QStringLiteral("Heji Hable ALU"),
                         QStringLiteral("Hable UC2"),
                         QStringLiteral("ACES Approx."),
                         QStringLiteral("Iray")});

    auto* postRow = new QHBoxLayout;
    postRow->addWidget(m_bloom);
    postRow->addWidget(m_grade);
    postRow->addWidget(m_chromab);
    postRow->addWidget(m_motionBlur);
    postRow->addWidget(new QLabel(QStringLiteral("Tonemap:"), this));
    postRow->addWidget(m_tonemap);
    postRow->addStretch(1);

    // Actions + status
    m_renderBtn = new QPushButton(QStringLiteral("Render"), this);
    m_stopBtn = new QPushButton(QStringLiteral("Stop"), this);
    m_stopBtn->setEnabled(false);
    m_saveBtn = new QPushButton(QStringLiteral("Save…"), this);
    m_saveBtn->setEnabled(false);
    m_status = new QLabel(QStringLiteral("Ready."), this);

    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* actionRow = new QHBoxLayout;
    actionRow->addWidget(m_renderBtn);
    actionRow->addWidget(m_stopBtn);
    actionRow->addWidget(m_saveBtn);
    actionRow->addStretch(1);

    auto* controls = new QVBoxLayout;
    controls->addLayout(outRow);
    controls->addLayout(channelRow);
    controls->addLayout(lightRow);
    controls->addLayout(dofRow);
    controls->addLayout(postRow);
    controls->addLayout(actionRow);
    controls->addWidget(m_status);

    auto* root = new QVBoxLayout(this);
    root->addWidget(scroll, 1);
    root->addLayout(controls);

    m_timer = new QTimer(this);
    m_timer->setInterval(kPollIntervalMs);

    connect(m_renderBtn, &QPushButton::clicked, this,
            &OvrtxRenderWindow::onRenderClicked);
    connect(m_stopBtn, &QPushButton::clicked, this,
            &OvrtxRenderWindow::onStopClicked);
    connect(m_saveBtn, &QPushButton::clicked, this,
            &OvrtxRenderWindow::onSaveClicked);
    connect(fromViewport, &QPushButton::clicked, this,
            &OvrtxRenderWindow::onFromViewportClicked);
    connect(channelGroup, &QButtonGroup::idClicked, this,
            &OvrtxRenderWindow::onChannelChanged);
    connect(m_timer, &QTimer::timeout, this, &OvrtxRenderWindow::onTick);

    // The widget is retained across Maya scenes; reset when the scene is
    // replaced so a stale render/proxy can't leak into the next scene.
    m_sceneCallbacks.append(MSceneMessage::addCallback(
        MSceneMessage::kBeforeNew, &OvrtxRenderWindow::onSceneChanged, this));
    m_sceneCallbacks.append(MSceneMessage::addCallback(
        MSceneMessage::kBeforeOpen, &OvrtxRenderWindow::onSceneChanged, this));

    resize(kWindowWidth, kWindowHeight);
}

OvrtxRenderWindow::~OvrtxRenderWindow()
{
    MMessage::removeCallbacks(m_sceneCallbacks);
    if (m_ovrtx && m_rendering) {
        m_ovrtx->abandonRender();
    }
}

void OvrtxRenderWindow::onSceneChanged(void* clientData)
{
    if (auto* self = static_cast<OvrtxRenderWindow*>(clientData)) {
        self->resetForSceneChange();
    }
}

void OvrtxRenderWindow::resetForSceneChange()
{
    stopRender();
    m_proxyName.clear();   // the -node target no longer exists after New/Open
    m_finalImage = QImage();
    if (m_saveBtn) {
        m_saveBtn->setEnabled(false);
    }
    refreshDisplay();
    setStatus(QStringLiteral("Scene changed — press Render."));
}

void OvrtxRenderWindow::closeEvent(QCloseEvent* event)
{
    stopRender();
    QWidget::closeEvent(event);
}

OvrtxRendererSession::Settings OvrtxRenderWindow::collectSettings() const
{
    OvrtxRendererSession::Settings s;
    s.aov = static_cast<OvrtxRendererSession::Aov>(m_aov->currentIndex());
    s.samplesPerPixel = m_samples->value();
    s.autoExposure = m_autoExposure->isChecked();
    s.transparentBackground = m_transparent->isChecked();
    s.dofEnabled = m_dof->isChecked();
    s.dofFStop = static_cast<float>(m_fstop->value());
    s.dofSubjectDistance = static_cast<float>(m_focusDist->value());
    s.bloomEnabled = m_bloom->isChecked();
    s.colorGradingEnabled = m_grade->isChecked();
    s.chromaticAberrationEnabled = m_chromab->isChecked();
    s.motionBlurEnabled = m_motionBlur->isChecked();
    s.tonemap = static_cast<OvrtxRendererSession::Settings::Tonemap>(m_tonemap->currentIndex());
    return s;
}

bool OvrtxRenderWindow::ensureOvrtxReady()
{
    if (m_ovrtx && m_ovrtx->isReady()) {
        return true;
    }
    if (!m_ovrtx) {
        m_ovrtx = std::make_unique<OvrtxRendererSession>();
    }

    if (!m_ovrtx->initialize()) {
        setStatus(QStringLiteral("Renderer init failed: %1")
                      .arg(m_ovrtx->lastError()));
        return false;
    }
    return true;
}

void OvrtxRenderWindow::onRenderClicked()
{
    if (m_rendering) {
        return;
    }

    m_renderBtn->setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    struct CursorGuard {
        ~CursorGuard() { QApplication::restoreOverrideCursor(); }
    } cursorGuard;

    // Gather the proxy-shape stage(s) to render.
    setStatus(QStringLiteral("Finding USD stage(s)…"));
    MString                      err;
    std::vector<ProxyStageEntry> entries;
    if (!m_proxyName.isEmpty()) {
        MMatrix proxyWorld = MMatrix::identity;
        if (UsdStageRefPtr s = findProxyShapeStage(
                m_proxyName.toUtf8().constData(), &proxyWorld, &err)) {
            entries.push_back({s, proxyWorld});
        }
    } else {
        entries = findAllProxyShapeStages(&err);
    }
    if (entries.empty()) {
        setStatus(QString::fromUtf8(err.asUTF8()));
        m_renderBtn->setEnabled(true);
        return;
    }

    // The renderer only lights the scene from USD lights in the stage (Maya
    // native lights are not part of it). Warn, but still render, when unlit.
    m_unlitScene = std::none_of(
        entries.begin(), entries.end(),
        [](const ProxyStageEntry& e) { return stageHasLight(e.stage); });
    if (m_unlitScene) {
        MGlobal::displayWarning(
            "[ovrtxRender] No USD lights in the stage — add a USD light "
            "(e.g. a DomeLight or SphereLight prim). The render will be dark.");
    }

    // Map each stage into the common render space (the first stage's space).
    std::vector<RtxStage> rtxStages;
    rtxStages.reserve(entries.size());
    for (const ProxyStageEntry& e : entries) {
        rtxStages.push_back(
            {e.stage, usdStageToMayaWorld(e.stage, e.proxyWorld)});
    }

    // Active viewport camera, framed for the chosen output resolution so it
    // matches Maya's resolution gate (Film Fit, offsets, overscan, pan/zoom).
    const auto renderW = m_width->value();
    const auto renderH = m_height->value();
    const auto renderAspect = (renderH > 0) ? (double(renderW) / double(renderH)) : 1.0;
    GfCamera cam;
    if (!activeViewportCamera(renderAspect, &cam, &err)) {
        setStatus(QString::fromUtf8(err.asUTF8()));
        m_renderBtn->setEnabled(true);
        return;
    }
    cam.SetTransform(cam.GetTransform() * rtxStages.front().stageToWorld.GetInverse());

    // Renderer. The first call initializes the ovrtx system + creates the
    // renderer, which can take several seconds.
    const bool firstRun = !(m_ovrtx && m_ovrtx->isReady());
    setStatus(firstRun
                  ? QStringLiteral("Initializing ovrtx renderer "
                                   "(first run can take a while)…")
                  : QStringLiteral("Preparing renderer…"));
    if (!ensureOvrtxReady()) {
        m_renderBtn->setEnabled(true);
        return;
    }

    // Feed the stage(s), then overlay camera + render product.
    setStatus(QStringLiteral("Loading %1 USD stage(s) into renderer…")
                  .arg(rtxStages.size()));
    QString openErr;
    if (!openStagesForRtx(*m_ovrtx, rtxStages, &openErr)) {
        setStatus(QStringLiteral("Open stage failed: %1").arg(openErr));
        m_renderBtn->setEnabled(true);
        return;
    }

    const OvrtxRendererSession::Settings settings = collectSettings();
    const std::string overlay = buildRtxOverlayUsda(cam, renderW, renderH, settings);
    if (!m_ovrtx->addUsdReferenceFromString(overlay, QString::fromLatin1(kRtxOverlayRoot))) {
        setStatus(QStringLiteral("Overlay failed: %1")
                      .arg(m_ovrtx->lastError()));
        m_renderBtn->setEnabled(true);
        return;
    }

    m_ovrtx->resetSimulation(0.0);

    setStatus(QStringLiteral("Starting render…"));
    if (!m_ovrtx->beginRender(QString::fromLatin1(kRtxRenderProductPath),
                              settings.aov,
                              1.0 / 60.0)) {
        setStatus(QStringLiteral("beginRender failed: %1")
                      .arg(m_ovrtx->lastError()));
        m_renderBtn->setEnabled(true);
        return;
    }

    setRenderingUi(true);
    setStatus(firstRun
                  ? QStringLiteral("Rendering (first frame compiles shaders)…")
                  : QStringLiteral("Rendering…"));
    m_timer->start();
}

void OvrtxRenderWindow::onTick()
{
    if (!m_rendering || !m_ovrtx) {
        return;
    }

    if (!m_ovrtx->pollRender()) {
        setStatus(QStringLiteral("Render error: %1").arg(m_ovrtx->lastError()));
        stopRender();
        return;
    }

    if (m_ovrtx->renderConverged()) {
        m_finalImage = m_ovrtx->readImage();
        refreshDisplay();
        m_ovrtx->endRender();
        m_ovrtx->resetStage();
        setRenderingUi(false);
        m_timer->stop();
        m_rendering = false;
        m_saveBtn->setEnabled(!m_finalImage.isNull());
        if (m_finalImage.isNull()) {
            setStatus(QStringLiteral("Render finished but image is empty."));
        } else {
            QString msg = QStringLiteral("Render complete (%1×%2).")
                              .arg(m_finalImage.width())
                              .arg(m_finalImage.height());
            if (m_unlitScene) {
                msg += QStringLiteral(" No lights in stage — add a USD light.");
            }
            setStatus(msg);
        }
    } else {
        const double p = m_ovrtx->renderProgress();
        if (p >= 0.0) {
            setStatus(QStringLiteral("Rendering… %1%")
                          .arg(int(p * 100.0)));
        }
    }
}

void OvrtxRenderWindow::onStopClicked()
{
    stopRender();
    setStatus(QStringLiteral("Stopped."));
}

void OvrtxRenderWindow::stopRender()
{
    if (!m_rendering) {
        return;
    }
    m_timer->stop();
    if (m_ovrtx) {
        m_ovrtx->abandonRender();
    }
    m_rendering = false;
    setRenderingUi(false);
}

void OvrtxRenderWindow::onSaveClicked()
{
    // Save exactly what is displayed: the full RGBA image for the RGB view, or
    // the isolated grayscale channel (R/G/B/A) when a single channel is picked.
    const QImage image = imageForCurrentChannel();
    if (image.isNull()) {
        return;
    }

    // Tag the suggested filename with the channel so saving channels
    // individually doesn't silently overwrite the previous one.
    const auto channelSuffix = [](Channel c) -> QString {
        using enum Channel;
        switch (c) {
        case RGB: return QString();
        case R:   return QStringLiteral("_R");
        case G:   return QStringLiteral("_G");
        case B:   return QStringLiteral("_B");
        case A:   return QStringLiteral("_A");
        }
        return QString();
    };
    const QString suggested =
        QStringLiteral("render%1.png").arg(channelSuffix(m_channel));

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save rendered image"), suggested,
        QStringLiteral("PNG (*.png);;JPEG (*.jpg);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (image.save(path)) {
        setStatus(QStringLiteral("Saved: %1").arg(path));
    } else {
        setStatus(QStringLiteral("Failed to save: %1").arg(path));
    }
}

void OvrtxRenderWindow::onFromViewportClicked()
{
    auto w = 0;
    auto h = 0;
    MString err;
    if (!activeViewportSize(&w, &h, &err)) {
        setStatus(QString::fromUtf8(err.asUTF8()));
        return;
    }
    m_width->setValue(std::max(16, w));
    m_height->setValue(std::max(16, h));
    setStatus(QStringLiteral("Resolution set to viewport: %1×%2").arg(w).arg(h));
}

void OvrtxRenderWindow::onChannelChanged(int id)
{
    m_channel = static_cast<Channel>(id);
    refreshDisplay();
}

// Returns the current image, or for a single R/G/B/A channel 
QImage OvrtxRenderWindow::imageForCurrentChannel() const
{
    if (m_finalImage.isNull() || m_channel == Channel::RGB) {
        return m_finalImage;
    }

    const QImage rgba = m_finalImage.convertToFormat(QImage::Format_RGBA8888);
    QImage out(rgba.size(), QImage::Format_RGBA8888);
    const auto channel = static_cast<int>(m_channel) - 1; // R=0, G=1, B=2, A=3

    for (auto y = 0; y < rgba.height(); ++y) {
        const uchar* src = rgba.constScanLine(y);
        uchar* dst = out.scanLine(y);
        for (auto x = 0; x < rgba.width(); ++x) {
            const uchar v = src[x * 4 + channel];
            dst[x * 4 + 0] = v;
            dst[x * 4 + 1] = v;
            dst[x * 4 + 2] = v;
            dst[x * 4 + 3] = 255;
        }
    }
    return out;
}

void OvrtxRenderWindow::refreshDisplay()
{
    const QImage image = imageForCurrentChannel();
    if (image.isNull()) {
        m_imageLabel->setPixmap(QPixmap());
        m_imageLabel->setText(QStringLiteral("No image."));
        return;
    }
    m_imageLabel->setText(QString());
    m_imageLabel->setPixmap(QPixmap::fromImage(image));
    m_imageLabel->resize(image.size());
}

void OvrtxRenderWindow::setRenderingUi(bool rendering)
{
    m_rendering = rendering;
    m_renderBtn->setEnabled(!rendering);
    m_stopBtn->setEnabled(rendering);
}

void OvrtxRenderWindow::setStatus(const QString& text)
{
    if (m_status) {
        m_status->setText(text);
        m_status->repaint();
    }
}

} // namespace OVRTXMAYA_NS