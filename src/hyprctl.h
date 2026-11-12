#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Runs read-only commands. Never pass anything here that changes state: this
// phase of the app does not write configuration or reload Hyprland.
namespace Hyprctl {
struct Result {
    bool ok = false;
    QByteArray output;
    QString error;
};

// ARJUY_HYPRCTL overrides the path (tests use a fake script).
QString executable();
// Program and arguments stay separate; the process is killed after timeoutMs.
Result run(const QString &program, const QStringList &args, int timeoutMs = 2000);
}
