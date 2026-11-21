#include <optional>
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
template<class Channels, std::size_t... I>
Channels sampleChannels(std::index_sequence<I...>)
{
    const auto channel = []<class C>() {
        using S = std::remove_const_t<typename decltype(C::spectrum)::element_type>;
        auto spectrum = std::make_unique<S>();
        std::fill(spectrum->begin(), spectrum->end(), 1.0);
        return C(std::move(spectrum));
    };
    return std::make_shared<typename Channels::element_type>(channel.template operator()<std::tuple_element_t<I, typename Channels::element_type>>()...);
}
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    using DeviceT = Cdl5Device;
    static_assert(std::derived_from<DeviceT, DAQFamily> && !std::is_abstract_v<DeviceT>);
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
        auto *card = dynamic_cast<DAQFamily *>(backend);
        if (!card || page->property("family").value<QObject *>() != backend) return 7;
        auto *cdl5 = qobject_cast<DeviceT *>(backend);
        for (int channel = 0; channel < 3; ++channel) {
            {
                constexpr std::array steps{6, 15, 30};
                const bool mixed = channel == 1;
                constexpr std::array masks{80, 32, 16}; // 镜面 480 m：6/15/30 m 各遮蔽的门数
                const int rows = (mixed ? 420 : 240) - masks[channel];
                const auto name = QStringLiteral("%1m").arg(steps[channel]);
                const auto surface = charts.at(channel * 3)->property("contentItem").value<QObject *>();
                const auto radial = charts.at(channel * 3 + 1)->property("contentItem").value<QObject *>();
                const auto barbs = charts.at(channel * 3 + 2)->property("contentItem").value<QObject *>();
                if (!surface || !radial || !barbs) return 12;
                const auto positions = radial->property("specification").toMap();
                if (positions["rows"].toInt() != rows
                    || barbs->property("configuration").toMap()["maxHeight"].toInt() != (mixed ? 14400 : 240 * steps[channel]))
                    return 13;
                if (mixed) {
                    const auto edges = positions["rangeEdges"].value<StaticRangeEdges>().values;
                    if (edges.data() != Cdl5MixedRange::edges.data() + masks[channel] || edges.size() != 421 - masks[channel]) return 13;
                } else if (positions["rangeStart"].toDouble() != 480 || positions["gateSpacing"].toDouble() != steps[channel])
                    return 13;
                const auto *surfaceItem = qobject_cast<SurfaceItem *>(surface->property("plot").value<QObject *>());
                if (!surfaceItem) return 14;
                // 功率谱按物理斜距标注，混合网格的顶点位置按实际门间距分布。
                const auto grid = surfaceItem->configuration();
                const SurfaceGrid axesGrid(grid["rows"].toInt(), grid["columns"].toInt(), grid["rowStart"].toDouble(),
                    grid["rowStep"].toDouble(), grid["columnStart"].toDouble(), grid["columnStep"].toDouble(),
                    grid["rowEdges"].value<StaticRangeEdges>().values);
                const SurfaceAxes::Projection projection(SurfaceCamera{}, QSizeF(800, 600));
                const auto axes = SurfaceAxes::makeScene(projection, axesGrid, 0, 1, app.font());
                if (axes.scales[1].title != QStringLiteral("斜距 (m)") || axes.scales[1].low != 480
                    || axes.scales[1].high != (mixed ? 14340 : 480 + (rows - 1) * steps[channel])
                    || axes.scales[0].title != QStringLiteral("频率 (MHz)")
                    || axes.scales[0].low != 15 * Cdl5::BinMHz || axes.scales[0].high != 254 * Cdl5::BinMHz)
                    return 14;
                const int shift = masks[1]; // 混合通道遮蔽后，图上第 0 门对应模型第 shift 门
                if (mixed && (axesGrid.rowPosition(239 - shift) != 3585 || axesGrid.rowPosition(240 - shift) != 3600
                    || std::abs(axesGrid.rowFraction(240 - shift) - (3600.0 - 480) / (14340.0 - 480)) > 1e-12
                    || std::abs((axesGrid.rowFraction(241 - shift) - axesGrid.rowFraction(240 - shift))
                              - 4 * (axesGrid.rowFraction(240 - shift) - axesGrid.rowFraction(239 - shift))) > 1e-12))
                    return 14;
                const QStringList plotNames{QStringLiteral("功率谱"), QStringLiteral("径向风"), QStringLiteral("风廓线")};
                for (int plot = 0; plot < plotNames.size(); ++plot)
                    if (charts.at(channel * 3 + plot)->property("title").toString() != name + u'·' + plotNames[plot])
                        return 15;
            }
            if (!page->findChild<QObject *>(QString("radialADC%1").arg(channel))
                || !page->findChild<QObject *>(QString("barbsADC%1").arg(channel))) return 10;
            // 族信号经页面接线交给图窗；类型不符时 QML 会报警告（warned）。
            Q_EMIT qobject_cast<DeviceT *>(backend)->channelsReady(sampleChannels<DeviceT::Channels>(std::make_index_sequence<std::tuple_size_v<typename DeviceT::Channels::element_type>>{}));
            Cdl5Profiles profiles;
            std::apply([&](auto &...p) {
                ((p = std::make_shared<const std::remove_const_t<typename std::decay_t<decltype(p)>::element_type>>(
                    std::chrono::system_clock::now(), 30.0)), ...);
            }, profiles);
            Q_EMIT cdl5->profilesReady(profiles);
        }
        const auto spec = cdl5->chartParameters();
        if (spec["rows"].toInt() != Cdl5::Rows - 80 || spec["columns"].toInt() != Cdl5::Columns
            || spec["channels"].toInt() != 3 || !cdl5->chartParameters(3).isEmpty()
            || cdl5->acquisitionParameters()["accumMax"].toInt() != Cdl5::MaxAccumSeconds) return 11;
        // 型号范围由 paramsFor 判定：越界造不出参数，也就到不了 begin。
        if (card->paramsFor(Cdl5::MaxAccumSeconds + 1, {}, {}, {}, false) || !card->paramsFor(1, {}, {}, {}, false)) return 12;
        if (card->begin(*card->paramsFor(1, {}, {}, {}, false), [](auto) {}, {})) return 9; // Offline admission must reject, without starting hardware.
        card->stop({});
        const QPointer<QObject> lifetime(backend);
        QTimer::singleShot(2500, &app, &QCoreApplication::quit);
        app.exec();
        page.reset();
        if (lifetime) return 6;
    }
    return warned ? 4 : 0;
}
