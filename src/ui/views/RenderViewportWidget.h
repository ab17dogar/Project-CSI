#pragma once

#include <QImage>
#include <QWidget>

namespace Raytracer {
namespace ui {

class RenderViewportWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RenderViewportWidget(QWidget *parent = nullptr);

public slots:
    void setFrame(const QImage &frame);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_frame;
};

} // namespace ui
} // namespace Raytracer
