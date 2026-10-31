#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlListReference>
#include <QtQml/QQmlExtensionPlugin>
#include <QDebug>
#include <memory>
#include <QPointer>
#include <QTimer>
#include <Cdl5/Device.h>
static_assert(std::derived_from<Cdl5Device, DAQDeviceBase> && !std::is_abstract_v<Cdl5Device>);
Q_IMPORT_QML_PLUGIN(LidarUIPlugin)
Q_IMPORT_QML_PLUGIN(LidarChartsPlugin)
Q_IMPORT_QML_PLUGIN(LidarTaskUIPlugin)
Q_IMPORT_QML_PLUGIN(LidarCdl5Plugin)
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
    bool warned = false;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const auto &) { warned = true; });
    {
        QQmlComponent component(&engine);
        component.setData("import Lidar.Cdl5\nCdl5Module {  }", QUrl());
        std::unique_ptr<QObject> page(component.create());
        if (!page) { qCritical() << component.errors(); return 1; }
        const QQmlListReference charts(page.get(), "mainPages");
        const QQmlListReference sections(page.get(), "sections");
        if (charts.count() != 3 || sections.count() < 1) return 2;
        if (page->property("name").toString().isEmpty()) return 3;
        QObject *backend = nullptr;
        int backends = 0;
        for (auto *child : page->children()) {
            if (QByteArray(child->metaObject()->className()) == "Cdl5Device") {
                backend = child;
                ++backends;
            }
        }
        if (backends != 1 || backend->parent() != page.get()) return 5;
        auto *card = qobject_cast<DAQDeviceBase *>(backend);
        if (!card || page->property("family").value<DAQDeviceBase *>() != card) return 7;
        const auto prototype = card->collectorFor(1, {});
        if (!std::dynamic_pointer_cast<const Cdl5ActiveCollector>(prototype) || prototype->settled()) return 8;
        if (card->begin(prototype, [](auto) {}, {})) return 9; // Offline admission must reject, without starting hardware.
        card->stop({});
        const QPointer<QObject> lifetime(backend);
        QTimer::singleShot(8000, &app, &QCoreApplication::quit);
        app.exec();
        page.reset();
        if (lifetime) return 6;
    }
    return warned ? 4 : 0;
}
