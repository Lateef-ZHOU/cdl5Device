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
    Q_PROPERTY(QString displayName MEMBER displayName CONSTANT)
    Q_PROPERTY(QString subtitle MEMBER subtitle CONSTANT)
    Q_PROPERTY(bool isOpen READ isOpen)
    Q_PROPERTY(QVariant holder READ qmlHolder)
    Q_PROPERTY(QVariantMap connectionParameters READ connectionParameters CONSTANT)
    Q_PROPERTY(QVariantMap acquisitionParameters READ acquisitionParameters CONSTANT)
    using Base = DAQDevice<PcapHandle, Cdl5Collector>;
public:
    static Cdl5Device *qmlAttachedProperties(QObject *owner) { return new Cdl5Device(owner); }
    explicit Cdl5Device(QObject *parent)
        : Base(QStringLiteral("采集·国实CDL5"), QStringLiteral("%1 kHz").arg(Cdl5::RateHz / 1000), parent)
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
            Q_EMIT logMessage(QStringLiteral("累积时间 %1 秒超出本型号范围（%2~%3 秒），未启动")
                                  .arg(accumSec).arg(1).arg(Cdl5::MaxAccumSeconds));
            return std::nullopt;
        }
        return DAQParameters{accumSec, std::move(pose), std::move(saveRoot), std::move(sessionDir), sync};
    }
    Q_INVOKABLE QVariantMap chartParameters(int channel = 0) const
    {
        if (channel < 0 || channel >= Cdl5::Channels) return {};
        const int step = Cdl5::GateSpacingM[channel];
        return {{"rows", Cdl5::Rows}, {"columns", Cdl5::Columns}, {"channels", Cdl5::Channels},
                {"channelName", QStringLiteral("%1m").arg(step)},
                {"rangeStart", Cdl5::RangeStartM}, {"gateSpacing", step},
                {"powerMax", static_cast<double>(std::numeric_limits<float>::max())}};
    }
Q_SIGNALS:
    void lockChanged(HandleCore::ResourcePhase phase) override;
    void spectrumReady(int channel, const QVector<double> &values) override;
    void radialReady(int channel, const QVector<double> &speeds, const QVector<double> &cnr, bool calibrated) override;
    void profileReady(int channel, VerticalWindProfilePtr profile) override;
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
