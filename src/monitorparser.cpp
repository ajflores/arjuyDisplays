#include "monitorparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

bool MonitorParser::swapsAxes(int transform) {
    // Transforms 1, 3, 5, 7 are the quarter-turn rotations (flipped or not).
    return transform >= 0 && transform <= 7 && (transform % 2) == 1;
}

double MonitorParser::logicalSize(int pixels, double scale) {
    return scale > 0 ? pixels / scale : pixels;
}

double MonitorInfo::logicalWidth() const {
    return MonitorParser::logicalSize(MonitorParser::swapsAxes(transform) ? height : width, scale);
}

double MonitorInfo::logicalHeight() const {
    return MonitorParser::logicalSize(MonitorParser::swapsAxes(transform) ? width : height, scale);
}

QVariantMap MonitorInfo::toVariant() const {
    return {
        {"name", name}, {"description", description}, {"make", make}, {"model", model},
        {"serial", serial}, {"x", x}, {"y", y}, {"width", width}, {"height", height},
        {"refreshRate", refreshRate}, {"scale", scale}, {"transform", transform},
        {"disabled", disabled}, {"mirrorOf", mirrorOf}, {"vrr", vrr}, {"focused", focused},
        {"availableModes", availableModes},
        {"logicalWidth", logicalWidth()}, {"logicalHeight", logicalHeight()},
    };
}

QString MonitorParser::normalizeMode(const QString &mode) {
    static const QRegularExpression pattern(R"(^\s*(\d+)x(\d+)@(\d+(?:\.\d+)?)\s*(?:Hz)?\s*$)");
    const auto match = pattern.match(mode);
    if (!match.hasMatch())
        return {};
    return QString("%1x%2@%3Hz").arg(match.captured(1), match.captured(2),
                                     QString::number(match.captured(3).toDouble(), 'f', 2));
}

QStringList MonitorParser::normalizeModes(const QStringList &modes) {
    QStringList out;
    for (const auto &mode : modes) {
        const QString normalized = normalizeMode(mode);
        if (!normalized.isEmpty() && !out.contains(normalized))
            out << normalized;
    }
    return out;
}

static bool isNumber(const QJsonValue &v) { return v.isDouble() && std::isfinite(v.toDouble()); }

QList<MonitorInfo> MonitorParser::parse(const QByteArray &json, QString *error) {
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return QList<MonitorInfo>{};
    };

    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(tr("hyprctl returned something that is not valid JSON (%1).").arg(parseError.errorString()));
    if (!doc.isArray())
        return fail(tr("hyprctl returned an unexpected shape: a list of monitors was expected."));

    QList<MonitorInfo> monitors;
    QStringList seen;
    for (const auto &item : doc.array()) {
        if (!item.isObject())
            return fail(tr("hyprctl returned an unexpected shape: a monitor entry is not an object."));
        const auto o = item.toObject();
        const QString name = o.value("name").toString();
        if (name.isEmpty() || seen.contains(name))
            return fail(tr("hyprctl returned a monitor without a usable name."));
        seen << name;

        for (const char *key : {"x", "y", "width", "height", "scale", "refreshRate", "transform"})
            if (!isNumber(o.value(key)))
                return fail(tr("Monitor %1 has a missing or invalid field: %2.").arg(name, key));

        MonitorInfo m;
        m.name = name;
        m.x = o.value("x").toInt();
        m.y = o.value("y").toInt();
        m.width = o.value("width").toInt();
        m.height = o.value("height").toInt();
        m.scale = o.value("scale").toDouble();
        m.refreshRate = o.value("refreshRate").toDouble();
        m.transform = o.value("transform").toInt(-1);
        m.disabled = o.value("disabled").toBool(false);
        if (m.width < 0 || m.height < 0 || (!m.disabled && (m.width == 0 || m.height == 0)))
            return fail(tr("Monitor %1 reports an invalid size.").arg(name));
        if (!(m.scale > 0 && m.scale <= 16))
            return fail(tr("Monitor %1 reports an invalid scale.").arg(name));
        if (m.transform < 0 || m.transform > 7)
            return fail(tr("Monitor %1 reports an invalid transform.").arg(name));

        m.description = o.value("description").toString();
        m.make = o.value("make").toString();
        m.model = o.value("model").toString();
        m.serial = o.value("serial").toString();
        m.vrr = o.value("vrr").toBool(false);
        m.focused = o.value("focused").toBool(false);
        const QString mirror = o.value("mirrorOf").toString();
        m.mirrorOf = (mirror.isEmpty() || mirror == "none") ? QString() : mirror;
        const auto modes = o.value("availableModes");
        if (!modes.isUndefined() && !modes.isArray())
            return fail(tr("Monitor %1 has an invalid list of modes.").arg(name));
        QStringList raw;
        for (const auto &mode : modes.toArray())
            raw << mode.toString();
        m.availableModes = normalizeModes(raw);
        monitors << m;
    }
    if (monitors.isEmpty())
        return fail(tr("hyprctl reported no monitors."));
    return monitors;
}
