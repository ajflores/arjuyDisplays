#include "identifier.h"

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QTextStream>
#include <LayerShellQt/Window>

static bool sameText(const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) == 0; }

ScreenDesc Identifier::describe(const QScreen *screen) {
    return {screen->name(), screen->manufacturer(), screen->model(), screen->serialNumber()};
}

QList<ScreenMatch> Identifier::matchScreens(const QList<ScreenDesc> &screens, const QList<MonitorInfo> &monitors) {
    QList<ScreenMatch> result(screens.size());
    QList<bool> used(monitors.size(), false);
    for (int s = 0; s < screens.size(); ++s)
        for (int m = 0; m < monitors.size(); ++m)
            if (!used[m] && monitors[m].name == screens[s].name) {
                result[s] = {m, true};
                used[m] = true;
                break;
            }
    for (int s = 0; s < screens.size(); ++s) {
        const auto &d = screens[s];
        if (result[s].monitorIndex >= 0 || (d.manufacturer.isEmpty() && d.model.isEmpty()))
            continue;
        auto same = [&](const MonitorInfo &mon) {
            return sameText(mon.make, d.manufacturer) && sameText(mon.model, d.model) && sameText(mon.serial, d.serial);
        };
        // Ambiguous when several unmatched monitors or screens share the same identity.
        int candidate = -1, candidates = 0, rivals = 0;
        for (int m = 0; m < monitors.size(); ++m)
            if (!used[m] && same(monitors[m])) {
                candidate = m;
                ++candidates;
            }
        for (int o = 0; o < screens.size(); ++o)
            if (result[o].monitorIndex < 0 && sameText(screens[o].manufacturer, d.manufacturer)
                && sameText(screens[o].model, d.model) && sameText(screens[o].serial, d.serial))
                ++rivals;
        if (candidates == 1 && rivals == 1) {
            result[s] = {candidate, false};
            used[candidate] = true;
        }
    }
    return result;
}

Identifier::Identifier(const QList<MonitorInfo> *monitors, QObject *parent) : QObject(parent), m_monitors(monitors) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &Identifier::hide);
    auto refresh = [this] {
        if (m_active)
            rebuild();
    };
    connect(qGuiApp, &QGuiApplication::screenAdded, this, refresh);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, refresh);
}

Identifier::~Identifier() { clearWindows(); }

QString Identifier::warningText() const {
    return m_unmatched.isEmpty()
               ? QString()
               : tr("Could not match %1 with a Hyprland monitor; its banner shows the name Qt reports.")
                     .arg(m_unmatched.join(", "));
}

void Identifier::retranslate() { setWarning(warningText()); }

void Identifier::setWarning(const QString &warning) {
    if (warning == m_warning)
        return;
    m_warning = warning;
    emit warningChanged();
}

void Identifier::clearWindows() {
    for (auto &w : m_windows)
        if (w) {
            w->hide();
            w->deleteLater();
        }
    m_windows.clear();
}

void Identifier::show(const QString &highlight, int durationMs) {
    m_highlight = highlight;
    m_timer.start(durationMs);
    rebuild();
    if (!m_active) {
        m_active = true;
        emit activeChanged();
    }
}

void Identifier::hide() {
    m_timer.stop();
    clearWindows();
    if (m_active) {
        m_active = false;
        emit activeChanged();
    }
}

void Identifier::setHighlight(const QString &highlight) {
    if (highlight == m_highlight)
        return;
    m_highlight = highlight;
    if (m_active)
        rebuild();
}

void Identifier::rebuild() {
    clearWindows();
    const auto screens = QGuiApplication::screens();
    QList<ScreenDesc> descs;
    for (const auto *s : screens)
        descs << describe(s);
    const auto matches = matchScreens(descs, *m_monitors);
    const bool layerShell = QGuiApplication::platformName().startsWith("wayland");

    QStringList unmatched;
    for (int i = 0; i < screens.size(); ++i) {
        QScreen *screen = screens[i];
        const int mi = matches[i].monitorIndex;
        QString label = screen->name(), detail;
        if (mi >= 0) {
            const auto &m = (*m_monitors)[mi];
            label = m.name;
            detail = QString("%1×%2 · %3×").arg(m.width).arg(m.height).arg(QString::number(m.scale, 'g', 4));
        } else {
            unmatched << screen->name();
        }

        auto *view = new QQuickView;
        view->setScreen(screen);
        view->setColor(Qt::transparent);
        view->setFlag(Qt::FramelessWindowHint);
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->setTitle("arjuy-displays-identify");
        view->setInitialProperties({{"label", label},
                                    {"detail", detail},
                                    {"accent", m_accent},
                                    {"highlighted", !m_highlight.isEmpty() && label == m_highlight},
                                    {"matched", mi >= 0}});
        view->setSource(QUrl("qrc:/Identify.qml"));
        if (view->status() != QQuickView::Ready) {
            delete view;
            continue;
        }
        view->resize(560, 300);
        if (layerShell) {
            if (auto *lw = LayerShellQt::Window::get(view)) {
                lw->setScope("arjuy-displays-identify");
                lw->setLayer(LayerShellQt::Window::LayerOverlay);
                lw->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
                lw->setAnchors({});  // centered on the screen
                lw->setExclusiveZone(-1);
                lw->setDesiredSize(QSize(560, 300));
                lw->setScreen(screen);
            }
        }
        QObject::connect(view->rootObject(), SIGNAL(dismissed()), this, SLOT(hide()));
        view->show();
        QTextStream(stderr) << "identify: " << label << " on screen " << screen->name()
                            << (mi < 0 ? " (no match in hyprctl)" : matches[i].byName ? "" : " (matched by make/model)")
                            << "\n";
        m_windows << view;
    }
    m_unmatched = unmatched;
    setWarning(warningText());
}
