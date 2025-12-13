#include "ui/views/RenderViewportWidget.h"

#include <QPainter>
#include <QPaintEvent>

namespace Raytracer {
namespace ui {

RenderViewportWidget::RenderViewportWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 300);
    setAutoFillBackground(true);
}

void RenderViewportWidget::setFrame(const QImage &frame)
{
    m_frame = frame;
    update();
}

void RenderViewportWidget::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));

    if (m_frame.isNull()) {
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect(), Qt::AlignCenter, tr("No frame rendered yet."));
        return;
    }

    const QImage scaled = m_frame.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const int x = (width() - scaled.width()) / 2;
    const int y = (height() - scaled.height()) / 2;
    painter.drawImage(QPoint(x, y), scaled);
}

} // namespace ui
} // namespace Raytracer
