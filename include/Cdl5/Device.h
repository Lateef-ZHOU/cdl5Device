#pragma once
#include <array>
#include <memory>
#include <limits>
#include <optional>
#include <QDateTime>
#include <QtQml/qqmlregistration.h>
#include <QVariantMap>
#include <Device/DAQ/DAQDevice.h>
#include <Device/PcapForm.h>
#include <Acquisition/AcqSaver.h>
#include "Collector.h"
#include "WindProcessing.h"

template<class CollectorT, class Self>
auto makeCdl5Processor(Self *self)
{
    using BeamRing = std::array<WindProfile::Input, WindProfile::kBeams>;
    return [self, reference = Cdl5::readReferenceMHz(), beams = std::array<BeamRing, CollectorT::Specification.channels>{},
                   lastRanges = QVector<double>{}, lastBin = -1, cursor = 0]
                  (const std::shared_ptr<const CollectorT> &frame) mutable {
        if (!frame || !frame->settled()) return;
        const auto &context = frame->context();
        const auto ranges = Cdl5::gateRanges(frame->profile(), frame->uniformRange(), frame->Rows);
        if (ranges != lastRanges || frame->firstBin() != lastBin || !context.pose) {
            beams = {};
            cursor = 0;
            lastRanges = ranges;
            lastBin = frame->firstBin();
        }
        QVector<AcqSaver::NonuniformRadialChannel> channels;
        for (int channel = 0; channel != CollectorT::Specification.channels; ++channel) {
            const auto &values = frame->constFrame()[channel];
            const auto tag = QString("ADC%1").arg(channel);
            Q_EMIT self->spectrumReady(channel, values);
            if (context.saveRoot)
                AcqSaver::saveSpec(*context.saveRoot, context.sessionDir,
                    AcqSaver::fileStem(frame->frameTime()), values, tag);
            if (ranges.isEmpty()) continue;
            auto [radial, calibrated] = Cdl5::radial(values, frame->Rows, frame->Columns,
                                                    frame->firstBin(), reference[channel]);
            Q_EMIT self->radialReady(channel, ranges, radial.ws, radial.cnr, calibrated);
            if (!calibrated) continue;
            beams[channel][cursor] = {context.pose, radial.ws};
            if (context.saveRoot) channels.append({tag, ranges, radial});
            if (context.sync) {
                const auto profile = WindProfile::Calc(beams[channel], ranges, frame->frameTime());
                if (profile) {
                    Q_EMIT self->profileReady(channel, profile);
                    if (context.saveRoot)
                        AcqSaver::saveWindProfile(profile, *context.saveRoot, context.sessionDir, tag);
                }
            }
        }
        cursor = (cursor + 1) % WindProfile::kBeams;
        if (!channels.isEmpty())
            AcqSaver::saveRadialWind(*context.saveRoot, context.sessionDir, AcqSaver::fileStem(frame->frameTime()),
                                     AcqSaver::timeStem(frame->frameTime()), std::move(channels));
    };
}

// Qt 元对象末端：Handle 与 Device 共用一个 QObject；析构先等上传退出。
class Cdl5FourKCore final : public Device<PcapHandle, Cdl5Collector<Cdl5::FourK>>
{
    Q_OBJECT
    Q_PROPERTY(QString displayName READ displayName CONSTANT)
    Q_PROPERTY(QString subtitle READ subtitle CONSTANT)
    Q_PROPERTY(QObject *logSource READ logSourceObject CONSTANT)
    Q_PROPERTY(bool isOpen READ isOpen)
    Q_PROPERTY(bool isLocked READ isLocked)
    Q_PROPERTY(QString holder READ holder)
public:
    using Device::Device;
    ~Cdl5FourKCore() override { PreDestruction(); }
Q_SIGNALS:
    void lockChanged(HandleCore::ResourcePhase phase) override;
private:
    void PreDestruction() noexcept final { Device::PreDestruction(); }
};

// Qt 元对象末端：Handle 与 Device 共用一个 QObject；析构先等上传退出。
class Cdl5FiveKCore final : public Device<PcapHandle, Cdl5Collector<Cdl5::FiveK>>
{
    Q_OBJECT
    Q_PROPERTY(QString displayName READ displayName CONSTANT)
    Q_PROPERTY(QString subtitle READ subtitle CONSTANT)
    Q_PROPERTY(QObject *logSource READ logSourceObject CONSTANT)
    Q_PROPERTY(bool isOpen READ isOpen)
    Q_PROPERTY(bool isLocked READ isLocked)
    Q_PROPERTY(QString holder READ holder)
public:
    using Device::Device;
    ~Cdl5FiveKCore() override { PreDestruction(); }
Q_SIGNALS:
    void lockChanged(HandleCore::ResourcePhase phase) override;
private:
    void PreDestruction() noexcept final { Device::PreDestruction(); }
};

