#pragma once
#include <vector>
#include <string>
#include <format>
#include "WindBarbItem.h"
#include "SurfaceItem.h"
#include "Wind2DItem.h"
#include <limits>
#include <optional>
#include <QDateTime>
#include <QDir>
#include <QtQml/qqmlregistration.h>
#include <QVariantMap>
#include <DAQFamily/DAQDevice.h>
#include <PcapForm.h>
#include "Collector.h"

// Qt 只登记具体结果元组；各项仍指向算法生成的原帧。
using Cdl5Profiles = DAQDevice<PcapHandle, Cdl5Collector>::Profiles;
using Cdl5Channels = DAQDevice<PcapHandle, Cdl5Collector>::Channels;
struct Cdl5ChannelsQml {
    Q_GADGET
    QML_FOREIGN(Cdl5Channels)
    QML_ANONYMOUS
};
struct Cdl5ProfilesQml
{
    Q_GADGET
    QML_FOREIGN(Cdl5Profiles)
    QML_ANONYMOUS
};

// 末端官邸同时是 Handle、Device 与采集卡族，唯一 QObject 来自 PcapHandle；
// 处理、显示、落盘与风廓线由族层按通道规格完成，本层只给型号参数与连接入口。
class Cdl5Device final
#ifdef Q_MOC_RUN
    : public PcapHandle // 元对象基类：唯一 QObject 来自 Handle，模板层不加 Q_OBJECT。
#else
    : public DAQDevice<PcapHandle, Cdl5Collector>
#endif
{
    Q_OBJECT
    QML_ELEMENT
    QML_ATTACHED(Cdl5Device)
    QML_UNCREATABLE("由 Cdl5Module 持有")
    Q_PROPERTY(std::string displayName MEMBER displayName CONSTANT)
    Q_PROPERTY(std::string subtitle MEMBER subtitle CONSTANT)
    Q_PROPERTY(bool isOpen READ isOpen)
    Q_PROPERTY(QVariant holder READ qmlHolder)
    Q_PROPERTY(QVariantMap connectionParameters READ connectionParameters CONSTANT)
    Q_PROPERTY(QVariantMap acquisitionParameters READ acquisitionParameters CONSTANT)
    using Base = DAQDevice<PcapHandle, Cdl5Collector>;
public:
    static Cdl5Device *qmlAttachedProperties(QObject *owner) { return new Cdl5Device(owner); }
    explicit Cdl5Device(QObject *parent)
        : Base("采集·国实CDL5", std::format("{} kHz", Cdl5::RateHz / 1000), parent)
    {
        setObjectName(QStringLiteral("Cdl5%1K").arg(Cdl5::RateHz / 1000));
    }
    ~Cdl5Device() override { PreDestruction(); }
    QVariantMap connectionParameters() const
    { return PcapForm::connectionParameters(); }
    QVariantMap acquisitionParameters() const
    {
        return {{"accumMin", 1}, {"accumMax", Cdl5::MaxAccumSeconds}, {"accumDefault", 1},
                {"saveDirDefault", QUrl::fromLocalFile(QDir::currentPath())}};
    }
    // 型号判据：累积时间须在 1~MaxAccumSeconds 秒内，越界记录原因并返回空；不造 Collector、不发令。
    [[nodiscard]] std::optional<DAQParameters> paramsFor(int accumSec, std::optional<WorldModel> pose,
        std::optional<QDir> saveRoot, QString sessionDir, bool sync) const override
    {
        if (accumSec < 1 || accumSec > Cdl5::MaxAccumSeconds)
        {
            Q_EMIT logMessage(std::format("累积时间 {} 秒超出本型号范围（{}~{} 秒），未启动", accumSec, 1, Cdl5::MaxAccumSeconds));
            return std::nullopt;
        }
        return DAQParameters(*this, accumSec, std::move(pose), std::move(saveRoot), std::move(sessionDir), sync);
    }
    // 停采命令（累积 0 秒）与当前 run 无关：断开或停止时由 Device 无条件发出。
    std::vector<std::vector<unsigned char>> stopCommands() const override
    {
        const auto command = Cdl5::command(0);
        return {{command.cbegin(), command.cend()}};
    }
    Q_INVOKABLE QVariantMap chartParameters(int channel = 0) const
    {
        if (channel < 0 || channel >= Cdl5::Channels) return {};
        const int step = Cdl5::GateSpacingM[channel];
        return {{"rows", Cdl5::Rows}, {"columns", Cdl5::Columns}, {"channels", Cdl5::Channels},
                {"channelName", QStringLiteral("%1m").arg(step)},
                {"rangeStart", Cdl5::RangeStartM}, {"gateSpacing", step},
                {"freqStart", Cdl5::FirstBin * Cdl5::BinMHz}, {"freqStep", Cdl5::BinMHz},
                {"powerMax", static_cast<double>(std::numeric_limits<float>::max())}};
    }
Q_SIGNALS:
    void lockChanged(HandleCore::ResourcePhase phase) override;
    void channelsReady(const Cdl5Channels &channels) override;
    void profilesReady(const Cdl5Profiles &profiles) override;
public Q_SLOTS:
    bool open(int interfaceIndex)
    {
        const auto params = PcapForm::parseParameters(*this, interfaceIndex,
            "192.168.100.200", 55200, "192.168.100.100", 55200, std::nullopt,
            kPcapReadTimeout, kPcapKernelBufferBytes);
        // 末端的 open 槽按名遮住 Handle::open；连接参数交给 Handle 本身。
        return params && PcapHandle::open(*params);
    }
    void close() { PcapHandle::close(); }
    void startTest(int accumSec, bool save, const QUrl &folder)
    {
        Base::startTest(accumSec, save, folder,
                        AcqSaver::sessionDir(QDateTime::currentDateTime(), objectName() + "Test"));
    }
    void cancelTest() noexcept { Base::cancelTest(); }
private:
    void PreDestruction() noexcept final
    {
        static_assert(FinalDevice<Cdl5Device, decltype(&Cdl5Device::PreDestruction)>);
        Base::PreDestruction();
    }
};

