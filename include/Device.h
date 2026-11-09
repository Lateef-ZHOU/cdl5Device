#pragma once
#include <limits>
#include <optional>
#include <QDateTime>
#include <QDir>
#include <QtQml/qqmlregistration.h>
#include <QVariantMap>
#include <DAQDevice.h>
#include <PcapForm.h>
#include "Collector.h"

// Specification is a compile-time value. 末端官邸同时是 Handle、Device 与采集卡族，唯一 QObject 来自 PcapHandle；
// 处理、显示、落盘与风廓线由族层按通道规格完成，本层只给型号参数与连接入口。
template<Cdl5::Configuration Config>
class Cdl5Device : public DAQDevice<PcapHandle, Cdl5Collector<Config>>
{
    using Base = DAQDevice<PcapHandle, Cdl5Collector<Config>>;
public:
    using CollectorType = Cdl5Collector<Config>;
    explicit Cdl5Device(QObject *parent)
        : Base(QStringLiteral("采集·国实CDL5"),
               QStringLiteral("%1 kHz").arg(Config.rateHz / 1000), parent)
    {
        this->setObjectName(QStringLiteral("Cdl5%1K").arg(Config.rateHz / 1000));
    }
    ~Cdl5Device() override = default;
    QVariantMap connectionParameters() const
    { return PcapForm::connectionParameters(); }
    QVariantMap acquisitionParameters() const
    {
        return {{"accumMin", 1}, {"accumMax", Config.maxAccumSeconds()}, {"accumDefault", 1},
                {"saveDirDefault", QUrl::fromLocalFile(QDir::currentPath())}};
    }
    QVariantMap chartParameters(int channel = 0) const
    {
        if (channel < 0 || channel >= Config.channels) return {};
        const int step = Config.rangeChannels[channel].spacingM;
        const auto name = step > 0 ? QStringLiteral("%1m").arg(step) : Cdl5::channelTag(Config, channel);
        return {{"rows", Config.rows}, {"columns", Config.columns}, {"channels", Config.channels},
                {"channelName", name},
                {"rangeMaximum", step > 0 ? step * Config.rows : Config.rangeMaximum},
                {"ranges", QVariant::fromValue(Cdl5::channelRanges(Config, channel, false))},
                {"powerMax", static_cast<double>(std::numeric_limits<float>::max())}};
    }
    bool openInterface(int interfaceIndex)
    {
        const auto params = PcapForm::parseParameters(*this, interfaceIndex,
            "192.168.100.200", 55200, "192.168.100.100", 55200, std::nullopt,
            Base::kPcapReadTimeout, Base::kPcapKernelBufferBytes);
        // 末端的 open 槽按名遮住 Handle::open；连接参数交给 Handle 本身。
        return params && PcapHandle::open(*params);
    }
    void startNamedTest(int accumSec, bool save, const QUrl &folder)
    {
        Base::startTest(accumSec, save, folder,
                        AcqSaver::sessionDir(QDateTime::currentDateTime(), this->objectName() + "Test"));
    }
};

class Cdl5FourKDevice final
#ifdef Q_MOC_RUN
    : public PcapHandle // 元对象基类：唯一 QObject 来自 Handle，模板层不加 Q_OBJECT。
#else
    : public Cdl5Device<Cdl5::FourK>
#endif
{
    Q_OBJECT
    QML_ELEMENT
    QML_ATTACHED(Cdl5FourKDevice)
    QML_UNCREATABLE("由对应 CDL5 QML 官邸持有")
    Q_PROPERTY(QString displayName MEMBER displayName CONSTANT)
    Q_PROPERTY(QString subtitle MEMBER subtitle CONSTANT)
    Q_PROPERTY(bool isOpen READ isOpen)
    Q_PROPERTY(QVariant holder READ qmlHolder)
    Q_PROPERTY(QVariantMap connectionParameters READ connectionParameters CONSTANT)
    Q_PROPERTY(QVariantMap acquisitionParameters READ acquisitionParameters CONSTANT)
    using Base = Cdl5Device<Cdl5::FourK>;
public:
    static Cdl5FourKDevice *qmlAttachedProperties(QObject *owner) { return new Cdl5FourKDevice(owner); }
    explicit Cdl5FourKDevice(QObject *parent) : Base(parent) {}
    ~Cdl5FourKDevice() override { PreDestruction(); }
    Q_INVOKABLE QVariantMap chartParameters() const { return Base::chartParameters(); }
Q_SIGNALS:
    void lockChanged(HandleCore::ResourcePhase phase) override;
    void spectrumReady(int channel, const QVector<double> &values) override;
    void radialReady(int channel, const QVector<double> &ranges, const QVector<double> &speeds,
                     const QVector<double> &cnr, bool calibrated) override;
    void profileReady(int channel, NonuniformVerticalWindProfilePtr profile) override;
public Q_SLOTS:
    bool open(int interfaceIndex) { return openInterface(interfaceIndex); }
    void close() { PcapHandle::close(); }
    void startTest(int accumSec, bool save, const QUrl &folder) { startNamedTest(accumSec, save, folder); }
    void cancelTest() noexcept { Base::cancelTest(); }
private:
    void PreDestruction() noexcept final
    {
        static_assert(FinalDevice<Cdl5FourKDevice, decltype(&Cdl5FourKDevice::PreDestruction)>);
        Base::PreDestruction();
    }
};

