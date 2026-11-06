#include "myopenglwidget.h"
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QThread>
#include <algorithm>

class QWheelEvent;
#include <climits>
#include <cmath>
#include <cstddef>
DoughOpenGLWidget::DoughOpenGLWidget(QWidget* p) : QOpenGLWidget(p) {}
DoughOpenGLWidget::~DoughOpenGLWidget() {
    cleanup();
}
bool DoughOpenGLWidget::setFrame(DoughFrame f, QString* error) {
    auto fail = [&](const QString& s) {
        if (error)
            *error = s;
        return false;
    };
    if (QThread::currentThread() != thread())
        return fail("GUI thread required");
    if (f.indices.size() % 3 || (f.vertices.empty() != f.indices.empty()))
        return fail("Expected vertices and triangle indices, or empty frame");
    if (f.vertices.size() > INT_MAX / sizeof(DoughVertex) ||
        f.indices.size() > INT_MAX / sizeof(uint32_t))
        return fail("Frame too large");
    for (const auto& v : f.vertices) {
        const float a[] = {v.x, v.y, v.z, v.nx, v.ny, v.nz, v.cooked, v.browned, v.moisture};
        for (float x : a)
            if (!std::isfinite(x))
                return fail("Non-finite value");
        double n = double(v.nx) * v.nx + double(v.ny) * v.ny + double(v.nz) * v.nz;
        if (n < 1e-12 || n > 1e12)
            return fail("Invalid normal");
        if (v.cooked < 0 || v.cooked > 1 || v.browned < 0 || v.browned > 1 || v.moisture < 0 ||
            v.moisture > 1)
            return fail("Material outside [0,1]");
        if (std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)}) > 1e6f)
            return fail("Coordinates too large");
    }
    for (auto i : f.indices)
        if (i >= f.vertices.size())
            return fail("Index out of range");
    frame_ = std::move(f);
    dirty_ = true;
    if (error)
        error->clear();
    update();
    return true;
}
void DoughOpenGLWidget::setFluffiness(float value) {
    if (!std::isfinite(value))
        return;
    fluffiness_ = std::clamp(value, 0.f, 1.f);
    update();
}
void DoughOpenGLWidget::setSlice(bool enabled, float position) {
    if (!std::isfinite(position))
        return;
    slice_ = enabled;
    sliceX_ = position;
    update();
}
void DoughOpenGLWidget::resetCamera() {
    center_ = {};
    radius_ = 1;
    if (!frame_.vertices.empty()) {
        QVector3D lo(1e6f, 1e6f, 1e6f), hi(-1e6f, -1e6f, -1e6f);
        for (const auto& v : frame_.vertices) {
            lo.setX(std::min(lo.x(), v.x));
            lo.setY(std::min(lo.y(), v.y));
            lo.setZ(std::min(lo.z(), v.z));
            hi.setX(std::max(hi.x(), v.x));
            hi.setY(std::max(hi.y(), v.y));
            hi.setZ(std::max(hi.z(), v.z));
        }
        center_ = (lo + hi) * .5f;
        radius_ = std::max((hi - lo).length() * .5f, 1e-5f);
    }
    yaw_ = -55;
    pitch_ = 15;
    zoom_ = 3.0f;
    update();
}
void DoughOpenGLWidget::cleanup() {
    if (!context() || !context()->isValid())
        return;
    makeCurrent();
    if (ebo_)
        glDeleteBuffers(1, &ebo_);
    if (vbo_)
        glDeleteBuffers(1, &vbo_);
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
    ebo_ = vbo_ = vao_ = 0;
    program_.reset();
    doneCurrent();
}
void DoughOpenGLWidget::initializeGL() {
    if (!initializeOpenGLFunctions()) {
        error_ = "OpenGL 3.3 Core required";
        return;
    }
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &DoughOpenGLWidget::cleanup,
            Qt::DirectConnection);
    program_ = std::make_unique<QOpenGLShaderProgram>();
    const char* vs = R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec3 material;
uniform mat4 mvp;
uniform mat4 model;
out vec3 N; out vec3 P; out vec3 M; out float worldX; out vec2 cutUV;
void main(){worldX=position.x;cutUV=position.yz;P=(model*vec4(position,1)).xyz;N=mat3(model)*normal;M=material;gl_Position=mvp*vec4(position,1);}
)";
    const char* fs = R"(#version 330 core
