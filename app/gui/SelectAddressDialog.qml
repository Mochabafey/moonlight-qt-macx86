import QtQuick 2.9
import QtQuick.Controls

import "theme"

// 连接 IP 选择框。PcView（对某台主机切地址）和 AppView（在应用列表里切当前主机的
// 地址）用的是同一个框：两边 model 给出的条目形状本来就一样
// （address / port / display / type / isActive / isTested / latency / isLocked），
// 只有提示语和「自动」这一项不同，所以做成参数。
//
// 选中结果通过 addressSelected 抛给调用方 —— 落地方式两边不一样
// （computerModel.setActiveAddressForComputer vs appModel.setActiveAddress + 自动开关），
// 那部分不属于这个框。第二个参数是「强制指定」勾选态。
NavigableDialog {
    id: control

    // 条目数组。约定每项含 address / port / display / type / isActive；
    // isTested 缺省视为已验证，isAuto 标记「自动选择」这种没有具体地址的伪条目，
    // latency 是这条链路最近测到的往返毫秒数（-1 表示还没测到）。
    property var addresses: []

    // 列表上方那句提示，调用方自己填（PcView 要带主机名）
    property string promptText: ""

    signal addressSelected(var address, bool forceLock)

    // 打开时预选哪一项：model 已经用 isActive 标好了当前生效的条目
    // （没固定地址时是「自动」，否则是被固定的那个），调用方不用自己算。
    readonly property int activeIndex: {
        for (var i = 0; i < addresses.length; i++) {
            if (addresses[i].isActive) {
                return i
            }
        }
        return 0
    }

    readonly property var currentAddress:
        addressCombo.currentIndex >= 0 && addressCombo.currentIndex < addresses.length
            ? addresses[addressCombo.currentIndex]
            : null

    readonly property bool hasAutoEntry: {
        for (var i = 0; i < addresses.length; i++) {
            if (addresses[i].isAuto) {
                return true
            }
        }
        return false
    }

    // 「自动」这一项没有具体地址，勾不了强制指定，所以只在选中具体地址时才显示勾选框。
    readonly property bool canLock: currentAddress !== null && !currentAddress.isAuto

    // 延迟文案。没测到过就留空，别显示 "0 ms" 骗人。
    readonly property string latencyText: {
        if (!currentAddress || currentAddress.isAuto) {
            return ""
        }
        if (currentAddress.latency === undefined || currentAddress.latency < 0) {
            return qsTr("Latency: not measured yet")
        }
        return qsTr("Latency: %1 ms").arg(currentAddress.latency)
    }

    title: qsTr("Select Connection IP")
    standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // 宽度显式给，别让内容撑：下面的 Column 要按 availableWidth 排版，
    // 两边互相依赖就成环了。
    width: Math.max(320, Math.min(560, parent ? parent.width - 80 : 480))

    onOpened: {
        addressCombo.currentIndex = activeIndex

        // 地址下拉是内容区第一个可聚焦的控件。这两页都不跑 UI 导航模式，手柄发的
        // 是真方向键，不接管上下的话会被 ComboBox 拿去换地址，底下的勾选框和
        // 确定 / 取消永远到不了。按钮由 standardButtons 生成，要等对话框建好才
        // 取得到，所以放在这里。navDownItem 的取值由 syncLockCheck() 负责。
        syncLockCheck()
        addressCombo.forceActiveFocus(Qt.TabFocusReason)
    }

    // 换地址时把勾选框同步到新条目的状态：每条地址各自记着自己的锁定态，
    // 不是全局一个开关。
    function syncLockCheck() {
        lockCheck.checked = control.canLock && control.currentAddress.isLocked === true
        addressCombo.navDownItem = control.canLock ? lockCheck : standardButton(DialogButtonBox.Ok)
    }

    onAccepted: {
        if (control.currentAddress) {
            control.addressSelected(control.currentAddress,
                                    control.canLock && lockCheck.checked)
        }
    }

    Column {
        width: control.availableWidth
        spacing: Theme.spaceMd

        Text {
            width: parent.width
            text: control.promptText
            color: Theme.text
            font.family: Theme.fontSans
            font.pointSize: Theme.fontRowTitle
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }

        // 走 AutoResizingComboBox 而不是裸 ComboBox：方角背景和方角展开面板都在
        // 那个文件里统一定义，裸 ComboBox 会退回 FluentWinUI3 的圆角面板。
        AutoResizingComboBox {
            id: addressCombo
            width: parent.width
            maximumWidth: parent.width
            popup.width: width
            model: control.addresses
            textRole: "display"

            onCurrentIndexChanged: control.syncLockCheck()
        }

        // 地址类型 / 延迟：都是机读信息，走等宽
        Text {
            width: parent.width
            visible: control.currentAddress !== null
            // 短路判断直接看 currentAddress，别看 visible：两个绑定都依赖
            // currentAddress，求值先后没有保证，靠 visible 挡的话在
            // currentAddress 变成 null 的那一拍可能先算 text 就取空指针成员了。
            text: {
                if (!control.currentAddress) {
                    return ""
                }
                var typeLine = qsTr("Type: %1").arg(control.currentAddress.type)
                return control.latencyText ? typeLine + " · " + control.latencyText : typeLine
            }
            color: Theme.textDim
            font.family: Theme.fontMono
            font.pointSize: Theme.fontBody
            wrapMode: Text.Wrap
        }

        Text {
            width: parent.width
            // isTested 缺省（undefined）当作已验证，不然没提供这个字段的调用方会
            // 满屏告警。
            visible: control.currentAddress !== null
                     && !control.currentAddress.isAuto
                     && control.currentAddress.isTested === false
            text: qsTr("Warning: This address has not been verified by polling yet.")
            color: Theme.danger
            font.family: Theme.fontMono
            font.pointSize: Theme.fontCaption
            wrapMode: Text.Wrap
        }

        HardCheckBox {
            id: lockCheck
            width: parent.width
            visible: control.canLock
            text: qsTr("Force this address (no fallback)")
            font.pointSize: Theme.fontBody

            // 手柄 / 键盘往下走：勾选框 → 确定。HardCheckBox 没有 navUpItem /
            // navDownItem，所以在这里补一句，免得焦点卡在勾选框上出不去。
            Keys.onDownPressed: {
                var okButton = control.standardButton(DialogButtonBox.Ok)
                if (okButton) {
                    okButton.forceActiveFocus(Qt.TabFocusReason)
                }
            }
        }

        Text {
            width: parent.width
            visible: control.canLock && lockCheck.checked
            text: qsTr("The connection stays on this address. If it stops responding, Moonlight will not fall back to another address.")
            color: Theme.textFaint
            font.family: Theme.fontMono
            font.pointSize: Theme.fontCaption
            wrapMode: Text.Wrap
        }

        Text {
            width: parent.width
            visible: control.hasAutoEntry
            text: qsTr("\"Auto\" uses the default address selection with automatic fallback. It prefers the fastest address that is known to work, and tries the next one when an address fails.")
            color: Theme.textFaint
            font.family: Theme.fontMono
            font.pointSize: Theme.fontCaption
            wrapMode: Text.Wrap
        }
    }
}
