// PocketDrop, standalone: the engine in src/, the screen in qml/PocketDropView.qml, and this thin Qt Quick host between them.
#include "backend.h"
#include <QColor>
#include <QCommandLineParser>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QQuickWindow>
#include <QTimer>
#include <QPalette>
#include <QStyleHints>
#include <QVersionNumber>

static QQmlPropertyMap* makeTheme(QObject* parent, bool dark) {
    QFile file(":/pd/theme.json");
    auto* theme = new QQmlPropertyMap(parent);
    if (!file.open(QIODevice::ReadOnly)) return theme;
    QJsonObject palette = QJsonDocument::fromJson(file.readAll()).object().value(dark ? "dark" : "light").toObject();
    for (auto it = palette.begin(); it != palette.end(); ++it) theme->insert(it.key(), QColor(it.value().toString()));
    return theme;
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("PocketDrop");
    app.setOrganizationName("PocketDrop");
    QCommandLineParser parser;
    parser.addPositionalArgument("files", "Files and folders to share");
    parser.process(app);

    Backend backend;
    QQmlApplicationEngine engine;
    // Light or dark follows the system. Qt 6.5 reports the scheme; older Qt is read from the window colour.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    auto isDark = [&] { return app.styleHints()->colorScheme() != Qt::ColorScheme::Light; };
#else
    auto isDark = [&] { return QGuiApplication::palette().color(QPalette::Window).lightness() < 128; };
#endif
    QQmlPropertyMap* theme = makeTheme(&app, isDark());
    auto restyle = [&] {
        QQmlPropertyMap* fresh = makeTheme(nullptr, isDark());
        for (const QString& key : fresh->keys()) theme->insert(key, fresh->value(key));
        delete fresh;
    };
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app, restyle);
#endif
    engine.rootContext()->setContextProperty("theme", theme);
    engine.rootContext()->setContextProperty("pocketDrop", &backend);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &backend, &Backend::shutdown);
    engine.load(QUrl("qrc:/pd/main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    backend.start();
    backend.addPaths(parser.positionalArguments());
    // Smoke test and screenshots for CI: POCKETDROP_SNAPSHOT=file.png saves the window after a moment and exits.
    if (const QByteArray snapshot = qgetenv("POCKETDROP_SNAPSHOT"); !snapshot.isEmpty()) {
        QTimer::singleShot(2500, &app, [&, snapshot] {
            if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) window->grabWindow().save(QString::fromLocal8Bit(snapshot));
            app.quit();
        });
    }
    return app.exec();
}
