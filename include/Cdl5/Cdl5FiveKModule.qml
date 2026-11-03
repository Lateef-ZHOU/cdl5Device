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
    readonly property DAQDeviceBase family: internals.device
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
        required property Cdl5FiveKDevice backend
        required property var spec
        readonly property list<Page> pages: [
            Page {
                title: qsTr("ADC%1·功率谱").arg(adc.channel)
                contentItem: SurfaceView {
                    id: surface
                    specification: adc.spec
                }
            },
            Page {
                title: qsTr("ADC%1·径向风").arg(adc.channel)
                contentItem: ColumnLayout {
                    Label {
                        id: radialNote
                        Layout.fillWidth: true
                        text: qsTr("等待接收功率谱")
                        wrapMode: Text.Wrap
                    }
                    NonuniformWind2DView {
                        id: radial
                        objectName: "radialADC" + adc.channel
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        specification: ({windMin: -60, windMax: 60, cnrMin: -50, cnrMax: 50,
                                         rangeMinimum: 0, rangeMaximum: adc.spec.rangeMaximum})
                    }
                }
            },
            Page {
                title: qsTr("ADC%1·风廓线").arg(adc.channel)
                contentItem: ColumnLayout {
                    Label {
                        id: profileNote
                        Layout.fillWidth: true
                        text: qsTr("等待已标定的四束采集结果")
                    }
                    NonuniformWindBarbItem {
                        id: barbs
                        objectName: "barbsADC" + adc.channel
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        configuration: ({heightBins: 1, windowSeconds: 120, columnSeconds: 1, maxHeight: adc.spec.rangeMaximum})
                    }
                }
            }
        ]
        readonly property Connections updates: Connections {
            target: adc.backend
            function onSpectrumReady(channel, values) {
                if (channel === adc.channel) surface.updateSpectrum(values)
            }
            function onRadialReady(channel, ranges, speeds, cnr, calibrated) {
                if (channel !== adc.channel) return
                radial.updateWS(ranges, speeds)
                radial.updateCNR(ranges, cnr)
                radialNote.visible = !calibrated
                if (!calibrated)
                    radialNote.text = qsTr("CNR 已更新。风速需在 cdl5-wind.ini 填入谱带内的零风速参考频率。")
            }
            function onProfileReady(channel, frame) {
                if (channel === adc.channel && barbs.appendColumn(frame)) profileNote.visible = false
            }
        }
    }
    readonly property ChannelPlots adc0: ChannelPlots { channel: 0; backend: internals.device; spec: internals.chartParameters }
    readonly property ChannelPlots adc1: ChannelPlots { channel: 1; backend: internals.device; spec: internals.chartParameters }
    readonly property ChannelPlots adc2: ChannelPlots { channel: 2; backend: internals.device; spec: internals.chartParameters }
    mainPages: [...adc0.pages, ...adc1.pages, ...adc2.pages]
    readonly property Section connectionSection: Section {
        title: qsTr("设备连接")
        FieldRow {
            label: qsTr("网卡")
            ChoiceField {
                Layout.fillWidth: true
                model: internals.connectionParameters.networkInterfaceOptions
                tips: internals.connectionParameters.networkInterfaceTips
                value: root.networkInterfaceIndex
                onCommitted: i => root.networkInterfaceIndex = i
            }
        }
        RowLayout {
            Button {
                Layout.fillWidth: true
                text: qsTr("连接")
                onClicked: {
                    if (internals.device.open(root.networkInterfaceIndex))
                        root.logMessage(qsTr("连接请求已提交"))
                }
            }
            Button { Layout.fillWidth: true; text: qsTr("断开"); onClicked: internals.device.close() }
        }
    }
    acquisitionParameters: internals.device.acquisitionParameters
    readonly property Section acquisitionSection: DAQControl {
        device: internals.device
        acquisition: root
    }
    objectName: "Cdl5FiveKModule"
    readonly property string name: internals.device.device.displayName
    readonly property string subtitle: internals.device.device.subtitle
    // 借用公共只读资源接口，不暴露具体型号后端或缓存状态。
    readonly property var deviceState: internals.device.device


    QtObject {
        id: internals
        readonly property var chartParameters: device.chartParameters()
        readonly property var connectionParameters: device.connectionParameters
        readonly property Cdl5FiveKDevice device: root.Cdl5FiveKDevice
    }
    Connections {
        target: internals.device.device.logSource
        function onLogMessage(text): void { root.logMessage(text) }
    }
}