// 末端仅提供 Qt 入口；历史和绘制由 ChartCore 的模板实现。
class Cdl5SixWindBarb
#ifdef Q_MOC_RUN
    : public WindBarbChart
#else
    : public WindBarbItem<typename std::tuple_element_t<0, Cdl5Profiles>::element_type>
#endif
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit Cdl5SixWindBarb(QQuickItem *parent = nullptr) : WindBarbItem(parent) {}
    Q_INVOKABLE void appendProfiles(const Cdl5Profiles &profiles)
    { appendColumn(std::get<0>(profiles)); }
};

// 末端仅提供 Qt 入口；历史和绘制由 ChartCore 的模板实现。
class Cdl5FifteenWindBarb
#ifdef Q_MOC_RUN
    : public WindBarbChart
#else
    : public WindBarbItem<typename std::tuple_element_t<1, Cdl5Profiles>::element_type>
#endif
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit Cdl5FifteenWindBarb(QQuickItem *parent = nullptr) : WindBarbItem(parent) {}
    Q_INVOKABLE void appendProfiles(const Cdl5Profiles &profiles)
    { appendColumn(std::get<1>(profiles)); }
};

// 末端仅提供 Qt 入口；历史和绘制由 ChartCore 的模板实现。
class Cdl5ThirtyWindBarb
#ifdef Q_MOC_RUN
    : public WindBarbChart
#else
    : public WindBarbItem<typename std::tuple_element_t<2, Cdl5Profiles>::element_type>
#endif
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit Cdl5ThirtyWindBarb(QQuickItem *parent = nullptr) : WindBarbItem(parent) {}
    Q_INVOKABLE void appendProfiles(const Cdl5Profiles &profiles)
    { appendColumn(std::get<2>(profiles)); }
};

