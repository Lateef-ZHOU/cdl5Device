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
        required property Cdl5TenKDevice backend
        required property var spec
        readonly property list<Page> pages: [
            Page {
                title: qsTr("%1·功率谱").arg(adc.spec.channelName)
                contentItem: SurfaceView {
                    id: surface
                    specification: adc.spec
                }
            },
            Page {
                title: qsTr("%1·径向风").arg(adc.spec.channelName)
                contentItem: NonuniformWind2DView {
                    id: radial
                    objectName: "radialADC" + adc.channel
                    specification: ({
                            windMin: -60,
                            windMax: 60,
                            cnrMin: -50,
                            cnrMax: 50,
                            ranges: adc.spec.ranges,
                            rangeMinimum: 0,
                            rangeMaximum: adc.spec.rangeMaximum
                        })
                }
            },
            Page {
                title: qsTr("%1·风廓线").arg(adc.spec.channelName)
                contentItem: NonuniformWindBarbItem {
                    id: barbs
                    objectName: "barbsADC" + adc.channel
                    configuration: ({
                            heightBins: 1,
                            windowSeconds: 120,
                            columnSeconds: 1,
                            maxHeight: adc.spec.rangeMaximum
                        })
                }
            }
        ]
        readonly property Connections updates: Connections {
            target: adc.backend
            function onSpectrumReady(channel, values) {
                if (channel === adc.channel)
                    surface.updateSpectrum(values);
            }
            function onRadialReady(channel, ranges, speeds, cnr, calibrated) {
                if (channel !== adc.channel)
                    return;
                radial.updateWS(ranges, speeds);
                radial.updateCNR(ranges, cnr);
                if (!calibrated)
                    root.logMessage(qsTr("ADC0·%1：CNR 已更新。零风速参考频率不在本帧频带内，风速缺测。").arg(adc.spec.channelName));
            }
            function onProfileReady(channel, frame) {
                if (channel === adc.channel)
                    barbs.appendColumn(frame);
            }
        }
    }
    readonly property ChannelPlots adc0: ChannelPlots {
        channel: 0
        backend: internals.device
        spec: backend.chartParameters(channel)
    }
    readonly property ChannelPlots adc1: ChannelPlots {
        channel: 1
        backend: internals.device
        spec: backend.chartParameters(channel)
    }
    readonly property ChannelPlots adc2: ChannelPlots {
        channel: 2
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
    objectName: "Cdl5TenKModule"
    readonly property string name: internals.device.displayName
    readonly property string subtitle: internals.device.subtitle
    // 借用公共只读资源接口（即设备对象本身），不另存状态。
    readonly property var deviceState: internals.device

    QtObject {
        id: internals
        readonly property var connectionParameters: device.connectionParameters
        readonly property Cdl5TenKDevice device: root.Cdl5TenKDevice
    }
    Connections {
        target: internals.device
        function onLogMessage(text): void {
            root.logMessage(text);
        }
    }
}