// Qt 元对象末端：Handle 与 Device 共用一个 QObject；析构先等上传退出。
class Cdl5TenKCore final : public Device<PcapHandle, Cdl5Collector<Cdl5::TenK>>
{
    Q_OBJECT
    Q_PROPERTY(QString displayName READ displayName CONSTANT)
    Q_PROPERTY(QString subtitle READ subtitle CONSTANT)
    Q_PROPERTY(QObject *logSource READ logSourceObject CONSTANT)
    Q_PROPERTY(bool isOpen READ isOpen)
    Q_PROPERTY(bool isLocked READ isLocked)
    Q_PROPERTY(QString holder READ holder)
public:
    using Device::Device;
    ~Cdl5TenKCore() override { PreDestruction(); }
Q_SIGNALS:
    void lockChanged(HandleCore::ResourcePhase phase) override;
private:
    void PreDestruction() noexcept final { Device::PreDestruction(); }
};

// Specification is a compile-time value. The final QObject owns its worker lifetime.
template<Cdl5::Configuration Config, typename Core>
class Cdl5Device : public DAQDevice<Core>
{
    using Base = DAQDevice<Core>;
public:
    using CollectorType = Cdl5Collector<Config>;
    explicit Cdl5Device(QObject *parent)
        : Base(QStringLiteral("CDL5 采集卡"),
               QStringLiteral("网口·UDP·%1kHz").arg(Config.rateHz / 1000), parent)
    {
        static_assert(DeviceLifecycleLayer<Cdl5Device, decltype(&Cdl5Device::PreDestruction)>);
        this->setObjectName(QStringLiteral("Cdl5%1K").arg(Config.rateHz / 1000));
    }
    ~Cdl5Device() override = default;
    QVariantMap connectionParameters() const
    { return PcapForm::connectionParameters("192.168.100.200", 55200, "192.168.100.100", 55200, std::nullopt); }
    QVariantMap acquisitionParameters() const
    {
        return {{"accumMin", 1}, {"accumMax", Config.maxAccumSeconds()}, {"accumDefault", 1},
                {"saveDirDefault", QUrl::fromLocalFile(QDir::currentPath())}};
    }
    QVariantMap chartParameters() const
    { return {{"rows", Config.rows}, {"columns", Config.columns}, {"channels", Config.channels},
              {"rangeMaximum", Config.rangeMaximum},
              {"powerMax", static_cast<double>(std::numeric_limits<float>::max())}}; }
    bool open(int interfaceIndex)
    {
        const auto params = PcapForm::parseParameters(this->device().logSource(), interfaceIndex,
            "192.168.100.200", 55200, "192.168.100.100", 55200, std::nullopt,
            Base::kPcapReadTimeout, Base::kPcapKernelBufferBytes);
        return params && this->device().open(*params);
    }
protected:
    void PreDestruction() noexcept override { Base::PreDestruction(); }
};

class Cdl5FourKDevice final
#ifdef Q_MOC_RUN
    : public DAQDeviceBase
#else
    : public Cdl5Device<Cdl5::FourK, Cdl5FourKCore>
#endif
{
    Q_OBJECT
    QML_ELEMENT
    QML_ATTACHED(Cdl5FourKDevice)
    QML_UNCREATABLE("由对应 CDL5 QML 官邸持有")
    Q_PROPERTY(QVariantMap connectionParameters READ connectionParameters CONSTANT)
    Q_PROPERTY(QVariantMap acquisitionParameters READ acquisitionParameters CONSTANT)
    using Base = Cdl5Device<Cdl5::FourK, Cdl5FourKCore>;
public:
    static Cdl5FourKDevice *qmlAttachedProperties(QObject *owner) { return new Cdl5FourKDevice(owner); }
    explicit Cdl5FourKDevice(QObject *parent) : Base(parent) { this->startProcessing(); }
    ~Cdl5FourKDevice() override { PreDestruction(); }
    Q_INVOKABLE QVariantMap chartParameters() const { return Base::chartParameters(); }
Q_SIGNALS:
    void spectrumReady(int channel, const QVector<double> &values);
    void radialReady(int channel, const QVector<double> &ranges, const QVector<double> &speeds,
                     const QVector<double> &cnr, bool calibrated);
    void profileReady(int channel, NonuniformVerticalWindProfilePtr profile);
public Q_SLOTS:
    bool open(int interfaceIndex) { return Base::open(interfaceIndex); }
    void close() { this->device().close(); }
    void startTest(int accumSec, bool save, const QUrl &folder)
    { Base::startTest(accumSec, save, folder, AcqSaver::sessionDir(QDateTime::currentDateTime(), this->objectName() + "Test")); }
    void cancelTest() noexcept { Base::cancelTest(); }
private:
    void run() override { this->consumeFrames(makeCdl5Processor<CollectorType>(this)); }
    void PreDestruction() noexcept final
    {
        static_assert(FinalDevice<Cdl5FourKDevice, decltype(&Cdl5FourKDevice::PreDestruction)>);
        Base::PreDestruction();
    }
};

