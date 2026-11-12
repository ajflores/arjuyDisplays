#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonValue>
#include <QLocale>
#include <QTimer>
#include <QTranslator>

// Follows the language picked under Omarchy's Setup > Language. Catalogs are
// flat JSON objects from English source text to translation, one per Omarchy
// language code (i18n/da.json, i18n/pt-BR.json), so QML keeps plain
// qsTr("Save") and C++ keeps tr("Save"). A counted string,
// qsTr("%n clips", "", count), maps to an object of plural forms keyed by
// CLDR category ({"one": "%n klip", "other": "%n klip"}).
class Translator : public QTranslator {
    Q_OBJECT
    Q_PROPERTY(QString language READ language NOTIFY changed)
    // The user's choice: "auto" (follow Omarchy), "en" or "es".
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)

public:
    explicit Translator(const QString &languageFile = {}, const QString &catalogDirectory = ":/i18n",
                        QObject *parent = nullptr, const QString &settingsFile = {});

    QString language() const { return m_language; }
    bool isEmpty() const override { return m_strings.isEmpty(); }
    QString translate(const char *context, const char *sourceText, const char *disambiguation = nullptr,
                      int n = -1) const override;

    QString mode() const { return m_mode; }
    // Changes the language live and, unless persistence is off, saves the
    // choice. Unknown values are ignored.
    void setMode(const QString &mode);
    Q_INVOKABLE void cycleMode();
    // Forces a mode for this run only: nothing is read from or written to the
    // settings file afterwards (used by `--lang`).
    void setTransientMode(const QString &mode);

    // The choices the selector offers, in cycling order.
    static QStringList modes();
    // ARJUY_SETTINGS, or ~/.local/state/arjuy-displays/settings.json.
    static QString defaultSettingsPath();
    // ARJUY_OMARCHY_LANGUAGE, or ~/.config/omarchy/language.
    static QString defaultLanguageFile();
    // The saved choice; a missing, unreadable or invalid file means "auto".
    static QString readMode(const QString &path);
    // Atomic write (temporary file, then rename). Returns false on failure.
    static bool writeMode(const QString &path, const QString &mode);

    static QString readLanguage(const QString &path);
    static QString pluralCategory(const QString &language, long long count);
    static QLocale formatLocale(const QString &language);

signals:
    void changed();
    void modeChanged();

private:
    void reload();

    QString m_languageFile;
    QString m_catalogDirectory;
    QString m_language;
    QString m_settingsFile;
    QString m_mode = "auto";
    bool m_persist = true;
    QHash<QString, QJsonValue> m_strings;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};