in vec3 N;in vec3 P;in vec3 M;in float worldX;in vec2 cutUV;
uniform bool sliceEnabled;uniform float sliceX;
uniform vec3 eye;
uniform bool capPass;uniform float fluffiness;
out vec4 color;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
vec3 crumb(vec2 uv){
    vec2 p=uv*160.0;
    p+=.15*vec2(sin(p.y*1.7),sin(p.x*1.3));
    vec2 cell=floor(p);
    float distanceToPore=10.0;
    for(int j=-1;j<=1;++j)for(int i=-1;i<=1;++i){
        vec2 id=cell+vec2(i,j);
        float seed=hash(id);
        if(seed>=fluffiness*.88)continue;
        vec2 q=p-id-vec2(.22+.56*hash(id+7.1),.22+.56*hash(id+19.3));
        float angle=6.283*hash(id+3.2);
        q=mat2(cos(angle),-sin(angle),sin(angle),cos(angle))*q;
        q/=vec2(.7+.6*hash(id+4.7),.65+.5*hash(id+9.1));
        float a=atan(q.y,q.x);
        float r=(.13+.20*hash(id+2.8))*(1.0+.16*sin(3.0*a+seed*17.0)+.10*sin(5.0*a));
        distanceToPore=min(distanceToPore,length(q)-r);
    }
    float aa=max(fwidth(distanceToPore),.008);
    float inside=1.0-smoothstep(-aa,aa,distanceToPore);
    float grain=hash(floor(uv*14000.0));
    vec3 bread=vec3(.87,.69,.42)*( .95+.07*grain);
    float rim=exp(-abs(distanceToPore)*30.0);
    bread+=vec3(.09,.07,.035)*rim;
    float depth=clamp(-distanceToPore*7.0,0.0,1.0);
    vec3 hole=mix(vec3(.37,.20,.075),vec3(.075,.035,.012),depth);
    return mix(bread,hole,inside);
}
void main(){
if(sliceEnabled && worldX>sliceX) discard;
vec3 n=normalize(N)*(gl_FrontFacing?1.0:-1.0);
vec3 base=mix(vec3(.78,.69,.49),vec3(.95,.85,.61),M.x);
base=mix(base,vec3(.45,.19,.045),smoothstep(.05,.7,M.y));
base=mix(base,vec3(.022,.014,.01),smoothstep(.65,1.0,M.y));
if(capPass)base=crumb(cutUV);
vec3 l=normalize(vec3(-.5,.8,1));
float diff=max(dot(n,l),0.0);
float spec=capPass?0.0:pow(max(dot(n,normalize(l+normalize(eye-P))),0.0),mix(12.,70.,M.z));
color=vec4(pow(base*(.24+.76*diff)+vec3(spec*mix(.03,.3,M.z)),vec3(1./2.2)),1);
}
)";
    if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, vs) ||
        !program_->addShaderFromSourceCode(QOpenGLShader::Fragment, fs) || !program_->link()) {
        error_ = program_->log();
        program_.reset();
        return;
    }
    error_.clear();
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    const size_t offsets[] = {offsetof(DoughVertex, x), offsetof(DoughVertex, nx),
                              offsetof(DoughVertex, cooked)};
    for (GLuint i = 0; i < 3; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, 3, GL_FLOAT, GL_FALSE, sizeof(DoughVertex),
                              reinterpret_cast<void*>(offsets[i]));
    }
    glBindVertexArray(0);
    dirty_ = true;
}
void DoughOpenGLWidget::paintGL() {
    // QPainter also displays initialization errors if modern GL is unavailable.
    if (program_) {
        glClearColor(.055f, .07f, .09f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glBindVertexArray(vao_);
        if (dirty_) {
            glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(frame_.vertices.size() * sizeof(DoughVertex)),
                         frame_.vertices.data(), GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         GLsizeiptr(frame_.indices.size() * sizeof(uint32_t)),
                         frame_.indices.data(), GL_DYNAMIC_DRAW);
            dirty_ = false;
        }
        QMatrix4x4 model, view, projection;
        model.rotate(pitch_, 1, 0, 0);
        model.rotate(yaw_, 0, 1, 0);
        model.scale(1 / radius_);
        model.translate(-center_);
        view.translate(0, 0, -zoom_);
        projection.perspective(42, float(std::max(width(), 1)) / std::max(height(), 1), .01f,
                               100.f);
        program_->bind();
        program_->setUniformValue("model", model);
        program_->setUniformValue("mvp", projection * view * model);
        program_->setUniformValue("eye", QVector3D(0, 0, zoom_));
        program_->setUniformValue("capPass", false);
        program_->setUniformValue("fluffiness", fluffiness_);
        program_->setUniformValue("sliceEnabled", slice_);
        program_->setUniformValue("sliceX", sliceX_);
        glDrawElements(GL_TRIANGLES, GLsizei(frame_.indices.size()), GL_UNSIGNED_INT, nullptr);
        if (slice_) {
            // Triangle-plane intersections form contour edges. XOR fans fill
            // their even/odd interior, preserving holes without contour stitching.
            std::vector<DoughVertex> cap;
            const DoughVertex anchor{sliceX_, center_.y(), center_.z(), 1, 0, 0, 1, 0, .2f};
            for (size_t t = 0; t < frame_.indices.size(); t += 3) {
                DoughVertex hits[2];
                int count = 0;
                for (int e = 0; e < 3; ++e) {
                    const auto& a = frame_.vertices[frame_.indices[t + e]];
                    const auto& b = frame_.vertices[frame_.indices[t + (e + 1) % 3]];
                    if ((a.x > sliceX_) == (b.x > sliceX_))
                        continue;
                    float u = (sliceX_ - a.x) / (b.x - a.x);
                    DoughVertex h{sliceX_,
                                  a.y + u * (b.y - a.y),
                                  a.z + u * (b.z - a.z),
                                  1,
                                  0,
                                  0,
                                  a.cooked + u * (b.cooked - a.cooked),
                                  a.browned + u * (b.browned - a.browned),
                                  a.moisture + u * (b.moisture - a.moisture)};
                    if (count < 2)
                        hits[count++] = h;
                }
                if (count == 2) {
                    cap.push_back(anchor);
                    cap.push_back(hits[0]);
                    cap.push_back(hits[1]);
                }
            }
            if (!cap.empty()) {
                glBindBuffer(GL_ARRAY_BUFFER, vbo_);
                glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(cap.size() * sizeof(DoughVertex)),
                             cap.data(), GL_DYNAMIC_DRAW);
                program_->setUniformValue("sliceEnabled", false);
                glEnable(GL_STENCIL_TEST);
                glStencilMask(0xff);
                glClear(GL_STENCIL_BUFFER_BIT);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glDepthMask(GL_FALSE);
                glDisable(GL_DEPTH_TEST);
                glStencilFunc(GL_ALWAYS, 0, 0xff);
                glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
                glDrawArrays(GL_TRIANGLES, 0, GLsizei(cap.size()));
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthMask(GL_TRUE);
                glEnable(GL_DEPTH_TEST);
                // Shade one rectangle through the parity mask. Drawing overlapping
                // fans here caused interpolation seams and depth fighting.
                float ymin = cap[0].y, ymax = ymin, zmin = cap[0].z, zmax = zmin;
                for (const auto& v : cap) {
                    ymin = std::min(ymin, v.y);
                    ymax = std::max(ymax, v.y);
                    zmin = std::min(zmin, v.z);
                    zmax = std::max(zmax, v.z);
                }
                auto vertex = [&](float y, float z) {
                    return DoughVertex{sliceX_, y, z, 1, 0, 0, 1, 0, .2f};
                };
                const DoughVertex quad[] = {vertex(ymin, zmin), vertex(ymax, zmin),
                                            vertex(ymax, zmax), vertex(ymin, zmin),
                                            vertex(ymax, zmax), vertex(ymin, zmax)};
                glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
                program_->setUniformValue("capPass", true);
                glStencilFunc(GL_NOTEQUAL, 0, 0xff);
                glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glDisable(GL_STENCIL_TEST);
                dirty_ = true; // Restore original surface vertices next paint.
            }
        }
        program_->release();
        glBindVertexArray(0);
        glDisable(GL_DEPTH_TEST);
    }
    QPainter p(this);
    p.setPen(QColor(215, 222, 232));
    if (!program_)
        p.fillRect(rect(), QColor(14, 18, 23));
    p.drawText(rect().adjusted(18, 18, -18, -18), Qt::AlignLeft | Qt::AlignTop,
               error_.isEmpty() ? QString::fromUtf8("ЛКМ — вращение · колесо — масштаб") : error_);
}
void DoughOpenGLWidget::mousePressEvent(QMouseEvent* e) {
    last_ = e->position();
}
void DoughOpenGLWidget::mouseMoveEvent(QMouseEvent* e) {
    auto d = e->position() - last_;
    last_ = e->position();
    if (e->buttons() & Qt::LeftButton) {
        yaw_ += float(d.x()) * .5f;
        pitch_ = std::clamp(pitch_ + float(d.y()) * .5f, -89.f, 89.f);
        update();
    }
}
void DoughOpenGLWidget::wheelEvent(QWheelEvent* e) {
    zoom_ = std::clamp(zoom_ * std::exp(-float(e->angleDelta().y()) / 1200.f), 1.2f, 30.f);
    update();
}
