#pragma once
#include "doughframe.h"
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPointF>
#include <QVector3D>
#include <memory>
class DoughOpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
  public:
    explicit DoughOpenGLWidget(QWidget* parent = nullptr);
    ~DoughOpenGLWidget() override;
    bool setFrame(DoughFrame frame, QString* error = nullptr); // GUI thread only.
    void resetCamera();
    void setFluffiness(float value);             // [0,1], procedural pores on the cut only.
    void setSlice(bool enabled, float position); // X plane, position in metres
    QString rendererError() const {
        return error_;
    }

  protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

  private:
    void cleanup();
    DoughFrame frame_;
    bool dirty_ = true;
    bool slice_ = false;
    float sliceX_ = 0, fluffiness_ = 0.5f;
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    std::unique_ptr<QOpenGLShaderProgram> program_;
    QString error_;
    QVector3D center_;
    float radius_ = 1, yaw_ = 30, pitch_ = 20, zoom_ = 3.4f;
    QPointF last_;
};
