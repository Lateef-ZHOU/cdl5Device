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
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    using DeviceT = Cdl5Device;
    static_assert(std::derived_from<DeviceT, DAQDeviceBase> && !std::is_abstract_v<DeviceT>);
    QQmlEngine engine;
    bool warned = false;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const auto &) { warned = true; });
    {
        QQmlComponent component(&engine);
        component.setData("import Lidar.Cdl5\nCdl5Module {}", QUrl());
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
        // 设备与采集卡族是同一个对象：family 即后端本身，可交叉转换到纯 C++ 族基。
        auto *card = dynamic_cast<DAQDeviceBase *>(backend);
        if (!card || page->property("family").value<QObject *>() != backend) return 7;
        auto *cdl5 = qobject_cast<DeviceT *>(backend);
        for (int channel = 0; channel < 3; ++channel) {
            {
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
        if (spec["rows"].toInt() != Cdl5::Rows || spec["columns"].toInt() != Cdl5::Columns
            || cdl5->acquisitionParameters()["accumMax"].toInt() != Cdl5::MaxAccumSeconds) return 11;
        // 型号范围由 paramsFor 判定：越界返回空，begin 复核同一判据。
        if (card->paramsFor(Cdl5::MaxAccumSeconds + 1, {}, {}, {}, false) || !card->paramsFor(1, {}, {}, {}, false)
            || card->begin(DAQParameters{Cdl5::MaxAccumSeconds + 1}, [](auto) {}, {})) return 12;
        if (card->begin(DAQParameters{1}, [](auto) {}, {})) return 9; // Offline admission must reject, without starting hardware.
        card->stop({});
        const QPointer<QObject> lifetime(backend);
        QTimer::singleShot(2500, &app, &QCoreApplication::quit);
        app.exec();
        page.reset();
        if (lifetime) return 6;
    }
    return warned ? 4 : 0;
}
