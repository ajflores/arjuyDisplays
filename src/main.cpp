// arjuy-displays — arrange, identify and configure monitors on Omarchy (phase 1: read-only).

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "cli.h"
#include "identifier.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // Commands without a window must not need a display.
    const QByteArray command = argc > 1 ? QByteArray(argv[1]) : QByteArray("open");
    const bool windowed = command == "open" || command == "identify";
    if (!windowed)
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    app.setApplicationName("arjuy-displays");
    app.setApplicationVersion("0.1.0");

    Translator translator;
    app.installTranslator(&translator);

    if (command == "identify") {
        Theme theme;
        Backend backend({}, {}, false);
        if (!backend.error().isEmpty()) {
            fprintf(stderr, "%s\n", qPrintable(backend.error()));
            return 1;
        }
        Identifier identifier(&backend.monitorList());
        identifier.setAccent(theme.accent());
        QObject::connect(&identifier, &Identifier::activeChanged, &app, [&] {
            if (!identifier.active())
                app.quit();
        });
        QTimer::singleShot(0, &identifier, [&] { identifier.show(); });
        return app.exec();
    }

    if (const int code = runCli(argc, argv); code >= 0)
        return code;

    // Matches the window to arjuy-displays.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("arjuy-displays");
    app.setWindowIcon(QIcon::fromTheme("arjuy-displays"));

    QQuickStyle::setStyle("Material");

    Theme theme;
    Backend backend;
    Identifier identifier(&backend.monitorList());
    identifier.setAccent(theme.accent());
    QObject::connect(&theme, &Theme::changed, &identifier, [&] { identifier.setAccent(theme.accent()); });

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("identifier", &identifier);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
