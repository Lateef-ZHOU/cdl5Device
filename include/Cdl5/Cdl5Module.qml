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
    mainPages: [
        Page {
            title: qsTr("ADC0 功率谱 3D")
            contentItem: SurfaceView {
                id: surface0
                specification: internals.chartParameters
                Connections {
                    target: internals.device
                    function onSpectrumReady(channel, values) {
                        if (channel === 0) surface0.updateSpectrum(values)
                    }
                }
            }
        },
        Page {
            title: qsTr("ADC1 功率谱 3D")
            contentItem: SurfaceView {
                id: surface1
                specification: internals.chartParameters
                Connections {
                    target: internals.device
                    function onSpectrumReady(channel, values) {
                        if (channel === 1) surface1.updateSpectrum(values)
                    }
                }
            }
        },
        Page {
            title: qsTr("ADC2 功率谱 3D")
            contentItem: SurfaceView {
                id: surface2
                specification: internals.chartParameters
                Connections {
                    target: internals.device
                    function onSpectrumReady(channel, values) {
                        if (channel === 2) surface2.updateSpectrum(values)
                    }
                }
            }
        }
    ]
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
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            opacity: 0.6
            text: qsTr("%1:%2 ↔ %3:%4")
                .arg(internals.connectionParameters.deviceIPv4Default).arg(internals.connectionParameters.devicePortDefault)
                .arg(internals.connectionParameters.hostIPv4Default).arg(internals.connectionParameters.hostPortDefault)
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
    objectName: "Cdl5Module"
    readonly property string name: internals.device.device.displayName
    readonly property string subtitle: internals.device.device.subtitle
    // 借用公共只读资源接口，不暴露具体型号后端或缓存状态。
    readonly property var deviceState: internals.device.device


    QtObject {
        id: internals
        readonly property var chartParameters: device.chartParameters()
        readonly property var connectionParameters: device.connectionParameters
        readonly property Cdl5Device device: root.Cdl5Device
    }
    Connections {
        target: internals.device.device.logSource
        function onLogMessage(text): void { root.logMessage(text) }
    }
}
