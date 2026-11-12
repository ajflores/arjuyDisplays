#include "cli.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QStringList>
#include <cstdio>

#include "backend.h"

static QString tr(const char *text) { return QCoreApplication::translate("Cli", text); }

static void out(const QString &text, FILE *stream = stdout) {
    fputs(text.toUtf8().constData(), stream);
}

int runCli(int argc, char *argv[]) {
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    if (args.isEmpty() || args.first() == "open")
        return -1;

    const QString usage = tr("Usage: arjuy-displays [open | identify | check [--json] | --version | --help] [--lang auto|en|es]\n");
    if (args.first() == "--help" || args.first() == "-h") {
        out(usage);
        return 0;
    }
    if (args.first() == "--version") {
        out("arjuy-displays 0.1.0\n");
        return 0;
    }
    if (args.first() != "check") {
        out(tr("Unknown command: %1\n").arg(args.first()), stderr);
        out(usage, stderr);
        return 1;
    }

    const bool json = args.contains("--json");
    Backend backend({}, {}, false);
    if (json) {
        out(QString::fromUtf8(QJsonDocument(backend.stateJson()).toJson(QJsonDocument::Indented)));
    } else {
        for (const auto &m : backend.monitorList())
            out(tr("%1  %2x%3@%4Hz  scale %5  at %6,%7%8\n")
                    .arg(m.name, QString::number(m.width), QString::number(m.height),
                         QString::number(m.refreshRate, 'f', 2), QString::number(m.scale),
                         QString::number(m.x), QString::number(m.y), m.disabled ? tr("  (disabled)") : QString()));
        for (const auto &mw : backend.workspaceModel().monitors) {
            QStringList ids;
            for (const auto &w : mw.workspaces)
                ids << QString("%1%2%3").arg(w.label(), w.active ? "*" : "", w.guest ? "!" : "");
            out(tr("  %1 workspaces [%2]  block %3\n").arg(mw.monitor, ids.join(' '),
                    mw.block.known ? QString("%1-%2").arg(mw.block.first).arg(mw.block.last) : tr("unknown")));
        }
        for (const auto &c : backend.workspaceModel().conflicts)
            out("[" + tr("info") + "] " + c.message + "\n");
        if (!backend.workspaceError().isEmpty())
            out(backend.workspaceError() + "\n");
        for (const auto &w : backend.warnings())
            out(w + "\n");
        for (const auto &v : backend.issues()) {
            const auto issue = v.toMap();
            out(QString("[%1] %2 %3\n").arg(issue["severity"].toString() == "warning" ? tr("warning") : tr("info"), issue["message"].toString(),
                                            issue["hint"].toString()));
        }
    }
    if (!backend.error().isEmpty()) {
        out(backend.error() + "\n", stderr);
        return 1;
    }
    return 0;
}
