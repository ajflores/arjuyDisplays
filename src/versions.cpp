#include "versions.h"

#include <QRegularExpression>

static bool matches(const QString &version, const QString &prefix) {
    return version == prefix || version.startsWith(prefix + ".");
}

bool Versions::hyprlandTested(const QString &version) { return matches(version, "0.56"); }
bool Versions::omarchyTested(const QString &version) { return matches(version, "4.5"); }

QString Versions::parsePacmanVersion(const QString &text) {
    static const QRegularExpression pattern(R"(^\s*\S+\s+(\d[\w.+~:-]*)\s*$)");
    const auto match = pattern.match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}
