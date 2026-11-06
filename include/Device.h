#pragma once
#include <array>
#include <memory>
#include <limits>
#include <optional>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QtQml/qqmlregistration.h>
#include <QVariantMap>
#include <DAQDevice.h>
#include <PcapForm.h>
#include <AcqSaver.h>
#include "Collector.h"
#include "WindProcessing.h"

namespace Cdl5 {
// One descriptor per received frame, beside the three unchanged binary64 payloads.
template<class CollectorT>
void saveSpectrumMetadata(const CollectorT &frame, const QDir &root, const QString &stem)
{
    constexpr auto config = CollectorT::Specification;
    const QDir dir(root.filePath("Rawspec/" + frame.context().sessionDir));
    QJsonArray channels;
    for (int channel = 0; channel < config.channels; ++channel) {
        const auto &range = config.rangeChannels[channel];
        const auto tag = channelTag(config, channel);
        channels.append(QJsonObject{{"tag", tag}, {"file", tag + "_" + stem + ".bin"},
            {"adc", 0}, {"rangeStartM", 0}, {"gateSpacingM", range.spacingM},
            {"validSamplesPerGate", range.validSamples}});
    }
    const QJsonObject metadata{{"configuredPlVersion", "0x26110401"},
        {"profile", frame.profile()}, {"rows", config.rows}, {"columns", config.columns},
        {"encoding", "float64-le"}, {"layout", "range-gate-major"},
        {"sampleRateHz", SampleRateHz}, {"fftLength", FftLength}, {"firstBin", frame.firstBin()},
        {"triggerPeriodTicks", qint64(frame.triggerPeriodTicks())},
        {"accumulationCount", qint64(frame.accumulationCount())}, {"channels", channels}};
    const auto bytes = QJsonDocument(metadata).toJson();
    QSaveFile file(dir.filePath(stem + ".json"));
    if (!dir.mkpath(QStringLiteral("."))) {
        qWarning().noquote() << "Cannot create spectrum metadata directory:" << dir.path();
        return;
    }
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        qWarning().noquote() << "Cannot save spectrum metadata:" << file.fileName() << file.errorString();
}
} // namespace Cdl5

