#pragma once
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

using Cdl5ActiveCollector = Cdl5FiveKCollector; // Active board frame: 450 gates x 128 bins.

// Desktop endpoint is UDP only. Linux DMA belongs to the board-side bridge.
class Cdl5Device final
#ifdef Q_MOC_RUN
    : public DAQDeviceBase
#else
    : public DAQDevice<PcapHandle, Cdl5ActiveCollector>
#endif
{
    Q_OBJECT
    QML_ELEMENT
    QML_ATTACHED(Cdl5Device)
    QML_UNCREATABLE("由 CDL5 QML 官邸持有")
    Q_PROPERTY(QVariantMap connectionParameters READ connectionParameters CONSTANT)
    Q_PROPERTY(QVariantMap acquisitionParameters READ acquisitionParameters CONSTANT)
public:
    static Cdl5Device *qmlAttachedProperties(QObject *owner) { return new Cdl5Device(owner); }
    explicit Cdl5Device(QObject *parent)
        : DAQDevice(QStringLiteral("CDL5 采集卡"),
            QStringLiteral("网口 · UDP · %1×%2 · 三通道").arg(Cdl5ActiveCollector::Rows).arg(Cdl5ActiveCollector::Columns), parent)
    {
        setObjectName("Cdl5");
        startProcessing();
    }
    ~Cdl5Device() override { PreDestruction(); }
    QVariantMap connectionParameters() const
    { return PcapForm::connectionParameters("192.168.100.200", 55200, "192.168.100.100", 55200, std::nullopt); }
    QVariantMap acquisitionParameters() const
    {
        return {{"accumMin", 1}, {"accumMax", Cdl5::MaxAccumSeconds}, {"accumDefault", 1},
                {"saveDirDefault", QUrl::fromLocalFile(QDir::currentPath())}};
    }
    Q_INVOKABLE QVariantMap chartParameters() const
    { return {{"rows", Cdl5ActiveCollector::Rows}, {"columns", Cdl5ActiveCollector::Columns},
              {"powerMax", static_cast<double>(std::numeric_limits<float>::max())}}; }
Q_SIGNALS:
    void spectrumReady(int channel, const QVector<double> &values);
    void radialReady(int channel, const QVector<double> &ranges, const QVector<double> &speeds,
                     const QVector<double> &cnr, bool calibrated);
    void profileReady(int channel, NonuniformVerticalWindProfilePtr profile);
public Q_SLOTS:
    bool open(int interfaceIndex)
    {
        const auto params = PcapForm::parseParameters(device().logSource(), interfaceIndex,
            "192.168.100.200", 55200, "192.168.100.100", 55200, std::nullopt,
            kPcapReadTimeout, kPcapKernelBufferBytes);
        return params && device().open(*params);
    }
    void close() { device().close(); }
    void startTest(int accumSec, bool save, const QUrl &folder)
    { DAQDevice::startTest(accumSec, save, folder, AcqSaver::sessionDir(QDateTime::currentDateTime(), "Cdl5Test")); }
    void cancelTest() noexcept { DAQDevice::cancelTest(); }
private:
    void PreDestruction() noexcept final
    {
        static_assert(FinalDevice<Cdl5Device, decltype(&Cdl5Device::PreDestruction)>);
        DAQDevice::PreDestruction();
    }
    void run() override
    {
        using BeamRing = std::array<WindProfile::Input, WindProfile::kBeams>;
        consumeFrames([this, reference = Cdl5::readReferenceMHz(), beams = std::array<BeamRing, 3>{},
                       lastRanges = QVector<double>{}, lastBin = -1, cursor = 0]
                      (const std::shared_ptr<const Cdl5ActiveCollector> &frame) mutable {
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
            for (int channel = 0; channel != 3; ++channel) {
                const auto &values = frame->constFrame()[channel];
                const auto tag = QString("ADC%1").arg(channel);
                Q_EMIT spectrumReady(channel, values);
                if (context.saveRoot)
                    AcqSaver::saveSpec(*context.saveRoot, context.sessionDir,
                        AcqSaver::fileStem(frame->frameTime()), values, tag);
                if (ranges.isEmpty()) continue;
                auto [radial, calibrated] = Cdl5::radial(values, frame->Rows, frame->Columns,
                                                        frame->firstBin(), reference[channel]);
                Q_EMIT radialReady(channel, ranges, radial.ws, radial.cnr, calibrated);
                if (!calibrated) continue;
                beams[channel][cursor] = {context.pose, radial.ws};
                if (context.saveRoot) channels.append({tag, ranges, radial});
                if (context.sync) {
                    const auto profile = WindProfile::Calc(beams[channel], ranges, frame->frameTime());
                    if (profile) {
                        Q_EMIT profileReady(channel, profile);
                        if (context.saveRoot)
                            AcqSaver::saveWindProfile(profile, *context.saveRoot, context.sessionDir, tag);
                    }
                }
            }
            cursor = (cursor + 1) % WindProfile::kBeams;
            if (!channels.isEmpty())
                AcqSaver::saveRadialWind(*context.saveRoot, context.sessionDir, AcqSaver::fileStem(frame->frameTime()),
                                         AcqSaver::timeStem(frame->frameTime()), std::move(channels));
        });
    }
};
