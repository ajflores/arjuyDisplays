#include "cli.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QStringList>
#include <cstdio>

#include "backend.h"

static void out(const QString &text, FILE *stream = stdout) {
    fputs(text.toUtf8().constData(), stream);
}

int runCli(int argc, char *argv[]) {
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    if (args.isEmpty() || args.first() == "open")
        return -1;

    const QString usage = QCoreApplication::translate("Cli", "Usage: arjuy-displays [open | identify | check [--json] | --version | --help]\n");
    if (args.first() == "--help" || args.first() == "-h") {
        out(usage);
        return 0;
    }
    if (args.first() == "--version") {
        out("arjuy-displays 0.1.0\n");
        return 0;
    }
    if (args.first() != "check") {
        out(QCoreApplication::translate("Cli", "Unknown command: %1\n").arg(args.first()), stderr);
        out(usage, stderr);
        return 1;
    }

    const bool json = args.contains("--json");
    Backend backend({}, {}, false);
    if (json) {
        out(QString::fromUtf8(QJsonDocument(backend.stateJson()).toJson(QJsonDocument::Indented)));
    } else {
        for (const auto &m : backend.monitorList())
            out(QString("%1  %2x%3@%4Hz  scale %5  at %6,%7%8\n")
                    .arg(m.name, QString::number(m.width), QString::number(m.height),
                         QString::number(m.refreshRate, 'f', 2), QString::number(m.scale),
                         QString::number(m.x), QString::number(m.y), m.disabled ? "  (disabled)" : ""));
        for (const auto &w : backend.warnings())
            out(w + "\n");
        for (const auto &v : backend.issues()) {
            const auto issue = v.toMap();
            out(QString("[%1] %2 %3\n").arg(issue["severity"].toString(), issue["message"].toString(),
                                            issue["hint"].toString()));
        }
    }
    if (!backend.error().isEmpty()) {
        out(backend.error() + "\n", stderr);
        return 1;
    }
    return 0;
}
