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

Q_IMPORT_QML_PLUGIN(LidarUIPlugin)
Q_IMPORT_QML_PLUGIN(LidarChartsPlugin)
Q_IMPORT_QML_PLUGIN(LidarTaskUIPlugin)
Q_IMPORT_QML_PLUGIN(LidarCdl5Plugin)
template<class DeviceT> int verify(QGuiApplication &app, const char *module)
{
    static_assert(std::derived_from<DeviceT, DAQDeviceBase> && !std::is_abstract_v<DeviceT>);
    QQmlEngine engine;
    bool warned = false;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const auto &) { warned = true; });
    {
        QQmlComponent component(&engine);
        component.setData(QByteArray("import Lidar.Cdl5\n") + module + " {}", QUrl());
        std::unique_ptr<QObject> page(component.create());
        if (!page) { qCritical() << component.errors(); return 1; }
        const QQmlListReference charts(page.get(), "mainPages");
        const QQmlListReference sections(page.get(), "sections");
        if (charts.count() != 9 || sections.count() < 1) return 2;
        if (page->property("name").toString().isEmpty()) return 3;
        QObject *backend = nullptr;
        int backends = 0;
        for (auto *child : page->children()) {
            if (qobject_cast<DeviceT *>(child) != nullptr) {
                backend = child;
                ++backends;
            }
        }
        if (backends != 1 || backend->parent() != page.get()) return 5;
        auto *card = qobject_cast<DAQDeviceBase *>(backend);
        if (!card || page->property("family").value<DAQDeviceBase *>() != card) return 7;
        auto *cdl5 = qobject_cast<DeviceT *>(backend);
        for (int channel = 0; channel < 3; ++channel) {
            if (!page->findChild<QObject *>(QString("radialADC%1").arg(channel))
                || !page->findChild<QObject *>(QString("barbsADC%1").arg(channel))) return 10;
            Q_EMIT cdl5->radialReady(channel, {0, 15, 150}, {1, 2, 3}, {-9, -7, -4}, true);
            Q_EMIT cdl5->profileReady(channel, std::make_shared<const NonuniformVerticalWindProfileUnit>(
                NonuniformVerticalWindProfileUnit{std::chrono::system_clock::now(), {0, 10, 90}, {{1,2,0},{3,4,0},{5,6,0}}}));
        }
        const auto spec = cdl5->chartParameters();
        using C = typename DeviceT::CollectorType;
        if (spec["rows"].toInt() != C::Rows || spec["columns"].toInt() != C::Columns
            || cdl5->acquisitionParameters()["accumMax"].toInt() != C::Specification.maxAccumSeconds()) return 11;
        const auto prototype = card->collectorFor(1, {});
        if (!std::dynamic_pointer_cast<const typename DeviceT::CollectorType>(prototype) || prototype->settled()) return 8;
        if (card->begin(prototype, [](auto) {}, {})) return 9; // Offline admission must reject, without starting hardware.
        card->stop({});
        const QPointer<QObject> lifetime(backend);
        QTimer::singleShot(2500, &app, &QCoreApplication::quit);
        app.exec();
        page.reset();
        if (lifetime) return 6;
    }
    return warned ? 4 : 0;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (auto result = verify<Cdl5FourKDevice>(app, "Cdl5FourKModule")) return result;
    if (auto result = verify<Cdl5FiveKDevice>(app, "Cdl5FiveKModule")) return result;
    return verify<Cdl5TenKDevice>(app, "Cdl5TenKModule");
}