class Cdl5FiveKDevice final
#ifdef Q_MOC_RUN
    : public PcapHandle // 元对象基类：唯一 QObject 来自 Handle，模板层不加 Q_OBJECT。
#else
    : public Cdl5Device<Cdl5::FiveK>
#endif
{
    Q_OBJECT
    QML_ELEMENT
    QML_ATTACHED(Cdl5FiveKDevice)
    QML_UNCREATABLE("由对应 CDL5 QML 官邸持有")
    Q_PROPERTY(QString displayName MEMBER displayName CONSTANT)
    Q_PROPERTY(QString subtitle MEMBER subtitle CONSTANT)
    Q_PROPERTY(bool isOpen READ isOpen)
    Q_PROPERTY(QVariant holder READ qmlHolder)
    Q_PROPERTY(QVariantMap connectionParameters READ connectionParameters CONSTANT)
    Q_PROPERTY(QVariantMap acquisitionParameters READ acquisitionParameters CONSTANT)
    using Base = Cdl5Device<Cdl5::FiveK>;
public:
    static Cdl5FiveKDevice *qmlAttachedProperties(QObject *owner) { return new Cdl5FiveKDevice(owner); }
    explicit Cdl5FiveKDevice(QObject *parent) : Base(parent) {}
    ~Cdl5FiveKDevice() override { PreDestruction(); }
    Q_INVOKABLE QVariantMap chartParameters() const { return Base::chartParameters(); }
Q_SIGNALS:
    void lockChanged(HandleCore::ResourcePhase phase) override;
    void spectrumReady(int channel, const QVector<double> &values) override;
    void radialReady(int channel, const QVector<double> &ranges, const QVector<double> &speeds,
                     const QVector<double> &cnr, bool calibrated) override;
    void profileReady(int channel, NonuniformVerticalWindProfilePtr profile) override;
public Q_SLOTS:
    bool open(int interfaceIndex) { return openInterface(interfaceIndex); }
    void close() { PcapHandle::close(); }
    void startTest(int accumSec, bool save, const QUrl &folder) { startNamedTest(accumSec, save, folder); }
    void cancelTest() noexcept { Base::cancelTest(); }
private:
    void PreDestruction() noexcept final
    {
        static_assert(FinalDevice<Cdl5FiveKDevice, decltype(&Cdl5FiveKDevice::PreDestruction)>);
        Base::PreDestruction();
    }
};

class Cdl5TenKDevice final
#ifdef Q_MOC_RUN
    : public PcapHandle // 元对象基类：唯一 QObject 来自 Handle，模板层不加 Q_OBJECT。
#else
    : public Cdl5Device<Cdl5::TenKAdc0>
#endif
{
    Q_OBJECT
    QML_ELEMENT
    QML_ATTACHED(Cdl5TenKDevice)
    QML_UNCREATABLE("由对应 CDL5 QML 官邸持有")
    Q_PROPERTY(QString displayName MEMBER displayName CONSTANT)
    Q_PROPERTY(QString subtitle MEMBER subtitle CONSTANT)
    Q_PROPERTY(bool isOpen READ isOpen)
    Q_PROPERTY(QVariant holder READ qmlHolder)
    Q_PROPERTY(QVariantMap connectionParameters READ connectionParameters CONSTANT)
    Q_PROPERTY(QVariantMap acquisitionParameters READ acquisitionParameters CONSTANT)
    using Base = Cdl5Device<Cdl5::TenKAdc0>;
public:
    static Cdl5TenKDevice *qmlAttachedProperties(QObject *owner) { return new Cdl5TenKDevice(owner); }
    explicit Cdl5TenKDevice(QObject *parent) : Base(parent) {}
    ~Cdl5TenKDevice() override { PreDestruction(); }
    Q_INVOKABLE QVariantMap chartParameters(int channel = 0) const { return Base::chartParameters(channel); }
Q_SIGNALS:
    void lockChanged(HandleCore::ResourcePhase phase) override;
    void spectrumReady(int channel, const QVector<double> &values) override;
    void radialReady(int channel, const QVector<double> &ranges, const QVector<double> &speeds,
                     const QVector<double> &cnr, bool calibrated) override;
    void profileReady(int channel, NonuniformVerticalWindProfilePtr profile) override;
public Q_SLOTS:
    bool open(int interfaceIndex) { return openInterface(interfaceIndex); }
    void close() { PcapHandle::close(); }
    void startTest(int accumSec, bool save, const QUrl &folder) { startNamedTest(accumSec, save, folder); }
    void cancelTest() noexcept { Base::cancelTest(); }
private:
    void PreDestruction() noexcept final
    {
        static_assert(FinalDevice<Cdl5TenKDevice, decltype(&Cdl5TenKDevice::PreDestruction)>);
        Base::PreDestruction();
    }
};
