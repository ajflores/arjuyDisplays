#pragma once

#include <QString>

// Versions this app was tested against. Anything else is shown as untested.
namespace Versions {
bool hyprlandTested(const QString &version);  // 0.56.x
bool omarchyTested(const QString &version);   // 4.5.x
// "omarchy 4.5.0-3" -> "4.5.0-3"; empty when the text is not recognised.
QString parsePacmanVersion(const QString &text);
}
