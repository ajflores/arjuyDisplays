#pragma once

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// One monitor as reported by `hyprctl -j monitors all`.
struct MonitorInfo {
    QString name, description, make, model, serial, mirrorOf;
    int x = 0, y = 0, width = 0, height = 0, transform = 0;
    double refreshRate = 0, scale = 1;
    bool disabled = false, vrr = false, focused = false;
    QStringList availableModes;

    // Size in layout coordinates: pixels divided by scale, with width and
    // height swapped when the monitor is rotated a quarter turn.
    double logicalWidth() const;
    double logicalHeight() const;
    QVariantMap toVariant() const;
};

// Parses and validates hyprctl's output. Anything unexpected is rejected as a
// whole rather than guessed at, because later phases write config from it.
class MonitorParser {
    Q_DECLARE_TR_FUNCTIONS(MonitorParser)

public:
    static QList<MonitorInfo> parse(const QByteArray &json, QString *error);
    static double logicalSize(int pixels, double scale);
    static bool swapsAxes(int transform);
    // "1920x1080@60Hz" becomes "1920x1080@60.00Hz"; an empty string means invalid.
    static QString normalizeMode(const QString &mode);
    static QStringList normalizeModes(const QStringList &modes);
};
