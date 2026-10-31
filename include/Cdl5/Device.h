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

using Cdl5ActiveCollector = Cdl5FiveKCollector; // Current board: P8, 450 gates x 128 bins.

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
    { return PcapForm::connectionParameters("192.168.1.102", 55200, "192.168.1.11", 55200, std::nullopt); }
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
public Q_SLOTS:
    bool open(int interfaceIndex)
    {
        const auto params = PcapForm::parseParameters(device().logSource(), interfaceIndex,
            "192.168.1.102", 55200, "192.168.1.11", 55200, std::nullopt,
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
        consumeFrames([this](const std::shared_ptr<const Cdl5ActiveCollector> &frame) {
            if (!frame || !frame->settled()) return;
            const auto &context = frame->context();
            for (int channel = 0; channel != 3; ++channel) {
                const auto &values = frame->constFrame()[channel];
                Q_EMIT spectrumReady(channel, values);
                if (context.saveRoot)
                    AcqSaver::saveSpec(*context.saveRoot, context.sessionDir,
                        AcqSaver::fileStem(frame->frameTime()), values, QString("ADC%1").arg(channel));
            }
        });
    }
};