class Cdl5FiveKDevice final
#ifdef Q_MOC_RUN
    : public DAQDeviceBase
#else
    : public Cdl5Device<Cdl5::FiveK, Cdl5FiveKCore>
#endif
{
    Q_OBJECT
    QML_ELEMENT
    QML_ATTACHED(Cdl5FiveKDevice)
    QML_UNCREATABLE("由对应 CDL5 QML 官邸持有")
    Q_PROPERTY(QVariantMap connectionParameters READ connectionParameters CONSTANT)
    Q_PROPERTY(QVariantMap acquisitionParameters READ acquisitionParameters CONSTANT)
    using Base = Cdl5Device<Cdl5::FiveK, Cdl5FiveKCore>;
public:
    static Cdl5FiveKDevice *qmlAttachedProperties(QObject *owner) { return new Cdl5FiveKDevice(owner); }
    explicit Cdl5FiveKDevice(QObject *parent) : Base(parent) { this->startProcessing(); }
    ~Cdl5FiveKDevice() override { PreDestruction(); }
    Q_INVOKABLE QVariantMap chartParameters() const { return Base::chartParameters(); }
Q_SIGNALS:
    void spectrumReady(int channel, const QVector<double> &values);
    void radialReady(int channel, const QVector<double> &ranges, const QVector<double> &speeds,
                     const QVector<double> &cnr, bool calibrated);
    void profileReady(int channel, NonuniformVerticalWindProfilePtr profile);
public Q_SLOTS:
    bool open(int interfaceIndex) { return Base::open(interfaceIndex); }
    void close() { this->device().close(); }
    void startTest(int accumSec, bool save, const QUrl &folder)
    { Base::startTest(accumSec, save, folder, AcqSaver::sessionDir(QDateTime::currentDateTime(), this->objectName() + "Test")); }
    void cancelTest() noexcept { Base::cancelTest(); }
private:
    void run() override { this->consumeFrames(makeCdl5Processor<CollectorType>(this)); }
    void PreDestruction() noexcept final
    {
        static_assert(FinalDevice<Cdl5FiveKDevice, decltype(&Cdl5FiveKDevice::PreDestruction)>);
        Base::PreDestruction();
    }
};

class Cdl5TenKDevice final
#ifdef Q_MOC_RUN
    : public DAQDeviceBase
#else
    : public Cdl5Device<Cdl5::TenK, Cdl5TenKCore>
#endif
{
    Q_OBJECT
    QML_ELEMENT
    QML_ATTACHED(Cdl5TenKDevice)
    QML_UNCREATABLE("由对应 CDL5 QML 官邸持有")
    Q_PROPERTY(QVariantMap connectionParameters READ connectionParameters CONSTANT)
    Q_PROPERTY(QVariantMap acquisitionParameters READ acquisitionParameters CONSTANT)
    using Base = Cdl5Device<Cdl5::TenK, Cdl5TenKCore>;
public:
    static Cdl5TenKDevice *qmlAttachedProperties(QObject *owner) { return new Cdl5TenKDevice(owner); }
    explicit Cdl5TenKDevice(QObject *parent) : Base(parent) { this->startProcessing(); }
    ~Cdl5TenKDevice() override { PreDestruction(); }
    Q_INVOKABLE QVariantMap chartParameters() const { return Base::chartParameters(); }
Q_SIGNALS:
    void spectrumReady(int channel, const QVector<double> &values);
    void radialReady(int channel, const QVector<double> &ranges, const QVector<double> &speeds,
                     const QVector<double> &cnr, bool calibrated);
    void profileReady(int channel, NonuniformVerticalWindProfilePtr profile);
public Q_SLOTS:
    bool open(int interfaceIndex) { return Base::open(interfaceIndex); }
    void close() { this->device().close(); }
    void startTest(int accumSec, bool save, const QUrl &folder)
    { Base::startTest(accumSec, save, folder, AcqSaver::sessionDir(QDateTime::currentDateTime(), this->objectName() + "Test")); }
    void cancelTest() noexcept { Base::cancelTest(); }
private:
    void run() override { this->consumeFrames(makeCdl5Processor<CollectorType>(this)); }
    void PreDestruction() noexcept final
    {
        static_assert(FinalDevice<Cdl5TenKDevice, decltype(&Cdl5TenKDevice::PreDestruction)>);
        Base::PreDestruction();
    }
};