template<class CollectorT, class Self>
auto makeCdl5Processor(Self *self)
{
    using BeamRing = std::array<WindProfile::Input, WindProfile::kBeams>;
    const QSettings settings(QDir(QCoreApplication::applicationDirPath()).filePath("cdl5-wind.ini"),
                             QSettings::IniFormat);
    return [self, reference = Cdl5::readReferenceMHz(CollectorT::Specification, settings),
            beams = std::array<BeamRing, CollectorT::Specification.channels>{},
            lastUniform = false, lastBin = -1, cursor = 0]
                  (const std::shared_ptr<const CollectorT> &frame) mutable {
        if (!frame || !frame->settled()) return;
        constexpr auto config = CollectorT::Specification;
        const auto &context = frame->context();
        const bool uniform = !config.adc0Multires() && frame->uniformRange();
        if (uniform != lastUniform || frame->firstBin() != lastBin || !context.pose) {
            beams = {};
            cursor = 0;
            lastUniform = uniform;
            lastBin = frame->firstBin();
        }
        const auto stem = context.saveRoot ? AcqSaver::fileStem(frame->frameTime()) : QString{};
        const auto sharedRanges = config.adc0Multires()
            ? QVector<double>{} : Cdl5::channelRanges(config, 0, uniform);
        QVector<AcqSaver::NonuniformRadialChannel> channels;
        for (int channel = 0; channel != config.channels; ++channel) {
            const auto ranges = config.adc0Multires()
                ? Cdl5::channelRanges(config, channel, uniform) : sharedRanges;
            const auto &values = frame->constFrame()[channel];
            const auto tag = Cdl5::channelTag(config, channel);
            Q_EMIT self->spectrumReady(channel, values);
            if (context.saveRoot)
                AcqSaver::saveSpec(*context.saveRoot, context.sessionDir,
                    stem, values, tag);
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
        if constexpr (config.adc0Multires())
            if (context.saveRoot) Cdl5::saveSpectrumMetadata(*frame, *context.saveRoot, stem);
        if (!channels.isEmpty())
            AcqSaver::saveRadialWind(*context.saveRoot, context.sessionDir, stem,
                                     AcqSaver::timeStem(frame->frameTime()), std::move(channels));
    };
}

// Qt 元对象末端：Handle 与 Device 共用一个 QObject；析构先等上传退出。
class Cdl5FourKCore final : public Device<PcapHandle, Cdl5Collector<Cdl5::FourK>>
{
    Q_OBJECT
    Q_PROPERTY(QString displayName MEMBER displayName CONSTANT)
    Q_PROPERTY(QString subtitle MEMBER subtitle CONSTANT)
    Q_PROPERTY(bool isOpen READ isOpen)
    Q_PROPERTY(QVariant holder READ qmlHolder)
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
    Q_PROPERTY(QString displayName MEMBER displayName CONSTANT)
    Q_PROPERTY(QString subtitle MEMBER subtitle CONSTANT)
    Q_PROPERTY(bool isOpen READ isOpen)
    Q_PROPERTY(QVariant holder READ qmlHolder)
public:
    using Device::Device;
    ~Cdl5FiveKCore() override { PreDestruction(); }
Q_SIGNALS:
    void lockChanged(HandleCore::ResourcePhase phase) override;
private:
    void PreDestruction() noexcept final { Device::PreDestruction(); }
};

// Qt 元对象末端：Handle 与 Device 共用一个 QObject；析构先等上传退出。
class Cdl5TenKCore final : public Device<PcapHandle, Cdl5Collector<Cdl5::TenKAdc0>>
{
    Q_OBJECT
    Q_PROPERTY(QString displayName MEMBER displayName CONSTANT)
    Q_PROPERTY(QString subtitle MEMBER subtitle CONSTANT)
    Q_PROPERTY(bool isOpen READ isOpen)
    Q_PROPERTY(QVariant holder READ qmlHolder)
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
        : Base(QStringLiteral("采集·国实CDL5"),
               QStringLiteral("%1 kHz").arg(Config.rateHz / 1000), parent)
    {
        static_assert(DeviceLifecycleLayer<Cdl5Device, decltype(&Cdl5Device::PreDestruction)>);
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
                {"channelName", name}, {"rangeStep", step},
                {"rangeMaximum", step > 0 ? step * Config.rows : Config.rangeMaximum},
                {"ranges", QVariant::fromValue(Cdl5::channelRanges(Config, channel, false))},
                {"powerMax", static_cast<double>(std::numeric_limits<float>::max())}};
    }
    bool open(int interfaceIndex)
    {
        const auto params = PcapForm::parseParameters(this->device(), interfaceIndex,
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
    : public Cdl5Device<Cdl5::TenKAdc0, Cdl5TenKCore>
#endif
{
    Q_OBJECT
    QML_ELEMENT
    QML_ATTACHED(Cdl5TenKDevice)
    QML_UNCREATABLE("由对应 CDL5 QML 官邸持有")
    Q_PROPERTY(QVariantMap connectionParameters READ connectionParameters CONSTANT)
    Q_PROPERTY(QVariantMap acquisitionParameters READ acquisitionParameters CONSTANT)
    using Base = Cdl5Device<Cdl5::TenKAdc0, Cdl5TenKCore>;
public:
    static Cdl5TenKDevice *qmlAttachedProperties(QObject *owner) { return new Cdl5TenKDevice(owner); }
    explicit Cdl5TenKDevice(QObject *parent) : Base(parent) { this->startProcessing(); }
    ~Cdl5TenKDevice() override { PreDestruction(); }
    Q_INVOKABLE QVariantMap chartParameters(int channel = 0) const { return Base::chartParameters(channel); }
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
