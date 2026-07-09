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

// Qt Render View widget: render/output/post-processing controls

#include "ovrtxRendererSession.h"

#include <maya/MCallbackIdArray.h>

#include <QImage>
#include <QString>
#include <QWidget>

#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;

namespace OVRTXMAYA_NS
{

class OvrtxRenderWindow : public QWidget
{
    Q_OBJECT
public:
    explicit OvrtxRenderWindow(QWidget* parent = nullptr);
    ~OvrtxRenderWindow() override;

    void setProxyShapeName(const QString& name) { m_proxyName = name; }

protected:
    void closeEvent(QCloseEvent* event) override;

private Q_SLOTS:
    void onRenderClicked();
    void onStopClicked();
    void onSaveClicked();
    void onFromViewportClicked();
    void onChannelChanged(int id);
    void onTick();

private:
    // Display channel. Values 1..4 index into RGBA byte offsets in
    // imageForCurrentChannel().
    enum class Channel { RGB = 0, R = 1, G = 2, B = 3, A = 4 };

    bool ensureOvrtxReady();
    OvrtxRendererSession::Settings collectSettings() const;
    void setStatus(const QString& text);
    void stopRender();
    void setRenderingUi(bool rendering);
    void refreshDisplay();
    QImage imageForCurrentChannel() const;

    // Clears the retained render state when the Maya scene is replaced
    // (File > New / Open), so a stale image/proxy can't survive into the new scene.
    void resetForSceneChange();
    static void onSceneChanged(void* clientData);

    std::unique_ptr<OvrtxRendererSession> m_ovrtx;
    QImage  m_finalImage;
    QString m_proxyName;
    bool    m_rendering {false};
    bool    m_unlitScene {false};
    Channel m_channel {Channel::RGB};

    // Maya scene-change (New/Open) callbacks, removed in the destructor.
    MCallbackIdArray m_sceneCallbacks;

    // Display
    QLabel* m_imageLabel {nullptr};
    QLabel* m_status {nullptr};

    // Output + core controls
    QSpinBox*  m_width {nullptr};
    QSpinBox*  m_height {nullptr};
    QComboBox* m_aov {nullptr};
    QSpinBox*  m_samples {nullptr};

    // Lighting / exposure
    QCheckBox*      m_autoExposure {nullptr};
    QCheckBox*      m_transparent {nullptr};

    // Depth of field
    QCheckBox*      m_dof {nullptr};
    QDoubleSpinBox* m_fstop {nullptr};
    QDoubleSpinBox* m_focusDist {nullptr};

    // Post passes
    QCheckBox* m_bloom {nullptr};
    QCheckBox* m_grade {nullptr};
    QCheckBox* m_chromab {nullptr};
    QCheckBox* m_motionBlur {nullptr};
    QComboBox* m_tonemap {nullptr};

    // Actions
    QPushButton* m_renderBtn {nullptr};
    QPushButton* m_stopBtn {nullptr};
    QPushButton* m_saveBtn {nullptr};

    QTimer* m_timer {nullptr};
};

} // namespace OVRTXMAYA_NS