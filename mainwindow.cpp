#include "mainwindow.h"
#include "demodough.h"
#include "myopenglwidget.h"
#include <QCheckBox>
#include <QLabel>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <memory>
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    auto* gl = new DoughOpenGLWidget(this);
    setCentralWidget(gl);
    setWindowTitle(QString::fromUtf8("Тесто — просмотр поверхности"));
    auto* bar = addToolBar(QString::fromUtf8("Демонстрация"));
    bar->setMovable(false);
    auto slider = [&](const char* title, int value) {
        bar->addWidget(new QLabel(QString::fromUtf8(title), bar));
        auto* s = new QSlider(Qt::Horizontal, bar);
        s->setRange(0, 100);
        s->setValue(value);
        s->setMaximumWidth(120);
        bar->addWidget(s);
        return s;
    };
    auto* donenessSlider = slider("Готовность ", 70);
    auto* browningSlider = slider("Подгорание ", 40);
    auto* moistureSlider = slider("Влажность ", 30);
    auto* fluffinessSlider = slider("Пышность ", 50);
    auto elapsedSeconds = std::make_shared<float>(0.f);
    auto refresh = [=] {
        gl->setFluffiness(fluffinessSlider->value() / 100.f);
        gl->setFrame(makeDemoDough(donenessSlider->value() / 100.f, browningSlider->value() / 100.f,
                                   moistureSlider->value() / 100.f,
                                   fluffinessSlider->value() / 100.f, *elapsedSeconds));
    };
    for (auto* s : {donenessSlider, browningSlider, moistureSlider, fluffinessSlider})
        connect(s, &QSlider::valueChanged, this, [refresh](int) { refresh(); });
    connect(bar->addAction(QString::fromUtf8("Вписать")), &QAction::triggered, gl,
            &DoughOpenGLWidget::resetCamera);
    addToolBarBreak();
    auto* cutBar = addToolBar(QString::fromUtf8("Срез"));
    auto* cut = new QCheckBox(QString::fromUtf8("Срез по X"), cutBar);
    cutBar->addWidget(cut);
    auto* position = new QSlider(Qt::Horizontal, cutBar);
    position->setRange(-120, 120);
    position->setValue(0);
    position->setMaximumWidth(300);
    cutBar->addWidget(position);
    auto updateCut = [=] { gl->setSlice(cut->isChecked(), position->value() / 1000.f); };
    connect(cut, &QCheckBox::toggled, this, [updateCut](bool) { updateCut(); });
    connect(position, &QSlider::valueChanged, this, [updateCut](int) { updateCut(); });
    cut->setChecked(true);
    updateCut();
    auto* animate = new QCheckBox(QString::fromUtf8("Движение"), cutBar);
    animate->setChecked(true);
    cutBar->addWidget(animate);
    auto* timer = new QTimer(this);
    timer->setInterval(33);
    connect(timer, &QTimer::timeout, this, [=] {
        if (animate->isChecked()) {
            *elapsedSeconds += .033f;
            refresh();
        }
    });
    timer->start();
    refresh();
    gl->resetCamera();
    statusBar()->showMessage(QString::fromUtf8("Демо-деформация • Пышность меняет количество "
                                               "нарисованных пор • Движение можно приостановить"));
    resize(1100, 720);
}
