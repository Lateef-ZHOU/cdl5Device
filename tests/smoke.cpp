#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlListReference>
#include <QtQml/QQmlExtensionPlugin>
#include <QDebug>
#include <memory>
#include <QPointer>
#include <QTimer>
#include <QStringList>
#include <Device.h>
#include <SurfaceItem.h>

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
            if constexpr (std::is_same_v<DeviceT, Cdl5TenKDevice>) {
                constexpr std::array steps{6, 15, 30};
                const auto name = QStringLiteral("%1m").arg(steps[channel]);
                const auto surface = charts.at(channel * 3)->property("contentItem").value<QObject *>();
                const auto radial = charts.at(channel * 3 + 1)->property("contentItem").value<QObject *>();
                const auto barbs = charts.at(channel * 3 + 2)->property("contentItem").value<QObject *>();
                if (!surface || !radial || !barbs) return 12;
                const auto positions = radial->property("specification").toMap();
                const auto gates = positions["ranges"].value<QVector<double>>();
                if (gates.size() != 240 || gates.back() != 239 * steps[channel]
                    || positions["rangeMaximum"].toInt() != 240 * steps[channel]
                    || barbs->property("configuration").toMap()["maxHeight"].toInt() != 240 * steps[channel])
                    return 13;
                const auto *surfaceItem = surface->findChild<SurfaceItem *>();
                if (!surfaceItem) return 14;
                const auto grid = surfaceItem->configuration();
                const SurfaceGrid surfaceGrid{grid["rows"].toInt(), grid["columns"].toInt()};
                const SurfaceAxes::Projection projection(SurfaceCamera{}, QSizeF(800, 600));
                const auto axes = SurfaceAxes::makeScene(projection, surfaceGrid, 0, 1, app.font());
                if (axes.scales[1].title != QStringLiteral("距离门 (下标)")
                    || axes.scales[1].low != 0 || axes.scales[1].high != 239)
                    return 14;
                const QStringList plotNames{QStringLiteral("功率谱"), QStringLiteral("径向风"), QStringLiteral("风廓线")};
                for (int plot = 0; plot < plotNames.size(); ++plot)
                    if (charts.at(channel * 3 + plot)->property("title").toString() != name + u'·' + plotNames[plot])
                        return 15;
            }
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
