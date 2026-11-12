#include "hyprctl.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

QString Hyprctl::executable() {
    const QString override = qEnvironmentVariable("ARJUY_HYPRCTL");
    return override.isEmpty() ? QStandardPaths::findExecutable("hyprctl") : override;
}

Hyprctl::Result Hyprctl::run(const QString &program, const QStringList &args, int timeoutMs) {
    auto tr = [](const char *s) { return QCoreApplication::translate("Hyprctl", s); };
    Result result;
    if (program.isEmpty() || !QFileInfo(program).isExecutable()) {
        result.error = tr("The command was not found.");
        return result;
    }
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(timeoutMs)) {
        result.error = tr("The command could not be started.");
        return result;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        result.error = tr("The command did not answer in time and was stopped.");
        return result;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.error = tr("The command failed with exit code %1.").arg(process.exitCode());
        return result;
    }
    result.output = process.readAllStandardOutput();
    result.ok = true;
    return result;
}
