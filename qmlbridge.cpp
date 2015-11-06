#include <iostream>
#include <cassert>
#include <unistd.h>
#include <QMessageBox>

#include "qmlbridge.h"

#include "core/flash.h"
#include "core/keypad.h"

QMLBridge::QMLBridge(QObject *parent) : QObject(parent)
{}

QMLBridge::~QMLBridge()
{
    #ifdef MOBILE_UI
        emu_thread.stop();
    #endif
}

constexpr const int ROWS = 8, COLS = 11;

void QMLBridge::keypadStateChanged(int keymap_id, bool state)
{
    int col = keymap_id % 11, row = keymap_id / 11;
    assert(row < ROWS);
    //assert(col < COLS); Not needed.

    if(state)
        keypad.key_map[row] |= 1 << col;
    else
        keypad.key_map[row] &= ~(1 << col);
}

static QObject *buttons[ROWS][COLS];

void QMLBridge::registerNButton(int keymap_id, QVariant button)
{
    int col = keymap_id % COLS, row = keymap_id / COLS;
    assert(row < ROWS);
    //assert(col < COLS); Not needed.

    if(buttons[row][col])
        qWarning() << "Warning: Button " << keymap_id << " already registered as " << buttons[row][col] << "!";
    else
        buttons[row][col] = button.value<QObject*>();
}

void QMLBridge::touchpadStateChanged(qreal x, qreal y, bool state)
{
    keypad.touchpad_down = keypad.touchpad_contact = state;

    if(state)
    {
        keypad.touchpad_x = x * TOUCHPAD_X_MAX;
        keypad.touchpad_y = (1.0f-y) * TOUCHPAD_Y_MAX;
    }

    keypad.kpc.gpio_int_active |= 0x800;
    keypad_int_check();
}

static QObject *qml_touchpad;

void QMLBridge::registerTouchpad(QVariant touchpad)
{
    qml_touchpad = touchpad.value<QObject*>();
}

bool QMLBridge::isMobile()
{
    // TODO: Mobile UI on desktop? Q_OS_ANDROID doesn't work somehow.
    #ifdef MOBILE_UI
        return true;
    #else
        return false;
    #endif
}

#ifdef MOBILE_UI

bool QMLBridge::restart()
{
    if(emu_thread.isRunning() && !emu_thread.stop())
        return false;

#if defined(Q_OS_IOS)
    QString docsPath = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation)[0];
    std::string boot1_path_str = (docsPath + "/boot1.img").toStdString();
    std::string flash_path_str = (docsPath + "/flash.img").toStdString();
     if(access(boot1_path_str.c_str(), F_OK) != -1 && access(flash_path_str.c_str(), F_OK) != -1) {
         // Both files are good to use.
        emu_thread.boot1 = boot1_path_str;
        emu_thread.flash = flash_path_str;
    }
#else
    emu_thread.boot1 = getBoot1Path().toStdString();
    emu_thread.flash = getFlashPath().toStdString();
#endif

    if(emu_thread.boot1 != "" && emu_thread.flash != "") {
        emu_thread.start();
        return true;
    } else {
        QMessageBox::warning(nullptr, tr("Error"), tr("You need to select a proper boot1 and flash image before.\nSwipe the keypad to the left to show the settings menu."));
        return false;
    }
}

void QMLBridge::setPaused(bool b)
{
    emu_thread.setPaused(b);
}

void QMLBridge::reset()
{
    emu_thread.reset();
}

void QMLBridge::suspend()
{
    QString snapshot_path = settings.value("snapshotPath").toString();
    if(!snapshot_path.isEmpty())
        emu_thread.suspend(snapshot_path.toStdString());
}

void QMLBridge::resume()
{
    QString snapshot_path = settings.value("snapshotPath").toString();
    if(!snapshot_path.isEmpty())
        emu_thread.resume(snapshot_path.toStdString());
}

bool QMLBridge::stop()
{
    return emu_thread.stop();
}

bool QMLBridge::saveFlash()
{
    return flash_save_changes();
}

QString QMLBridge::getBoot1Path()
{
    return settings.value("boot1", "").toString();
}

void QMLBridge::setBoot1Path(QUrl path)
{
    settings.setValue("boot1", path.toLocalFile());
}

QString QMLBridge::getFlashPath()
{
    return settings.value("flash", "").toString();
}

void QMLBridge::setFlashPath(QUrl path)
{
    settings.setValue("flash", path.toLocalFile());
}

QString QMLBridge::getSnapshotPath()
{
    return settings.value("snapshotPath", "").toString();
}

void QMLBridge::setSnapshotPath(QUrl path)
{
    settings.setValue("snapshotPath", path.toLocalFile());
}

QString QMLBridge::basename(QString path)
{
    if(path == "")
        return "None";

    QFileInfo file_info(path);
    return file_info.fileName();
}

#endif

void notifyKeypadStateChanged(int row, int col, bool state)
{
    assert(row < ROWS);
    assert(col < COLS);

    if(!buttons[row][col])
    {
        qWarning() << "Warning: Button " << row*11+col << " not present in keypad!";
        return;
    }

    QQmlProperty::write(buttons[row][col], "state", state);
}

QObject *qmlBridgeFactory(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

    return new QMLBridge();
}

void notifyTouchpadStateChanged(qreal x, qreal y, bool state)
{
    if(!qml_touchpad)
    {
        qWarning("Warning: No touchpad registered!");
        return;
    }

    QVariant ret;

    if(state)
        QMetaObject::invokeMethod(qml_touchpad, "showHighlight", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, QVariant(x)), Q_ARG(QVariant, QVariant(y)));
    else
        QMetaObject::invokeMethod(qml_touchpad, "hideHighlight");
}

void notifyTouchpadStateChanged()
{
    notifyTouchpadStateChanged(float(keypad.touchpad_x)/TOUCHPAD_X_MAX, 1.0f-(float(keypad.touchpad_y)/TOUCHPAD_Y_MAX), keypad.touchpad_contact);
}
