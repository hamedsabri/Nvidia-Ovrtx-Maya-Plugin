#include "ovrtxRenderCommand.h"
#include "ovrtxRenderWindow.h"

#include <maya/MArgDatabase.h>
#include <maya/MGlobal.h>
#include <maya/MQtUtil.h>

#include <QApplication>
#include <QCoreApplication>
#include <QPointer>
#include <QWidget>

namespace
{

const char* const kNodeFlagShort = "-n";
const char* const kNodeFlagLong = "-node";
const char* const kReloadFlagShort = "-r";
const char* const kReloadFlagLong = "-reload";

const char* const kWorkspaceControlName = "ovrtxRenderViewControl";
const char* const kWorkspaceControlLabel = "ovrtx Render View";
const char* const kPluginModuleName = "ovrtxMayaPlugin";

QPointer<OVRTXMAYA_NS::OvrtxRenderWindow> g_window;

bool workspaceControlExists()
{
    int32_t exists {0};
    MGlobal::executeCommand(
        MString("workspaceControl -q -exists \"")
            + kWorkspaceControlName + "\"",
        exists);
    return exists != 0;
}

// Lazily create the retained render widget.
OVRTXMAYA_NS::OvrtxRenderWindow* ensureWidget()
{
    if (!g_window) {
        g_window = new OVRTXMAYA_NS::OvrtxRenderWindow(nullptr);
        g_window->setObjectName(QStringLiteral("ovrtxRenderViewWidget"));
        g_window->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    return g_window.data();
}
} // namespace

namespace OVRTXMAYA_NS
{

const MString OvrtxRenderCommand::commandName("ovrtxRender");

void* OvrtxRenderCommand::creator()
{
    return new OvrtxRenderCommand();
}

MSyntax OvrtxRenderCommand::createSyntax()
{
    MSyntax syntax;
    syntax.addFlag(kNodeFlagShort, kNodeFlagLong, MSyntax::kString);
    syntax.addFlag(kReloadFlagShort, kReloadFlagLong);
    return syntax;
}

MStatus OvrtxRenderCommand::doIt(const MArgList& args)
{
    MStatus status;
    MArgDatabase  argData(syntax(), args, &status);
    if (!status) {
        return status;
    }

    const bool isReload = argData.isFlagSet(kReloadFlagLong);

    OvrtxRenderWindow* w = ensureWidget();

    if (argData.isFlagSet(kNodeFlagLong)) {
        MString proxyName;
        argData.getFlagArgument(kNodeFlagLong, 0, proxyName);
        w->setProxyShapeName(QString::fromUtf8(proxyName.asUTF8()));
    }

    if (isReload) {
        QWidget* wsParent = MQtUtil::getCurrentParent();
        if (!wsParent) {
            MGlobal::displayError(
                "ovrtxRender -reload: no current parent set.");
            return MStatus::kFailure;
        }
        if (w->parentWidget() != wsParent) {
            MQtUtil::addWidgetToMayaLayout(w, wsParent);
        }
        w->show();
        return MStatus::kSuccess;
    }

    if (workspaceControlExists()) {
        QWidget* existing = MQtUtil::findControl(kWorkspaceControlName);
        if (existing && w->parentWidget() != existing) {
            MQtUtil::addWidgetToMayaLayout(w, existing);
        }
        w->show();
        MGlobal::executeCommand(
            MString("workspaceControl -e -restore \"")
                + kWorkspaceControlName + "\"");
        return MStatus::kSuccess;
    }

    MString cmd;
    cmd.format(
        "workspaceControl"
        " -label \"^1s\""
        " -retain true"
        " -loadImmediately true"
        " -floating true"
        " -initialWidth 1024"
        " -initialHeight 800"
        " -requiredPlugin \"^2s\""
        " \"^3s\"",
        MString(kWorkspaceControlLabel),
        MString(kPluginModuleName),
        MString(kWorkspaceControlName));
    MGlobal::executeCommand(cmd);

    QWidget* wsParent = MQtUtil::findControl(kWorkspaceControlName);
    if (!wsParent) {
        MGlobal::displayError(
            "ovrtxRender: workspaceControl widget not found.");
        return MStatus::kFailure;
    }

    MQtUtil::addWidgetToMayaLayout(w, wsParent);
    w->show();

    MString uiScriptCmd;
    uiScriptCmd.format(
        R"(workspaceControl -e -uiScript "^1s -reload" "^2s")",
        commandName,
        MString(kWorkspaceControlName));
    MGlobal::executeCommand(uiScriptCmd);

    MGlobal::executeCommand(
        MString("workspaceControl -e -restore \"")
            + kWorkspaceControlName + "\"");
    return MStatus::kSuccess;
}

void OvrtxRenderCommand::cleanup()
{
    if (g_window) {
        g_window->setParent(nullptr);
        g_window->close();
        delete g_window.data();
        g_window.clear();
    }

    if (workspaceControlExists()) {
        MGlobal::executeCommand(
            MString("workspaceControl -e -close \"")
                + kWorkspaceControlName + "\"");
        MGlobal::executeCommand(
            MString("deleteUI \"") + kWorkspaceControlName + "\"");
    }
}

} // namespace OVRTXMAYA_NS