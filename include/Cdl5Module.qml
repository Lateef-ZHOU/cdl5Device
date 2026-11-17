pragma ComponentBehavior: Bound
import QtQuick
import Lidar.UI
import Lidar.TaskUI
import Lidar.Charts
import QtQuick.Controls
import QtQuick.Layouts

// 末端页面独持具体型号后端，在这里连接业务、图窗与控制分区。
ModulePage {
    id: root
    readonly property string kind: "device"
    readonly property QtObject family: internals.device // 采集卡族基不是 QObject，交出设备对象本身
    property var acquisitionParameters
    property int accum: acquisitionParameters.accumDefault
    property url saveDir: acquisitionParameters.saveDirDefault
    property bool save: false
    sections: [connectionSection, acquisitionSection].filter(item => item !== null)
    // 界面只选择本机网卡；其他连接参数固定在型号配置中。
    property int networkInterfaceIndex: internals.connectionParameters.networkInterfaceDefault
    component ChannelPlots: QtObject {
        id: adc
        required property int channel
        required property Cdl5Device backend
        required property SurfaceItem spectrumPlot
        required property Wind2DItem windPlot
        required property Wind2DItem cnrPlot
        required property WindBarbChart profilePlot
        readonly property var spectrumConfiguration: surface.plotConfiguration
        readonly property var windConfiguration: radial.windConfiguration
        readonly property var cnrConfiguration: radial.cnrConfiguration
        required property var spec
        readonly property list<Page> pages: [
            Page {
                title: qsTr("%1·功率谱").arg(adc.spec.channelName)
                contentItem: SurfaceView {
                    id: surface
                    plot: adc.spectrumPlot
                    specification: adc.spec
                }
            },
            Page {
                title: qsTr("%1·径向风").arg(adc.spec.channelName)
                contentItem: Wind2DView {
                    id: radial
                    windPlot: adc.windPlot
                    cnrPlot: adc.cnrPlot
                    objectName: "radialADC" + adc.channel
                    specification: ({
                            rows: adc.spec.rows,
                            rangeStart: adc.spec.rangeStart,
                            gateSpacing: adc.spec.gateSpacing,
                            windMin: -60,
                            windMax: 60,
                            cnrMin: -50,
                            cnrMax: 50
                        })
                }
            },
            Page {
                title: qsTr("%1·风廓线").arg(adc.spec.channelName)
                contentItem: adc.profilePlot
            }
        ]
        readonly property Connections updates: Connections {
            target: adc.backend
            function onChannelsReady(channels) {
                surface.updateSpectrum(channels)
                radial.updateWS(channels)
                radial.updateCNR(channels)
            }

        }
    }
    readonly property ChannelPlots adc0: ChannelPlots {
        channel: 0
        spectrumPlot: Cdl5SixSpectrum { configuration: root.adc0.spectrumConfiguration }
        windPlot: Cdl5SixRadialWind { configuration: root.adc0.windConfiguration }
        cnrPlot: Cdl5SixCNR { configuration: root.adc0.cnrConfiguration }
        profilePlot: Cdl5SixWindBarb {
            id: barbs0
            objectName: "barbsADC0"
            configuration: ({heightBins: 1, windowSeconds: 120, columnSeconds: 1,
                maxHeight: root.adc0.spec.rangeStart + root.adc0.spec.rows * root.adc0.spec.gateSpacing})
            Connections {
                target: internals.device
                function onProfilesReady(profiles) { barbs0.appendProfiles(profiles) }
            }
        }
        backend: internals.device
        spec: backend.chartParameters(channel)
    }
    readonly property ChannelPlots adc1: ChannelPlots {
        channel: 1
        spectrumPlot: Cdl5FifteenSpectrum { configuration: root.adc1.spectrumConfiguration }
        windPlot: Cdl5FifteenRadialWind { configuration: root.adc1.windConfiguration }
        cnrPlot: Cdl5FifteenCNR { configuration: root.adc1.cnrConfiguration }
        profilePlot: Cdl5FifteenWindBarb {
            id: barbs1
            objectName: "barbsADC1"
            configuration: ({heightBins: 1, windowSeconds: 120, columnSeconds: 1,
                maxHeight: root.adc1.spec.rangeStart + root.adc1.spec.rows * root.adc1.spec.gateSpacing})
            Connections {
                target: internals.device
                function onProfilesReady(profiles) { barbs1.appendProfiles(profiles) }
            }
        }
        backend: internals.device
        spec: backend.chartParameters(channel)
    }
    readonly property ChannelPlots adc2: ChannelPlots {
        channel: 2
        spectrumPlot: Cdl5ThirtySpectrum { configuration: root.adc2.spectrumConfiguration }
        windPlot: Cdl5ThirtyRadialWind { configuration: root.adc2.windConfiguration }
        cnrPlot: Cdl5ThirtyCNR { configuration: root.adc2.cnrConfiguration }
        profilePlot: Cdl5ThirtyWindBarb {
            id: barbs2
            objectName: "barbsADC2"
            configuration: ({heightBins: 1, windowSeconds: 120, columnSeconds: 1,
                maxHeight: root.adc2.spec.rangeStart + root.adc2.spec.rows * root.adc2.spec.gateSpacing})
            Connections {
                target: internals.device
                function onProfilesReady(profiles) { barbs2.appendProfiles(profiles) }
            }
        }
        backend: internals.device
        spec: backend.chartParameters(channel)
    }
    mainPages: [...adc0.pages, ...adc1.pages, ...adc2.pages]
    readonly property Section connectionSection: ConnectionSection {
        title: qsTr("设备连接")

        label: qsTr("网卡")
        ChoiceField {
            Layout.fillWidth: true
            model: internals.connectionParameters.networkInterfaceOptions
            tips: internals.connectionParameters.networkInterfaceTips
            value: root.networkInterfaceIndex
            onCommitted: i => root.networkInterfaceIndex = i
        }

        firstAction: Action {
            text: qsTr("连接")
            onTriggered: {
                if (internals.device.open(root.networkInterfaceIndex))
                    root.logMessage(qsTr("连接请求已提交"));
            }
        }
        secondAction: Action {
            text: qsTr("断开")
            onTriggered: internals.device.close()
        }
    }
    acquisitionParameters: internals.device.acquisitionParameters
    readonly property Section acquisitionSection: DAQControl {
        device: internals.device
        acquisition: root
    }
    objectName: "Cdl5Module"
    readonly property string name: String(internals.device.displayName)
    readonly property string subtitle: String(internals.device.subtitle)
    // 借用公共只读资源接口（即设备对象本身），不另存状态。
    readonly property var deviceState: internals.device

    QtObject {
        id: internals
        readonly property var connectionParameters: device.connectionParameters
        readonly property Cdl5Device device: root.Cdl5Device
    }
    Connections {
        target: internals.device
        function onLogMessage(text): void {
            root.logMessage(text);
        }
    }
}
