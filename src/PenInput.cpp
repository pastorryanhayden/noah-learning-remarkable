#include "PenInput.h"
#include "InkCanvas.h"
#include <QDateTime>
#include <QDir>
#include <QQuickWindow>
#include <QSocketNotifier>
#include <QtGlobal>
#include <algorithm>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

PenInput::PenInput(InkCanvas *canvas, QObject *parent) : QObject(parent), m_canvas(canvas) {}
PenInput::~PenInput() { if (m_fd >= 0) ::close(m_fd); }

void PenInput::setRanges(int minX, int maxX, int minY, int maxY) {
    m_minX = minX; m_maxX = std::max(minX + 1, maxX);
    m_minY = minY; m_maxY = std::max(minY + 1, maxY);
}

QPointF PenInput::mappedPoint() const {
    double x = std::clamp(double(m_x - m_minX) / (m_maxX - m_minX), 0.0, 1.0);
    double y = std::clamp(double(m_y - m_minY) / (m_maxY - m_minY), 0.0, 1.0);
    if (qEnvironmentVariableIsSet("RM_PEN_SWAP_XY")) std::swap(x, y);
    if (qEnvironmentVariableIsSet("RM_PEN_INVERT_X")) x = 1 - x;
    if (qEnvironmentVariableIsSet("RM_PEN_INVERT_Y")) y = 1 - y;
    const double width = m_canvas->window() ? m_canvas->window()->width() : m_canvas->width();
    const double height = m_canvas->window() ? m_canvas->window()->height() : m_canvas->height();
    const QPointF scene(x * width, y * height);
    return m_canvas->window() ? m_canvas->mapFromScene(scene) : scene;
}

void PenInput::processEvent(unsigned short type, unsigned short code, int value, qint64 ms) {
    if (type == EV_ABS) {
        if (code == ABS_X) m_x = value;
        if (code == ABS_Y) m_y = value;
        if (code == ABS_PRESSURE) m_pressure = value;
    }
    if (type == EV_KEY && code == BTN_TOUCH) m_down = value != 0;
    if (type != EV_SYN || code != SYN_REPORT) return;
    if (!m_canvas->editingEnabled()) {
        m_capturing = false;
        m_wasDown = m_down;
        return;
    }
    m_ms = ms;
    const QPointF point = mappedPoint();
    const bool inside = point.x() >= 0 && point.y() >= 0
        && point.x() < m_canvas->width() && point.y() < m_canvas->height();
    if (m_down && !m_wasDown) {
        m_capturing = inside;
        if (m_capturing) m_canvas->beginStroke(point, double(m_pressure) / m_maxPressure, m_ms);
    } else if (m_down && m_wasDown && m_capturing) {
        if (inside) m_canvas->appendPoint(point, double(m_pressure) / m_maxPressure, m_ms);
        else { m_canvas->endStroke(); m_capturing = false; }
    } else if (!m_down && m_wasDown) {
        if (m_capturing) m_canvas->endStroke();
        m_capturing = false;
    }
    m_wasDown = m_down;
}

bool PenInput::start() {
    QStringList paths;
    const auto explicitPath = qEnvironmentVariable("RM_PEN_DEVICE");
    if (!explicitPath.isEmpty()) paths << explicitPath;
    else {
        QDir input("/dev/input");
        for (const auto &name : input.entryList({"event*"}, QDir::System | QDir::Files)) paths << input.filePath(name);
    }
    for (const auto &path : paths) {
        int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        char name[256] = {};
        input_absinfo x{}, y{}, pressure{};
        const bool axes = ioctl(fd, EVIOCGABS(ABS_X), &x) >= 0 && ioctl(fd, EVIOCGABS(ABS_Y), &y) >= 0;
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        const QString deviceName = QString::fromLocal8Bit(name).toLower();
        const bool candidate = axes && (deviceName.contains("wacom") || deviceName.contains("pen") || deviceName.contains("stylus") || !explicitPath.isEmpty());
        if (!candidate) { ::close(fd); continue; }
        m_fd = fd;
        setRanges(x.minimum, x.maximum, y.minimum, y.maximum);
        if (ioctl(fd, EVIOCGABS(ABS_PRESSURE), &pressure) >= 0) m_maxPressure = std::max(1, pressure.maximum);
        m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, [this] { readEvents(); });
        return true;
    }
    m_canvas->setExternalError("Marker input unavailable. Set RM_PEN_DEVICE to the tablet's pen event device.");
    return false;
}

void PenInput::readEvents() {
    input_event event{};
    while (::read(m_fd, &event, sizeof(event)) == sizeof(event))
        processEvent(event.type, event.code, event.value,
                     qint64(event.input_event_sec) * 1000 + event.input_event_usec / 1000);
}
