// Keystone — a fast native 3D viewer for IFC building models.

#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "meshgeometry.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("keystone");
    app.setApplicationVersion("0.1.0");

    // Matches the window to keystone.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("keystone");
    app.setWindowIcon(QIcon::fromTheme("keystone"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;
    QObject::connect(&translator, &Translator::changed, &backend, &Backend::refreshSelection);

    qmlRegisterType<MeshGeometry>("Keystone", 1, 0, "MeshGeometry");
    qmlRegisterUncreatableType<Backend>("Keystone", 1, 0, "Backend", "Backend is provided as `backend`");

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // `keystone model.ifc` opens it straight away, as the launcher's %f does.
    const QStringList arguments = app.arguments().mid(1);
    for (const QString &argument : arguments) {
        if (argument.startsWith('-'))
            continue;
        const QUrl url = QUrl::fromUserInput(argument, QDir::currentPath(), QUrl::AssumeLocalFile);
        backend.openUrl(url);
        break;
    }

    return app.exec();
}
