import QtQuick
import QtQuick.Window
import Learning 1.0

Window {
    id: root
    width: Screen.width
    height: Screen.height
    visible: true
    color: "white"
    title: "Learning"

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.045
        text: ink.progress
        color: "black"
        font.pixelSize: 28
    }

    Text {
        id: prompt
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: parent.height * 0.10
        text: ink.prompt
        color: "black"
        font.bold: true
        font.pixelSize: Math.min(parent.width * (ink.timedMode ? 0.065 : 0.115), 120)
        width: parent.width * 0.9
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.25
        text: ink.timerText
        visible: ink.timedMode
        color: "black"
        font.pixelSize: 48
        font.bold: true
    }

    Text {
        anchors.left: answer.left
        anchors.bottom: answer.top
        anchors.bottomMargin: 18
        text: "Write your answer here"
        visible: !ink.complete && (!ink.timedMode || !ink.canStart)
        color: "#333333"
        font.pixelSize: 34
    }

    Rectangle {
        id: answer
        visible: !ink.complete && (!ink.timedMode || !ink.canStart)
        x: parent.width * 0.08
        y: parent.height * 0.38
        width: parent.width * 0.84
        height: parent.height * 0.34
        color: "white"
        border.color: "black"
        border.width: 3
        InkCanvas {
            id: ink
            objectName: "answerCanvas"
            anchors.fill: parent
            anchors.margins: 5
        }
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: ink.complete ? prompt.bottom : answer.bottom
        anchors.topMargin: 24
        width: parent.width * 0.84
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "black"
        font.pixelSize: 30
        text: ink.error.length ? ink.error : ink.feedback
    }

    Row {
        visible: !ink.complete && (!ink.timedMode || !ink.canStart)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: parent.height * (ink.timedMode ? 0.145 : 0.055)
        spacing: 30
        Rectangle {
            width: 220; height: 90; color: "white"; border.color: "black"; border.width: 3
            Text { anchors.centerIn: parent; text: "Try again"; font.pixelSize: 30; color: "black" }
            MouseArea { anchors.fill: parent; enabled: ink.editingEnabled; onClicked: ink.clearInk() }
        }
        Rectangle {
            width: 220; height: 90; color: "black"
            Text { anchors.centerIn: parent; text: "Check"; font.pixelSize: 30; color: "white" }
            MouseArea { anchors.fill: parent; enabled: ink.checkEnabled; onClicked: ink.submit() }
        }
    }
    Row {
        visible: ink.timedMode
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: parent.height * 0.035
        spacing: 30
        Rectangle {
            visible: ink.canStart
            width: 300; height: 90; color: "black"
            Text { anchors.centerIn: parent; text: "Start practice"; font.pixelSize: 30; color: "white" }
            MouseArea { anchors.fill: parent; onClicked: ink.startPractice() }
        }
        Rectangle {
            visible: !ink.canStart && !ink.complete
            width: 220; height: 90; color: "white"; border.color: "black"; border.width: 3
            Text { anchors.centerIn: parent; text: "Stop"; font.pixelSize: 30; color: "black" }
            MouseArea { anchors.fill: parent; onClicked: ink.stopPractice() }
        }
        Rectangle {
            width: 220; height: 90; color: "white"; border.color: "black"; border.width: 3
            Text { anchors.centerIn: parent; text: "Exit"; font.pixelSize: 30; color: "black" }
            MouseArea { anchors.fill: parent; onClicked: ink.exitPractice() }
        }
    }

}