// 仅适配 Qt 元对象；类型与只读份额直达绘图数值入口。
class Cdl5SixSpectrum : public SurfaceItem
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit Cdl5SixSpectrum(QQuickItem *parent = nullptr) : SurfaceItem(parent) {}
    Q_INVOKABLE bool setData(const Cdl5Channels &channels, double low, double high)
    {
        if (!channels) return false;
        return setSpectrum(std::get<0>(*channels).spectrum, low, high);
    }
};

// 仅适配 Qt 元对象；类型与只读份额直达绘图数值入口。
class Cdl5SixRadialWind : public Wind2DItem
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit Cdl5SixRadialWind(QQuickItem *parent = nullptr) : Wind2DItem(parent) {}
    Q_INVOKABLE void setData(const Cdl5Channels &channels)
    {
        if (!channels) return;
        setRadialWind(std::get<0>(*channels).radialWind);
    }
};

// 仅适配 Qt 元对象；类型与只读份额直达绘图数值入口。
class Cdl5SixCNR : public Wind2DItem
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit Cdl5SixCNR(QQuickItem *parent = nullptr) : Wind2DItem(parent) {}
    Q_INVOKABLE void setData(const Cdl5Channels &channels)
    {
        if (!channels) return;
        setCNR(std::get<0>(*channels).cnr);
    }
};

// 仅适配 Qt 元对象；类型与只读份额直达绘图数值入口。
class Cdl5FifteenSpectrum : public SurfaceItem
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit Cdl5FifteenSpectrum(QQuickItem *parent = nullptr) : SurfaceItem(parent) {}
    Q_INVOKABLE bool setData(const Cdl5Channels &channels, double low, double high)
    {
        if (!channels) return false;
        return setSpectrum(std::get<1>(*channels).spectrum, low, high);
    }
};

// 仅适配 Qt 元对象；类型与只读份额直达绘图数值入口。
class Cdl5FifteenRadialWind : public Wind2DItem
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit Cdl5FifteenRadialWind(QQuickItem *parent = nullptr) : Wind2DItem(parent) {}
    Q_INVOKABLE void setData(const Cdl5Channels &channels)
    {
        if (!channels) return;
        setRadialWind(std::get<1>(*channels).radialWind);
    }
};

// 仅适配 Qt 元对象；类型与只读份额直达绘图数值入口。
class Cdl5FifteenCNR : public Wind2DItem
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit Cdl5FifteenCNR(QQuickItem *parent = nullptr) : Wind2DItem(parent) {}
    Q_INVOKABLE void setData(const Cdl5Channels &channels)
    {
        if (!channels) return;
        setCNR(std::get<1>(*channels).cnr);
    }
};

// 仅适配 Qt 元对象；类型与只读份额直达绘图数值入口。
class Cdl5ThirtySpectrum : public SurfaceItem
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit Cdl5ThirtySpectrum(QQuickItem *parent = nullptr) : SurfaceItem(parent) {}
    Q_INVOKABLE bool setData(const Cdl5Channels &channels, double low, double high)
    {
        if (!channels) return false;
        return setSpectrum(std::get<2>(*channels).spectrum, low, high);
    }
};

// 仅适配 Qt 元对象；类型与只读份额直达绘图数值入口。
class Cdl5ThirtyRadialWind : public Wind2DItem
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit Cdl5ThirtyRadialWind(QQuickItem *parent = nullptr) : Wind2DItem(parent) {}
    Q_INVOKABLE void setData(const Cdl5Channels &channels)
    {
        if (!channels) return;
        setRadialWind(std::get<2>(*channels).radialWind);
    }
};

// 仅适配 Qt 元对象；类型与只读份额直达绘图数值入口。
class Cdl5ThirtyCNR : public Wind2DItem
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit Cdl5ThirtyCNR(QQuickItem *parent = nullptr) : Wind2DItem(parent) {}
    Q_INVOKABLE void setData(const Cdl5Channels &channels)
    {
        if (!channels) return;
        setCNR(std::get<2>(*channels).cnr);
    }
};
