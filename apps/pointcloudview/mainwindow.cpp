#include "mainwindow.h"
#include "registrationdialog.h"
#include "ui_mainwindow.h"
#include <pcv/interface/temp_workpiece_interface.h>
#include <pcv/output/plane_output.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QSignalBlocker>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMatrix4x4>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPainter>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QAbstractItemView>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVector4D>
#include <QVector2D>
#include <QVector3D>
#include <QSet>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QThread>
#include <QDialog>
#include <QSignalBlocker>
#include <QtMath>
#include <QDir>
#include <QPixmap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <limits>
#include <cmath>
#include <functional>
#include <memory>

class PointCloudCanvas final : public QOpenGLWidget,
                               protected QOpenGLFunctions_3_3_Core {
public:
    enum PointState : quint8 {
        NormalPoint = 0,
        PlanePoint = 1,
        EdgePoint = 2
    };
    std::function<void(int)> pointPicked;
    std::function<void(const QRectF &)> edgeRectanglePicked;

    explicit PointCloudCanvas(QWidget *parent = nullptr)
        : QOpenGLWidget(parent), m_vertexBuffer(QOpenGLBuffer::VertexBuffer),
          m_stateBuffer(QOpenGLBuffer::VertexBuffer),
          m_contourBuffer(QOpenGLBuffer::VertexBuffer) {
        setMinimumSize(620, 480);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setToolTip(tr("左键旋转，右键平移，滚轮缩放"));
    }

    ~PointCloudCanvas() override {
        if (QCoreApplication::closingDown() || !context() || !context()->isValid()) return;
        makeCurrent();
        if (QOpenGLContext::currentContext() != context()) return;
        m_pickingFbo.reset();
        if (m_contourBuffer.isCreated()) m_contourBuffer.destroy();
        if (m_contourVertexArray.isCreated()) m_contourVertexArray.destroy();
        if (m_stateBuffer.isCreated()) m_stateBuffer.destroy();
        if (m_vertexBuffer.isCreated()) m_vertexBuffer.destroy();
        if (m_vertexArray.isCreated()) m_vertexArray.destroy();
        doneCurrent();
    }

    void setCloud(QVector<pointcloud::Point3D> points) {
        if (QThread::currentThread() != thread()) {
            qWarning() << "PointCloudCanvas::setCloud queued to GUI thread, points="
                       << points.size();
            QMetaObject::invokeMethod(
                this,
                [this, points = std::move(points)]() mutable {
                    setCloud(std::move(points));
                },
                Qt::QueuedConnection);
            return;
        }
        m_points = std::move(points);
        m_pointStates.fill(NormalPoint, m_points.size());
        m_planeResultIndices.clear();
        m_edgeResultIndices.clear();
        m_selectedIndices.clear();
        m_coordinatePointIndex = -1;
        m_workpieceCoordinate = {};
        m_contourVertices.clear();
        m_contourRanges.clear();
        m_contourUploadPending = true;
        updateBounds();
        m_uploadError.clear();
        m_uploadPending = true;
        qInfo() << "PointCloudCanvas cache published, points=" << m_points.size();
        update();
    }

    void setSelectionMode(bool enabled) {
        if (QThread::currentThread() != thread()) return;
        m_selectionMode = enabled;
        setToolTip(enabled ? tr("左键选择点，右键平移，Esc 取消，Backspace 撤销")
                           : tr("左键旋转，右键平移，滚轮缩放"));
        setFocus();
    }

    void setEdgeSelectionMode(bool enabled, const QVector<int> &edgeIndices) {
        if (QThread::currentThread() != thread()) return;
        m_edgeSelectionMode = enabled;
        m_edgeIndices = edgeIndices;
        setToolTip(enabled ? tr("左键选择边缘真实点，Esc 取消")
                           : tr("左键旋转，右键平移，滚轮缩放"));
        setFocus();
    }

    void setSelectedEdgeIndices(const QVector<int> &indices) {
        if (QThread::currentThread() != thread()) return;
        m_selectedEdgeIndices = indices;
        update();
    }

    QVector<int> pickRectangleForSelection(const QRectF &selection) {
        return pickRectangle(selection);
    }

    void setSelectedIndices(const QVector<int> &indices) {
        if (QThread::currentThread() != thread()) return;
        m_selectedIndices = indices;
        update();
    }

    void setPlaneResult(const QVector<int> &planeIndices, const QVector<int> &edgeIndices,
                        const QVector<pointcloud::PlaneContour> &contours) {
        if (QThread::currentThread() != thread()) return;
        m_planeResultIndices = planeIndices;
        m_edgeResultIndices = edgeIndices;
        m_pointStates.fill(NormalPoint, m_points.size());
        for (int index : m_planeResultIndices)
            if (index >= 0 && index < m_pointStates.size()) m_pointStates[index] = PlanePoint;
        for (int index : m_edgeResultIndices)
            if (index >= 0 && index < m_pointStates.size()) m_pointStates[index] = EdgePoint;
        m_contourVertices.clear();
        m_contourRanges.clear();
        for (const pointcloud::PlaneContour &contour : contours) {
            if (contour.points.size() < 2) continue;
            const int start = m_contourVertices.size();
            m_contourVertices += contour.points;
            m_contourRanges.push_back({start, int(contour.points.size())});
        }
        m_stateUploadPending = true;
        m_contourUploadPending = true;
        update();
    }

    void setExtractedPlane(const QVector<int> &planeIndices) {
        setPlaneResult(planeIndices, {}, {});
    }

    void clearPlaneResult() {
        if (QThread::currentThread() != thread()) return;
        m_pointStates.fill(NormalPoint, m_points.size());
        m_planeResultIndices.clear();
        m_edgeResultIndices.clear();
        m_contourVertices.clear();
        m_contourRanges.clear();
        m_stateUploadPending = true;
        m_contourUploadPending = true;
        update();
    }

    void setWorkpieceCoordinateSystem(const pointcloud::WorkpieceCoordinateSystem &frame) {
        if (QThread::currentThread() != thread()) return;
        m_workpieceCoordinate = frame;
        update();
    }

    void clearWorkpieceCoordinateSystem() {
        if (QThread::currentThread() != thread()) return;
        m_workpieceCoordinate = {};
        update();
    }

    void setPointSize(int size) {
        if (QThread::currentThread() != thread()) return;
        m_pointSize = qBound(1, size, 8);
        update();
    }

    void setDisplayOptions(int colorMode, double overlay, double mapMin,
                           double mapMax) {
        if (QThread::currentThread() != thread()) return;
        m_colorMode = colorMode;
        m_overlay = float(qBound(0.0, overlay, 1.0));
        m_mapMin = float(mapMin);
        m_mapMax = float(mapMax);
        update();
    }

    void resetView() {
        if (QThread::currentThread() != thread()) return;
        m_yaw = 38.0f;
        m_pitch = -28.0f;
        m_zoom = 1.0f;
        m_pan = QPointF();
        update();
    }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        // A failed/invalid context may legally return nullptr from glGetString.
        // Passing that pointer directly to QString::fromLatin1() caused the
        // Windows 0xFFFFFFFFFFFFFFFF read-access crash during QWidget::show().
        const auto safeGlString = [this](GLenum name) {
            const GLubyte *value = glGetString(name);
            return value ? QString::fromLatin1(reinterpret_cast<const char *>(value))
                         : QStringLiteral("<unavailable>");
        };
        const QString vendor = safeGlString(GL_VENDOR);
        const QString renderer = safeGlString(GL_RENDERER);
        const QString version = safeGlString(GL_VERSION);
        qInfo().noquote() << "OpenGL vendor:" << vendor;
        qInfo().noquote() << "OpenGL renderer:" << renderer;
        qInfo().noquote() << "OpenGL version:" << version;
        if (vendor == QStringLiteral("<unavailable>")
            || renderer == QStringLiteral("<unavailable>")) {
            m_initializationError = tr("无法创建有效的 OpenGL 上下文。请检查显卡驱动或 Qt OpenGL 部署。\n"
                                       "程序将保留界面，但暂时不绘制点云。");
            qWarning().noquote() << m_initializationError;
            return;
        }
        const QString rendererLower = renderer.toLower();
        if (rendererLower.contains(QStringLiteral("llvmpipe"))
            || rendererLower.contains(QStringLiteral("softpipe"))
            || rendererLower.contains(QStringLiteral("software"))
            || rendererLower.contains(QStringLiteral("gdi generic"))) {
            qWarning().noquote() << "OpenGL software renderer detected:" << renderer;
        }
        glClearColor(0.018f, 0.025f, 0.035f, 1.0f);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // Each vertex is emitted as a direct raster point.  No circle sprite,
        // antialiasing or per-point QPainter geometry is generated.
        glEnable(GL_PROGRAM_POINT_SIZE);

        static const char *vertexShader = R"(
            #version 330 core
            layout(location = 0) in vec3 vertexPosition;
            layout(location = 1) in vec3 vertexNormal;
            uniform mat4 transform;
            uniform vec3 cloudCenter;
            uniform float cloudSpan;
            uniform vec2 viewPan;
            uniform float pointSize;
            uniform float minHeight;
            uniform float maxHeight;
            uniform int colorMode;
            uniform float overlay;
            uniform int renderPass;
            layout(location = 2) in uint pointState;
            out float heightValue;
            out vec3 normalValue;
            flat out int stateValue;
            void main() {
                vec3 normalized = (vertexPosition - cloudCenter) * (2.0 / cloudSpan);
                gl_Position = transform * vec4(normalized, 1.0);
                gl_Position.xy += viewPan * gl_Position.w;
                gl_PointSize = renderPass == 0 ? pointSize : max(pointSize + 6.0, 8.0);
                normalValue = vertexNormal;
                stateValue = int(pointState);
                heightValue = clamp((vertexPosition.z - minHeight) /
                                    max(maxHeight - minHeight, 0.000001), 0.0, 1.0);
            }
        )";
        static const char *fragmentShader = R"(
            #version 330 core
            in float heightValue;
            in vec3 normalValue;
            uniform int colorMode;
            uniform float overlay;
            uniform int renderPass;
            out vec4 fragmentColor;
            flat in int stateValue;
            void main() {
                if (renderPass == 1 && distance(gl_PointCoord, vec2(0.5)) > 0.48)
                    discard;
                vec3 lowColor = vec3(0.05, 0.55, 0.95);
                vec3 middleColor = vec3(0.12, 0.88, 0.70);
                vec3 highColor = vec3(1.00, 0.68, 0.20);
                vec3 color = heightValue < 0.5
                    ? mix(lowColor, middleColor, heightValue * 2.0)
                    : mix(middleColor, highColor, (heightValue - 0.5) * 2.0);
                if (colorMode == 1) {
                    float gray = dot(color, vec3(0.299, 0.587, 0.114));
                    color = vec3(gray);
                } else if (colorMode == 2) {
                    color = normalize(abs(normalValue) + vec3(0.001));
                }
                vec3 normal = length(normalValue) > 0.0001
                    ? normalize(normalValue) : vec3(0.0, 0.0, 1.0);
                float light = 0.72 + 0.28 * max(dot(normal, normalize(vec3(0.35, 0.45, 0.82))), 0.0);
                if (renderPass == 1) color = vec3(1.0);
                else if (stateValue == 1) color = vec3(0.58);
                else if (stateValue == 2) color = vec3(1.0, 0.86, 0.18);
                fragmentColor = vec4(color * light, max(0.02, overlay));
            }
        )";

        static const char *pickingVertexShader = R"(
            #version 330 core
            layout(location = 0) in vec3 vertexPosition;
            uniform mat4 transform;
            uniform vec3 cloudCenter;
            uniform float cloudSpan;
            uniform vec2 viewPan;
            uniform float pointSize;
            flat out uint pointId;
            void main() {
                vec3 normalized = (vertexPosition - cloudCenter) * (2.0 / cloudSpan);
                gl_Position = transform * vec4(normalized, 1.0);
                gl_Position.xy += viewPan * gl_Position.w;
                gl_PointSize = pointSize;
                pointId = uint(gl_VertexID) + 1u;
            }
        )";
        static const char *pickingFragmentShader = R"(
            #version 330 core
            flat in uint pointId;
            out vec4 fragmentColor;
            void main() {
                fragmentColor = vec4(
                    float(pointId & 255u),
                    float((pointId >> 8u) & 255u),
                    float((pointId >> 16u) & 255u),
                    float((pointId >> 24u) & 255u)) / 255.0;
            }
        )";
        static const char *contourVertexShader = R"(
            #version 330 core
            layout(location = 0) in vec3 vertexPosition;
            uniform mat4 transform;
            uniform vec3 cloudCenter;
            uniform float cloudSpan;
            uniform vec2 viewPan;
            void main() {
                vec3 normalized = (vertexPosition - cloudCenter) * (2.0 / cloudSpan);
                gl_Position = transform * vec4(normalized, 1.0);
                gl_Position.xy += viewPan * gl_Position.w;
            }
        )";
        static const char *contourFragmentShader = R"(
            #version 330 core
            out vec4 fragmentColor;
            void main() { fragmentColor = vec4(1.0, 0.86, 0.18, 1.0); }
        )";

        if (!m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader)
            || !m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader)
            || !m_program.link()
            || !m_pickingProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, pickingVertexShader)
            || !m_pickingProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, pickingFragmentShader)
            || !m_pickingProgram.link()
            || !m_contourProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, contourVertexShader)
            || !m_contourProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, contourFragmentShader)
            || !m_contourProgram.link()) {
            m_initializationError = tr("OpenGL 着色器初始化失败\n主渲染：%1\n拾取：%2\n轮廓：%3")
                                        .arg(m_program.log(), m_pickingProgram.log(),
                                             m_contourProgram.log());
            qWarning().noquote() << m_initializationError;
            return;
        }

        if (!m_vertexArray.create() || !m_vertexBuffer.create() || !m_stateBuffer.create()
            || !m_contourVertexArray.create() || !m_contourBuffer.create()) {
            m_initializationError = tr("无法创建 OpenGL 顶点缓冲区");
            return;
        }
        m_uploadPending = true;
    }

    void paintGL() override {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (m_initializationError.isEmpty() && context() && context()->isValid()
            && !m_points.isEmpty()) {
            uploadCloudIfNeeded();
            if (m_uploadError.isEmpty()) {
                uploadStatesIfNeeded();
                uploadContoursIfNeeded();
                const QMatrix4x4 transform = viewTransform();
                m_program.bind();
                m_program.setUniformValue("transform", transform);
                m_program.setUniformValue("cloudCenter", m_center);
                m_program.setUniformValue("cloudSpan", m_span);
                m_program.setUniformValue("viewPan", QVector2D(m_pan));
                m_program.setUniformValue("pointSize", float(m_pointSize));
                const float lower = m_mapMin;
                const float upper = m_mapMax;
                m_program.setUniformValue("minHeight", lower);
                m_program.setUniformValue("maxHeight", upper);
                m_program.setUniformValue("colorMode", m_colorMode);
                m_program.setUniformValue("overlay", m_overlay);
                m_program.setUniformValue("renderPass", 0);
                m_vertexArray.bind();
                glDrawArrays(GL_POINTS, 0, int(m_points.size()));
                if (!m_selectedIndices.isEmpty()) {
                    glDepthFunc(GL_LEQUAL);
                    m_program.setUniformValue("renderPass", 1);
                    for (int index : m_selectedIndices) {
                        if (index >= 0 && index < m_points.size())
                            glDrawArrays(GL_POINTS, index, 1);
                    }
                    glDepthFunc(GL_LESS);
                }
                if (!m_selectedEdgeIndices.isEmpty()) {
                    glDepthFunc(GL_LEQUAL);
                    m_program.setUniformValue("renderPass", 1);
                    for (int index : m_selectedEdgeIndices) {
                        if (index >= 0 && index < m_points.size())
                            glDrawArrays(GL_POINTS, index, 1);
                    }
                    glDepthFunc(GL_LESS);
                }
                m_vertexArray.release();
                m_program.release();
                if (!m_contourRanges.isEmpty()) {
                    m_contourProgram.bind();
                    m_contourProgram.setUniformValue("transform", transform);
                    m_contourProgram.setUniformValue("cloudCenter", m_center);
                    m_contourProgram.setUniformValue("cloudSpan", m_span);
                    m_contourProgram.setUniformValue("viewPan", QVector2D(m_pan));
                    m_contourVertexArray.bind();
                    glDepthFunc(GL_LEQUAL);
                    glLineWidth(2.0f);
                    for (const auto &range : m_contourRanges)
                        glDrawArrays(GL_LINE_STRIP, range.first, range.second);
                    glLineWidth(1.0f);
                    glDepthFunc(GL_LESS);
                    m_contourVertexArray.release();
                    m_contourProgram.release();
                }
            }
        }

        drawOverlay();
    }

    void mousePressEvent(QMouseEvent *event) override {
        m_lastMousePosition = event->position();
        m_selectionOrigin = event->position();
        m_pressedButton = event->button();
        m_mouseMoved = false;
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        const QPointF delta = event->position() - m_lastMousePosition;
        if (std::abs(delta.x()) + std::abs(delta.y()) > 1.0) m_mouseMoved = true;
        m_lastMousePosition = event->position();
        if (m_pressedButton == Qt::LeftButton && !m_selectionMode && !m_edgeSelectionMode) {
            m_yaw += float(delta.x()) * 0.45f;
            // Do not clamp pitch at +/-89 degrees: a 2.5D plane can be
            // inspected from every orientation, including upside down.
            m_pitch = std::fmod(m_pitch + float(delta.y()) * 0.45f, 360.0f);
            if (m_pitch < 0.0f) m_pitch += 360.0f;
        } else if (m_pressedButton == Qt::RightButton) {
            m_pan += QPointF(2.0 * delta.x() / qMax(1, width()),
                             -2.0 * delta.y() / qMax(1, height()));
        }
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (m_edgeSelectionMode && event->button() == Qt::LeftButton && m_mouseMoved
            && edgeRectanglePicked) {
            edgeRectanglePicked(QRectF(m_selectionOrigin, event->position()).normalized());
        } else if (event->button() == Qt::LeftButton && !m_mouseMoved) {
            const int index = pickPoint(event->position());
            if (index >= 0) {
                m_coordinatePointIndex = index;
                update();
            }
            if (pointPicked) pointPicked(index);
        }
        m_pressedButton = Qt::NoButton;
    }

    void resizeGL(int width, int height) override {
        Q_UNUSED(width)
        Q_UNUSED(height)
        m_pickingFbo.reset();
    }

    void wheelEvent(QWheelEvent *event) override {
        m_zoom = qBound(0.08f,
                        m_zoom * (event->angleDelta().y() > 0 ? 1.15f : 0.87f),
                        30.0f);
        update();
    }

private:
    void updateBounds() {
        if (m_points.isEmpty()) {
            m_center = QVector3D();
            m_span = 1.0f;
            m_spanX = 0.0f;
            m_spanY = 0.0f;
            m_spanZ = 0.0f;
            m_minZ = 0.0f;
            m_maxZ = 1.0f;
            return;
        }

        float minX = m_points.first().x;
        float maxX = minX;
        float minY = m_points.first().y;
        float maxY = minY;
        m_minZ = m_points.first().z;
        m_maxZ = m_minZ;
        for (const pointcloud::Point3D &point : m_points) {
            minX = std::min(minX, point.x);
            maxX = std::max(maxX, point.x);
            minY = std::min(minY, point.y);
            maxY = std::max(maxY, point.y);
            m_minZ = std::min(m_minZ, point.z);
            m_maxZ = std::max(m_maxZ, point.z);
        }
        m_center = QVector3D((minX + maxX) * 0.5f,
                             (minY + maxY) * 0.5f,
                             (m_minZ + m_maxZ) * 0.5f);
        m_spanX = maxX - minX;
        m_spanY = maxY - minY;
        m_spanZ = m_maxZ - m_minZ;
        m_span = std::max({maxX - minX, maxY - minY, m_maxZ - m_minZ, 0.000001f});
    }

    void uploadCloudIfNeeded() {
        if (!m_uploadPending) return;
        if (m_points.size() > std::numeric_limits<int>::max() / qsizetype(sizeof(pointcloud::Point3D))) {
            m_uploadError = tr("点云过大，超过单个 OpenGL 缓冲区限制");
            qCritical() << "OpenGL upload rejected, points=" << m_points.size()
                        << "bytes="
                        << (m_points.size() * qsizetype(sizeof(pointcloud::Point3D)));
            m_uploadPending = false;
            return;
        }

        const qsizetype uploadBytes =
            m_points.size() * qsizetype(sizeof(pointcloud::Point3D));
        qInfo() << "OpenGL cloud upload started, points=" << m_points.size()
                << "bytes=" << uploadBytes;
        m_vertexArray.bind();
        m_vertexBuffer.bind();
        m_vertexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
        while (glGetError() != GL_NO_ERROR) {}
        m_vertexBuffer.allocate(m_points.constData(),
                                int(m_points.size() * qsizetype(sizeof(pointcloud::Point3D))));
        const GLenum uploadError = glGetError();
        if (uploadError != GL_NO_ERROR) {
            m_uploadError = uploadError == GL_OUT_OF_MEMORY
                ? tr("显存不足，无法全量上传当前点云")
                : tr("点云上传至显卡失败（OpenGL 错误 %1）").arg(uploadError);
            qCritical().noquote() << m_uploadError
                                  << "points=" << m_points.size()
                                  << "bytes=" << uploadBytes;
            m_uploadPending = false;
        } else {
            m_program.bind();
            m_program.enableAttributeArray(0);
            m_program.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(pointcloud::Point3D));
            m_program.enableAttributeArray(1);
            m_program.setAttributeBuffer(1, GL_FLOAT, int(3 * sizeof(float)), 3,
                                         sizeof(pointcloud::Point3D));
            m_stateBuffer.bind();
            m_stateBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
            m_stateBuffer.allocate(m_pointStates.constData(), int(m_pointStates.size()));
            glEnableVertexAttribArray(2);
            glVertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE, sizeof(quint8), nullptr);
            m_stateBuffer.release();
            m_program.release();
            m_uploadPending = false;
            m_stateUploadPending = false;
            qInfo() << "OpenGL cloud upload finished, points=" << m_points.size()
                    << "bytes=" << uploadBytes;
        }
        m_vertexBuffer.release();
        m_vertexArray.release();
    }

    void uploadStatesIfNeeded() {
        if (!m_stateUploadPending || !m_stateBuffer.isCreated()) return;
        m_vertexArray.bind();
        m_stateBuffer.bind();
        m_stateBuffer.allocate(m_pointStates.constData(), int(m_pointStates.size()));
        m_stateBuffer.release();
        m_vertexArray.release();
        m_stateUploadPending = false;
    }

    void uploadContoursIfNeeded() {
        if (!m_contourUploadPending || !m_contourBuffer.isCreated()) return;
        m_contourVertexArray.bind();
        m_contourBuffer.bind();
        m_contourBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        m_contourBuffer.allocate(m_contourVertices.constData(),
            int(m_contourVertices.size() * qsizetype(sizeof(pointcloud::Point3D))));
        m_contourProgram.bind();
        m_contourProgram.enableAttributeArray(0);
        m_contourProgram.setAttributeBuffer(0, GL_FLOAT, 0, 3,
                                             sizeof(pointcloud::Point3D));
        m_contourProgram.release();
        m_contourBuffer.release();
        m_contourVertexArray.release();
        m_contourUploadPending = false;
    }

    int pickPoint(const QPointF &position) {
        if (QThread::currentThread() != thread() || !m_initializationError.isEmpty()
            || m_points.isEmpty() || !context() || !isValid()) return -1;
        makeCurrent();
        if (QOpenGLContext::currentContext() != context()) return -1;
        uploadCloudIfNeeded();
        if (!m_uploadError.isEmpty()) { doneCurrent(); return -1; }
        const qreal dpr = devicePixelRatioF();
        const QSize pixelSize(qMax(1, qRound(width() * dpr)),
                              qMax(1, qRound(height() * dpr)));
        if (!m_pickingFbo || m_pickingFbo->size() != pixelSize) {
            QOpenGLFramebufferObjectFormat format;
            format.setAttachment(QOpenGLFramebufferObject::Depth);
            format.setInternalTextureFormat(GL_RGBA8);
            m_pickingFbo = std::make_unique<QOpenGLFramebufferObject>(pixelSize, format);
        }
        if (!m_pickingFbo || !m_pickingFbo->isValid()) { doneCurrent(); return -1; }
        m_pickingFbo->bind();
        glViewport(0, 0, pixelSize.width(), pixelSize.height());
        glDisable(GL_BLEND);
        glDisable(GL_DITHER);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_pickingProgram.bind();
        m_pickingProgram.setUniformValue("transform", viewTransform());
        m_pickingProgram.setUniformValue("cloudCenter", m_center);
        m_pickingProgram.setUniformValue("cloudSpan", m_span);
        m_pickingProgram.setUniformValue("viewPan", QVector2D(m_pan));
        m_pickingProgram.setUniformValue("pointSize", float(m_pointSize));
        m_vertexArray.bind();
        glDrawArrays(GL_POINTS, 0, int(m_points.size()));
        m_vertexArray.release();
        m_pickingProgram.release();

        const int centerX = qBound(0, int(std::floor(position.x() * dpr)),
                                   pixelSize.width() - 1);
        const int centerY = qBound(0, pixelSize.height() - 1
                                   - int(std::floor(position.y() * dpr)),
                                   pixelSize.height() - 1);
        quint8 pixels[3 * 3 * 4] = {};
        float depths[3 * 3] = {};
        const int x = qBound(0, centerX - 1, qMax(0, pixelSize.width() - 3));
        const int y = qBound(0, centerY - 1, qMax(0, pixelSize.height() - 3));
        const int readWidth = qMin(3, pixelSize.width());
        const int readHeight = qMin(3, pixelSize.height());
        glReadPixels(x, y, readWidth, readHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glReadPixels(x, y, readWidth, readHeight, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glViewport(0, 0, pixelSize.width(), pixelSize.height());
        glClearColor(0.018f, 0.025f, 0.035f, 1.0f);
        glEnable(GL_DITHER);
        glEnable(GL_BLEND);
        doneCurrent();
        const int preferredX = centerX - x, preferredY = centerY - y;
        auto decode = [&](int px, int py) -> quint32 {
            if (px < 0 || py < 0 || px >= readWidth || py >= readHeight) return 0;
            const quint8 *rgba = pixels + (py * readWidth + px) * 4;
            return quint32(rgba[0]) | (quint32(rgba[1]) << 8)
                | (quint32(rgba[2]) << 16) | (quint32(rgba[3]) << 24);
        };
        quint32 id = decode(preferredX, preferredY);
        if (id == 0) {
            int bestDistanceSquared = std::numeric_limits<int>::max();
            float bestDepth = 1.0f;
            for (int py = 0; py < readHeight; ++py) {
                for (int px = 0; px < readWidth; ++px) {
                    const quint32 candidate = decode(px, py);
                    if (candidate == 0) continue;
                    const int dx = px - preferredX;
                    const int dy = py - preferredY;
                    const int distanceSquared = dx * dx + dy * dy;
                    const float depth = depths[py * readWidth + px];
                    if (distanceSquared < bestDistanceSquared
                        || (distanceSquared == bestDistanceSquared && depth < bestDepth)) {
                        id = candidate;
                        bestDistanceSquared = distanceSquared;
                        bestDepth = depth;
                    }
                }
            }
        }
        return id > 0 && id <= quint32(m_points.size()) ? int(id - 1) : -1;
    }

    QVector<int> pickRectangle(const QRectF &selection) {
        QVector<int> result;
        if (QThread::currentThread() != thread() || !m_initializationError.isEmpty()
            || m_points.isEmpty() || !context() || !isValid()) return result;
        makeCurrent();
        if (QOpenGLContext::currentContext() != context()) return result;
        uploadCloudIfNeeded();
        if (!m_uploadError.isEmpty()) { doneCurrent(); return result; }
        const qreal dpr = devicePixelRatioF();
        const QSize pixelSize(qMax(1, qRound(width() * dpr)), qMax(1, qRound(height() * dpr)));
        if (!m_pickingFbo || m_pickingFbo->size() != pixelSize) {
            QOpenGLFramebufferObjectFormat format;
            format.setAttachment(QOpenGLFramebufferObject::Depth);
            format.setInternalTextureFormat(GL_RGBA8);
            m_pickingFbo = std::make_unique<QOpenGLFramebufferObject>(pixelSize, format);
        }
        if (!m_pickingFbo || !m_pickingFbo->isValid()) { doneCurrent(); return result; }
        const QRectF clipped = selection.intersected(rect());
        const int x0 = qBound(0, qFloor(clipped.left() * dpr), pixelSize.width() - 1);
        const int x1 = qBound(0, qCeil(clipped.right() * dpr), pixelSize.width() - 1);
        const int top = qBound(0, qFloor(clipped.top() * dpr), pixelSize.height() - 1);
        const int bottom = qBound(0, qCeil(clipped.bottom() * dpr), pixelSize.height() - 1);
        const int readWidth = qMax(1, x1 - x0 + 1);
        const int readHeight = qMax(1, bottom - top + 1);
        const int y0 = pixelSize.height() - 1 - bottom;
        m_pickingFbo->bind();
        glViewport(0, 0, pixelSize.width(), pixelSize.height());
        glDisable(GL_BLEND); glDisable(GL_DITHER); glEnable(GL_DEPTH_TEST);
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_pickingProgram.bind();
        m_pickingProgram.setUniformValue("transform", viewTransform());
        m_pickingProgram.setUniformValue("cloudCenter", m_center);
        m_pickingProgram.setUniformValue("cloudSpan", m_span);
        m_pickingProgram.setUniformValue("viewPan", QVector2D(m_pan));
        m_pickingProgram.setUniformValue("pointSize", float(m_pointSize));
        m_vertexArray.bind(); glDrawArrays(GL_POINTS, 0, int(m_points.size()));
        m_vertexArray.release(); m_pickingProgram.release();
        QByteArray pixels(readWidth * readHeight * 4, Qt::Uninitialized);
        glReadPixels(x0, y0, readWidth, readHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glViewport(0, 0, pixelSize.width(), pixelSize.height());
        glEnable(GL_DITHER); glEnable(GL_BLEND); doneCurrent();
        QSet<int> unique;
        for (int i = 0; i < readWidth * readHeight; ++i) {
            const uchar *rgba = reinterpret_cast<const uchar *>(pixels.constData() + i * 4);
            const quint32 id = quint32(rgba[0]) | (quint32(rgba[1]) << 8)
                | (quint32(rgba[2]) << 16) | (quint32(rgba[3]) << 24);
            if (id > 0 && id <= quint32(m_points.size())) unique.insert(int(id - 1));
        }
        result = unique.values().toVector();
        std::sort(result.begin(), result.end());
        return result;
    }

    QMatrix4x4 rotationTransform() const {
        QMatrix4x4 rotation;
        rotation.rotate(m_pitch, 1.0f, 0.0f, 0.0f);
        rotation.rotate(m_yaw, 0.0f, 0.0f, 1.0f);
        return rotation;
    }

    QMatrix4x4 viewTransform() const {
        const float aspect = float(qMax(1, width())) / float(qMax(1, height()));
        QMatrix4x4 projection;
        if (aspect >= 1.0f)
            projection.ortho(-aspect / m_zoom, aspect / m_zoom,
                             -1.0f / m_zoom, 1.0f / m_zoom, -4.0f, 4.0f);
        else
            projection.ortho(-1.0f / m_zoom, 1.0f / m_zoom,
                             -1.0f / (aspect * m_zoom), 1.0f / (aspect * m_zoom),
                             -4.0f, 4.0f);
        return projection * rotationTransform();
    }

    void drawOverlay() {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (m_edgeSelectionMode && m_mouseMoved && m_pressedButton == Qt::LeftButton) {
            painter.setPen(QPen(QColor(90, 210, 255), 1.5, Qt::DashLine));
            painter.setBrush(QColor(90, 210, 255, 35));
            painter.drawRect(QRectF(m_selectionOrigin, m_lastMousePosition).normalized());
        }
        painter.setPen(QColor(231, 237, 246));
        QFont titleFont = painter.font();
        titleFont.setPointSize(11);
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.drawText(18, 27, tr("3D 点云"));

        painter.setFont(QFont());
        painter.setPen(QColor(150, 164, 184));
        const QString pointText = m_points.isEmpty()
            ? tr("未加载数据")
            : tr("工作点数  %1  |  全量绘制")
                  .arg(QLocale().toString(m_points.size()));
        painter.drawText(18, 49, pointText);

        if (!m_points.isEmpty()) {
            const QString centerText = tr("中心 (mm)  X %1  Y %2  Z %3")
                .arg(QString::number(m_center.x(), 'f', 2))
                .arg(QString::number(m_center.y(), 'f', 2))
                .arg(QString::number(m_center.z(), 'f', 2));
            const QString extentText = tr("范围 (mm)  %1 × %2 × %3")
                .arg(QString::number(m_spanX, 'f', 2))
                .arg(QString::number(m_spanY, 'f', 2))
                .arg(QString::number(m_spanZ, 'f', 2));
            painter.setPen(QColor(196, 207, 222));
            const int right = width() - 18;
            int top = 27;
            if (m_coordinatePointIndex >= 0 && m_coordinatePointIndex < m_points.size()) {
                const pointcloud::Point3D &point = m_points[m_coordinatePointIndex];
                const QString positionText = tr("点 #%1  X %2  Y %3  Z %4 mm")
                    .arg(m_coordinatePointIndex)
                    .arg(QString::number(point.x, 'f', 3))
                    .arg(QString::number(point.y, 'f', 3))
                    .arg(QString::number(point.z, 'f', 3));
                const float normalLength = std::sqrt(point.nx * point.nx
                    + point.ny * point.ny + point.nz * point.nz);
                QString attitudeText;
                if (normalLength > 1.0e-6f && std::isfinite(normalLength)) {
                    const float nx = point.nx / normalLength;
                    const float ny = point.ny / normalLength;
                    const float nz = point.nz / normalLength;
                    const float a = qRadiansToDegrees(std::atan2(ny, nz));
                    const float b = qRadiansToDegrees(std::atan2(-nx, std::hypot(ny, nz)));
                    attitudeText = tr("法向姿态  A %1°  B %2°  C 0.00°")
                        .arg(QString::number(a, 'f', 2))
                        .arg(QString::number(b, 'f', 2));
                } else {
                    attitudeText = tr("法向姿态  A --  B --  C --");
                }
                painter.setPen(QColor(239, 244, 250));
                painter.drawText(right - painter.fontMetrics().horizontalAdvance(positionText),
                                 top, positionText);
                painter.setPen(QColor(107, 205, 255));
                painter.drawText(right - painter.fontMetrics().horizontalAdvance(attitudeText),
                                 top + 22, attitudeText);
                top += 44;
            }
            painter.setPen(QColor(196, 207, 222));
            painter.drawText(right - painter.fontMetrics().horizontalAdvance(centerText),
                             top, centerText);
            painter.setPen(QColor(145, 160, 180));
            painter.drawText(right - painter.fontMetrics().horizontalAdvance(extentText),
                             top + 22, extentText);
        }

        if (m_workpieceCoordinate.valid && m_span > 1.0e-9f) {
            const auto project = [this](const QVector3D &world) {
                const QVector3D normalized = (world - m_center) * (2.0f / m_span);
                QVector4D clip = viewTransform() * QVector4D(normalized, 1.0f);
                clip.setX(clip.x() + float(m_pan.x()) * clip.w());
                clip.setY(clip.y() + float(m_pan.y()) * clip.w());
                if (std::abs(clip.w()) > 1.0e-9f) clip /= clip.w();
                return QPointF((clip.x() * 0.5f + 0.5f) * width(),
                               (0.5f - clip.y() * 0.5f) * height());
            };
            const float axisLength = qMax(1.0f, m_span * 0.14f);
            const QPointF origin = project(m_workpieceCoordinate.originInRobotBase);
            const auto drawFrameAxis = [&](const QVector3D &axis, const QColor &color,
                                           const QString &label) {
                const QPointF end = project(m_workpieceCoordinate.originInRobotBase
                                            + axis * axisLength);
                painter.setPen(QPen(color, 2.5));
                painter.drawLine(origin, end);
                painter.setBrush(color);
                painter.drawEllipse(end, 3.0, 3.0);
                painter.drawText(end + QPointF(5.0, -4.0), label);
            };
            painter.setBrush(QColor(245, 245, 245));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(origin, 4.5, 4.5);
            drawFrameAxis(m_workpieceCoordinate.axisXInRobotBase,
                          QColor(244, 92, 92), tr("工件 X"));
            drawFrameAxis(m_workpieceCoordinate.axisYInRobotBase,
                          QColor(78, 218, 132), tr("工件 Y"));
            drawFrameAxis(m_workpieceCoordinate.axisZInRobotBase,
                          QColor(76, 156, 255), tr("工件 Z"));
        }

        const QString displayError = m_initializationError.isEmpty()
            ? m_uploadError : m_initializationError;
        if (!displayError.isEmpty()) {
            painter.setPen(QColor(255, 125, 115));
            painter.drawText(rect().adjusted(40, 40, -40, -40),
                             Qt::AlignCenter | Qt::TextWordWrap,
                             tr("点云显示失败\n%1").arg(displayError));
            return;
        }
        if (m_points.isEmpty()) {
            painter.setPen(QColor(125, 141, 162));
            painter.drawText(rect(), Qt::AlignCenter, tr("打开 PLY 文件开始预览"));
        }

        const QMatrix4x4 rotation = rotationTransform();
        const QPointF origin(58.0, height() - 52.0);
        const float length = 34.0f;
        const auto drawAxis = [&](const QVector4D &axis, const QColor &color, const QString &label) {
            const QVector4D rotated = rotation * axis;
            const QPointF end = origin + QPointF(rotated.x() * length, -rotated.y() * length);
            QPen pen(color, 2.0);
            painter.setPen(pen);
            painter.drawLine(origin, end);
            painter.drawText(end + QPointF(4.0, -2.0), label);
        };
        drawAxis(QVector4D(1, 0, 0, 0), QColor(244, 92, 92), QStringLiteral("X"));
        drawAxis(QVector4D(0, 1, 0, 0), QColor(78, 218, 132), QStringLiteral("Y"));
        drawAxis(QVector4D(0, 0, 1, 0), QColor(76, 156, 255), QStringLiteral("Z"));
    }

    QVector<pointcloud::Point3D> m_points;
    int m_coordinatePointIndex = -1;
    pointcloud::WorkpieceCoordinateSystem m_workpieceCoordinate;
    QVector<quint8> m_pointStates;
    QVector<int> m_selectedIndices;
    QVector<int> m_edgeIndices;
    QVector<int> m_selectedEdgeIndices;
    QVector<int> m_planeResultIndices;
    QVector<int> m_edgeResultIndices;
    QVector<pointcloud::Point3D> m_contourVertices;
    QVector<QPair<int, int>> m_contourRanges;
    QOpenGLShaderProgram m_program;
    QOpenGLShaderProgram m_pickingProgram;
    QOpenGLShaderProgram m_contourProgram;
    QOpenGLVertexArrayObject m_vertexArray;
    QOpenGLVertexArrayObject m_contourVertexArray;
    QOpenGLBuffer m_vertexBuffer;
    QOpenGLBuffer m_stateBuffer;
    QOpenGLBuffer m_contourBuffer;
    std::unique_ptr<QOpenGLFramebufferObject> m_pickingFbo;
    QVector3D m_center;
    QPointF m_lastMousePosition;
    QPointF m_selectionOrigin;
    QPointF m_pan;
    Qt::MouseButton m_pressedButton = Qt::NoButton;
    QString m_initializationError;
    QString m_uploadError;
    float m_span = 1.0f;
    float m_spanX = 0.0f;
    float m_spanY = 0.0f;
    float m_spanZ = 0.0f;
    float m_minZ = 0.0f;
    float m_maxZ = 1.0f;
    float m_yaw = 38.0f;
    float m_pitch = -28.0f;
    float m_zoom = 1.0f;
    int m_pointSize = 1;
    int m_colorMode = 0;
    float m_overlay = 1.0f;
    float m_mapMin = 0.0f;
    float m_mapMax = 1.0f;
    bool m_uploadPending = false;
    bool m_stateUploadPending = false;
    bool m_contourUploadPending = false;
    bool m_selectionMode = false;
    bool m_edgeSelectionMode = false;
    bool m_mouseMoved = false;
};

MainWindow::MainWindow(QWidget *parent) : MainWindow(pcv::config::PointCloudViewDefaults{}, parent) {}

MainWindow::MainWindow(const pcv::config::PointCloudViewDefaults &defaults, QWidget *parent)
    : QMainWindow(parent), m_defaults(defaults) {
    buildUi();
}

MainWindow::~MainWindow() {
    // Stop every completion signal before waiting.  QObject children are
    // destroyed after MainWindow members, so allowing a queued watcher slot
    // to run during teardown can access vectors/widgets whose lifetime has
    // already ended.
    m_closing = true;
    if (m_loadWatcher) disconnect(m_loadWatcher, nullptr, this, nullptr);
    if (m_noiseWatcher) disconnect(m_noiseWatcher, nullptr, this, nullptr);
    if (m_threePlaneWatcher) disconnect(m_threePlaneWatcher, nullptr, this, nullptr);
    if (m_edgeWatcher) disconnect(m_edgeWatcher, nullptr, this, nullptr);
    if (m_planeImageWatcher) disconnect(m_planeImageWatcher, nullptr, this, nullptr);
    if (m_loadWatcher && m_loadWatcher->isRunning()) {
        m_loadWatcher->waitForFinished();
    }
    if (m_noiseWatcher && m_noiseWatcher->isRunning()) {
        m_noiseWatcher->waitForFinished();
    }
    if (m_threePlaneWatcher && m_threePlaneWatcher->isRunning()) {
        m_threePlaneWatcher->waitForFinished();
    }
    if (m_edgeWatcher && m_edgeWatcher->isRunning()) {
        m_edgeWatcher->waitForFinished();
    }
    if (m_planeImageWatcher && m_planeImageWatcher->isRunning()) {
        m_planeImageWatcher->waitForFinished();
    }
    if (m_canvas) {
        m_canvas->setUpdatesEnabled(false);
        m_canvas->hide();
    }
    delete ui;
}

void MainWindow::buildUi() {
    ui = new Ui::MainWindow;
    ui->setupUi(this);
    m_fileList = ui->lw_files;
    m_fileInfo = ui->lbl_subtitle1;
    m_canvasInfo = ui->lbl_subtitle2;
    m_progress = ui->pbar_progress;
    m_pointSize = ui->spb_point_size;
    m_colorMode = ui->cb_color_mode;
    m_overlay = ui->dsb_overlay;
    m_mapMin = ui->dsb_map_min;
    m_mapMax = ui->dsb_map_max;
    m_voxelNoise = ui->chk_voxel_noise;
    m_voxelSize = ui->dsb_voxel_size;
    m_statisticalNoise = ui->chk_statistical_noise;
    m_meanK = ui->spb_mean_k;
    m_stddev = ui->dsb_stddev;
    m_noiseApply = ui->btn_apply_noise;
    m_pickPointsButton = ui->btn_pick_points;
    m_abandonPointsButton = ui->btn_abandon_points;
    m_undoPointButton = ui->btn_undo_point;
    m_pickSecondPlaneButton = ui->btn_pick_second_plane;
    m_cancelSecondPlaneButton = ui->btn_cancel_second_plane;
    m_determinePlaneButton = ui->btn_determine_plane;
    m_confirmCandidateButton = ui->btn_confirm_candidate;
    m_cancelCandidateButton = ui->btn_cancel_candidate;
    m_threeOutput = ui->pte_plane_output;
    m_edgeGridSize = ui->dsb_edge_grid;
    m_planeImageWidth = ui->dsb_plane_image_width;
    m_planeImageHeight = ui->dsb_plane_image_height;
    // Image framing is now derived from the extracted workpiece bounds. Keep
    // the Designer controls visible for compatibility, but prevent manual
    // dimensions from changing the export contract.
    if (m_planeImageWidth) m_planeImageWidth->setEnabled(false);
    if (m_planeImageHeight) m_planeImageHeight->setEnabled(false);
    m_edgeCloseRadius = ui->spb_edge_close;
    m_edgeOpenRadius = ui->spb_edge_open;
    m_edgeApplyButton = ui->btn_apply_edge;
    m_selectEdgeButton = ui->btn_select_edge;
    m_clearEdgeSelectionButton = ui->btn_clear_edge;
    m_extractPlaneImageButton = ui->btn_extract_plane_image;
    m_savePlaneImageButton = ui->btn_save_plane_image;
    m_finalizeTempWorkpieceButton = ui->btn_finalize_temp_workpiece;
    m_planeImagePreview = ui->lbl_plane_image_preview;
    m_edgeOutput = ui->pte_edge_output;
    applyConfiguredDefaults();
    connect(ui->btn_pick_axes, &QPushButton::clicked,
            this, &MainWindow::startWorkpieceAxisSelection);
    connect(ui->btn_clear_axes, &QPushButton::clicked,
            this, &MainWindow::clearWorkpieceAxisSelection);

    m_canvas = new PointCloudCanvas(ui->wgt_canvas_host);
    auto *canvasLayout = new QVBoxLayout(ui->wgt_canvas_host);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->addWidget(m_canvas);
    connect(ui->act_open, &QAction::triggered, this, &MainWindow::openPointCloudSource);
    connect(ui->act_exit, &QAction::triggered, qApp, &QApplication::quit);
    connect(ui->btn_open_point_cloud, &QPushButton::clicked, this, &MainWindow::openPointCloudSource);
    connect(ui->btn_open_scanning_info, &QPushButton::clicked,
            this, &MainWindow::openTempScanningInfo);
    connect(ui->btn_multiframe_registration, &QPushButton::clicked,
            this, &MainWindow::openMultiFrameRegistration);
    connect(m_fileList, &QListWidget::currentRowChanged, this, &MainWindow::loadSelectedSource);
    connect(m_pointSize, qOverload<int>(&QSpinBox::valueChanged), m_canvas, &PointCloudCanvas::setPointSize);
    connect(ui->btn_reset_view, &QPushButton::clicked, m_canvas, &PointCloudCanvas::resetView);
    connect(m_colorMode, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::updateRenderSettings);
    connect(m_overlay, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MainWindow::updateRenderSettings);
    connect(m_mapMin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MainWindow::updateRenderSettings);
    connect(m_mapMax, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MainWindow::updateRenderSettings);
    connect(m_noiseApply, &QPushButton::clicked, this, &MainWindow::applyNoiseRemoval);
    connect(ui->btn_restore_cloud, &QPushButton::clicked, this, [this]() {
        if (pointTaskRunning()) { statusBar()->showMessage(tr("点云处理任务正在运行")); return; }
        publishCanvasCache(m_rawPoints); statusBar()->showMessage(tr("已恢复原始点云"));
    });
    connect(m_pickPointsButton, &QPushButton::clicked, this, &MainWindow::startPlanePointSelection);
    connect(m_abandonPointsButton, &QPushButton::clicked, this, &MainWindow::abandonPlanePointSelection);
    connect(m_undoPointButton, &QPushButton::clicked, this, &MainWindow::undoPlanePointSelection);
    connect(m_determinePlaneButton, &QPushButton::clicked, this, &MainWindow::determinePlaneCandidate);
    connect(m_confirmCandidateButton, &QPushButton::clicked, this, &MainWindow::confirmPlaneCandidate);
    connect(m_cancelCandidateButton, &QPushButton::clicked, this, &MainWindow::cancelPlaneCandidate);
    connect(m_pickSecondPlaneButton, &QPushButton::clicked,
            this, &MainWindow::startSecondPlanePointSelection);
    connect(m_cancelSecondPlaneButton, &QPushButton::clicked,
            this, &MainWindow::cancelSecondPlanePointSelection);
    auto *cancelThreeAction = new QAction(this);
    cancelThreeAction->setShortcut(QKeySequence(Qt::Key_Escape));
    cancelThreeAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(cancelThreeAction, &QAction::triggered, this, &MainWindow::abandonPlanePointSelection);
    addAction(cancelThreeAction);
    auto *undoThreeAction = new QAction(this);
    undoThreeAction->setShortcut(QKeySequence(Qt::Key_Backspace));
    undoThreeAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(undoThreeAction, &QAction::triggered, this, &MainWindow::undoPlanePointSelection);
    addAction(undoThreeAction);
    connect(m_edgeApplyButton, &QPushButton::clicked, this, &MainWindow::applyPlaneEdgeSegmentation);
    connect(m_selectEdgeButton, &QPushButton::clicked, this, &MainWindow::startEdgePointSelection);
    connect(m_clearEdgeSelectionButton, &QPushButton::clicked, this, &MainWindow::clearEdgePointSelection);
    connect(m_extractPlaneImageButton, &QPushButton::clicked, this, &MainWindow::extractPlaneImage);
    connect(m_savePlaneImageButton, &QPushButton::clicked, this, &MainWindow::savePlaneImage);
    connect(m_finalizeTempWorkpieceButton, &QPushButton::clicked,
            this, &MainWindow::finalizeTempWorkpiece);
    ui->splitter_main->setStretchFactor(0, 0);
    ui->splitter_main->setStretchFactor(1, 1);
    ui->splitter_main->setStretchFactor(2, 0);
    ui->splitter_main->setSizes({245, 850, 290});
    m_progress->setFixedHeight(3);
    m_progress->hide();
    m_canvas->pointPicked = [this](int index) {
        if (m_edgeSelectionActive) handleCanvasEdgePointPicked(index);
        else handleCanvasPointPicked(index);
    };
    m_canvas->edgeRectanglePicked = [this](const QRectF &rect) {
        if (!m_edgeSelectionActive || pointTaskRunning()) return;
        const QVector<int> picked = m_canvas->pickRectangleForSelection(rect);
        for (int index : picked)
            if (m_planeEdgeResult.edgeIndices.contains(index) && !m_selectedEdgeIndices.contains(index))
                m_selectedEdgeIndices.push_back(index);
        m_canvas->setSelectedEdgeIndices(m_selectedEdgeIndices);
        updatePlaneEdgeUi();
        statusBar()->showMessage(m_selectedEdgeIndices.isEmpty()
            ? tr("框选区域没有黄色边缘点") : tr("框选边缘点：%1").arg(m_selectedEdgeIndices.size()));
    };
    updatePlaneExtractionUi();
    updatePlaneEdgeUi();
    statusBar()->showMessage(tr("就绪"));
}

void MainWindow::applyConfiguredDefaults() {
    m_pointSize->setValue(m_defaults.pointSize);
    m_colorMode->setCurrentIndex(m_defaults.colorMode);
    m_overlay->setValue(m_defaults.overlay);
    m_voxelNoise->setChecked(m_defaults.voxelEnabled);
    m_voxelSize->setValue(m_defaults.voxelSizeMm);
    m_statisticalNoise->setChecked(m_defaults.statisticalEnabled);
    m_meanK->setValue(m_defaults.meanK);
    m_stddev->setValue(m_defaults.stddevMultiplier);
    m_edgeGridSize->setValue(m_defaults.edgeGridSizeMm);
    m_edgeCloseRadius->setValue(m_defaults.edgeCloseRadius);
    m_edgeOpenRadius->setValue(m_defaults.edgeOpenRadius);
}

void MainWindow::openPointCloudSource() {
    if (pointTaskRunning()) {
        statusBar()->showMessage(tr("点云处理任务正在运行"));
        return;
    }
    QMessageBox dialog(QMessageBox::Question, tr("打开点云"),
                       tr("请选择点云来源"), QMessageBox::Cancel, this);
    QPushButton *fileButton = dialog.addButton(tr("打开单个 PLY"), QMessageBox::AcceptRole);
    QPushButton *folderButton = dialog.addButton(tr("扫描文件夹 PLY"), QMessageBox::AcceptRole);
    dialog.exec();
    if (dialog.clickedButton() == fileButton) openPointCloud();
    else if (dialog.clickedButton() == folderButton) {
        const QString directory = QFileDialog::getExistingDirectory(
            this, tr("选择包含 PLY 的文件夹"));
        if (directory.isEmpty()) return;
        QDir dir(directory);
        const QStringList files = dir.entryList({QStringLiteral("*.ply"), QStringLiteral("*.PLY")},
                                                 QDir::Files, QDir::Name);
        if (files.isEmpty()) {
            QMessageBox::information(this, tr("没有 PLY 文件"),
                                     tr("所选文件夹中没有 .ply 文件"));
            return;
        }
        m_sourceDirectory = directory;
        m_sourceFiles.clear();
        for (const QString &name : files) m_sourceFiles.push_back(dir.absoluteFilePath(name));
        m_folderScanOnly = true;
        {
            // Populate and select the list without relying on currentRowChanged
            // to start the first load. selectAll() may already change the
            // current index, so a later setCurrentRow(0) is not guaranteed to
            // emit the signal and previously left the center canvas empty.
            const QSignalBlocker blocker(m_fileList);
            m_fileList->clear();
            m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
            for (const QString &path : m_sourceFiles)
                m_fileList->addItem(QFileInfo(path).fileName());
            m_fileList->selectAll();
            m_fileList->setCurrentRow(0);
        }
        m_fileInfo->setText(tr("文件夹扫描完成\nPLY 文件数  %1\n请在左侧列表中选择要显示的文件。")
                                .arg(m_sourceFiles.size()));
        statusBar()->showMessage(tr("已扫描 %1 个 PLY，正在显示第一个文件").arg(m_sourceFiles.size()));
        loadSelectedSource();
    }
}

void MainWindow::loadSelectedSource() {
    const int row = m_fileList ? m_fileList->currentRow() : -1;
    if (row < 0 || row >= m_sourceFiles.size() || pointTaskRunning()) return;
    m_pendingPath = m_sourceFiles[row];
    m_loading = true;
    m_progress->show(); m_progress->setRange(0, 0);
    statusBar()->showMessage(tr("正在加载 %1...").arg(QFileInfo(m_pendingPath).fileName()));
    if (!m_loadWatcher) {
        m_loadWatcher = new QFutureWatcher<pointcloud::LoadResult>(this);
        connect(m_loadWatcher, &QFutureWatcher<pointcloud::LoadResult>::finished,
                this, &MainWindow::loadFinished);
    }
    const QString path = m_pendingPath;
    // The worker owns only the immutable path.  It must not retain or inspect
    // a QPointer/MainWindow while the GUI can be closing.
    m_loadWatcher->setFuture(QtConcurrent::run([path]() {
        return pointcloud::loadPlyResult(path);
    }));
}

void MainWindow::openPointCloud() {
    if (pointTaskRunning()) {
        statusBar()->showMessage(tr("点云处理任务正在运行"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开 PLY"), QString(), tr("PLY 文件 (*.ply);;所有文件 (*.*)"));
    if (path.isEmpty()) return;

    // A direct single-file open starts a new data-source session. Folder
    // scans keep their complete source list for quick preview switching.
    m_sourceFiles = {path};
    m_sourceDirectory = QFileInfo(path).absolutePath();

    m_loading = true;
    m_progress->show();
    m_progress->setRange(0, 0);
    statusBar()->showMessage(tr("正在加载 PLY..."));
    m_pendingPath = path;
    if (!m_loadWatcher) {
        m_loadWatcher = new QFutureWatcher<pointcloud::LoadResult>(this);
        connect(m_loadWatcher, &QFutureWatcher<pointcloud::LoadResult>::finished,
                this, &MainWindow::loadFinished);
    }
    m_loadWatcher->setFuture(QtConcurrent::run([path]() {
        return pointcloud::loadPlyResult(path);
    }));
}

void MainWindow::loadFinished() {
    // Transfer the result out of QFuture exactly once.  Copying the result
    // leaves a second owner of a multi-million-point QVector inside the
    // watcher until shutdown and was the source of the Debug heap assertion.
    pointcloud::LoadResult result = m_loadWatcher->future().takeResult();
    if (m_closing) {
        qWarning() << "PLY load result discarded because the window is closing, points="
                   << result.points.size() << "path=" << m_pendingPath;
        m_loading = false;
        return;
    }
    if (!result.ok) {
        m_loading = false;
        m_progress->hide();
        QMessageBox::critical(this, tr("打开失败"), result.error);
        statusBar()->showMessage(tr("加载失败"));
        return;
    }
    if (result.points.isEmpty()) {
        m_loading = false;
        m_progress->hide();
        QMessageBox::warning(this, tr("打开失败"), tr("PLY 文件不包含可显示的顶点"));
        statusBar()->showMessage(tr("加载失败：点云为空"));
        return;
    }
    qInfo() << "PLY load baseline: path=" << m_pendingPath
            << "points=" << result.points.size()
            << "header_ms=" << result.headerElapsedMs
            << "boundary_scan_ms=" << result.boundaryScanElapsedMs
            << "parse_ms=" << result.parseElapsedMs
            << "reader_total_ms=" << result.totalElapsedMs;
    m_rawPoints = std::move(result.points);
    const QFileInfo fileInfo(m_pendingPath);
    const bool keepFolderSources = m_folderScanOnly && m_sourceFiles.size() > 1
        && m_sourceFiles.contains(m_pendingPath);
    if (!keepFolderSources) {
        m_sourceFiles = {m_pendingPath};
        m_sourceDirectory = fileInfo.absolutePath();
    }
    float minZ = result.hasBounds ? result.minimum.z : m_rawPoints.first().z;
    float maxZ = result.hasBounds ? result.maximum.z : minZ;
    m_mapMin->setValue(minZ);
    m_mapMax->setValue(maxZ > minZ ? maxZ : minZ + 1.0);
    publishCanvasCache(m_rawPoints);
    qInfo() << "PLY display publication baseline: main_points=" << m_points.size()
            << "raw_points=" << m_rawPoints.size();
    if (!keepFolderSources) {
        m_sourceFiles = {m_pendingPath};
        m_sourceDirectory = fileInfo.absolutePath();
        m_fileList->clear();
        m_fileList->addItem(fileInfo.fileName());
        m_fileList->setCurrentRow(0);
    }
    m_fileInfo->setText(tr("%1 MB\n原始点数  %2")
                            .arg(fileInfo.size() / 1048576.0, 0, 'f', 1)
                            .arg(QLocale().toString(m_rawPoints.size())));
    m_canvasInfo->setText(tr("当前显示 %1 个点  ·  原始 %2 个点  ·  直接像素标记")
                              .arg(QLocale().toString(m_points.size()))
                              .arg(QLocale().toString(m_rawPoints.size())));
    m_progress->setRange(0, 100);
    m_progress->setValue(100);
    m_progress->hide();
    m_loading = false;
    statusBar()->showMessage(tr("加载完成"));
}

void MainWindow::updateRenderSettings() {
    if (!m_canvas) return;
    m_canvas->setDisplayOptions(m_colorMode->currentIndex(), m_overlay->value(),
                                m_mapMin->value(), m_mapMax->value());
}

void MainWindow::applyDepthRenderRange(const QVector<pointcloud::Point3D> &points) {
    if (!m_canvas || !m_colorMode || !m_mapMin || !m_mapMax) return;
    float minimum = std::numeric_limits<float>::max();
    float maximum = std::numeric_limits<float>::lowest();
    bool hasFiniteDepth = false;
    for (const pointcloud::Point3D &point : points) {
        if (!std::isfinite(point.z)) continue;
        minimum = qMin(minimum, point.z);
        maximum = qMax(maximum, point.z);
        hasFiniteDepth = true;
    }
    if (!hasFiniteDepth) {
        qWarning() << "JSON depth rendering skipped: no finite robot_base.z values";
        return;
    }
    if (!(maximum > minimum)) maximum = minimum + 1.0f;

    // Keep the JSON workflow deterministic: the transformed robot_base Z
    // range drives the height colormap and is also reflected in the controls.
    const QSignalBlocker colorBlocker(m_colorMode);
    const QSignalBlocker minimumBlocker(m_mapMin);
    const QSignalBlocker maximumBlocker(m_mapMax);
    m_colorMode->setCurrentIndex(0);
    m_mapMin->setValue(double(minimum));
    m_mapMax->setValue(double(maximum));
    if (m_mapMax->value() <= m_mapMin->value())
        m_mapMax->setValue(m_mapMin->value() + 1.0);
    updateRenderSettings();
    qInfo() << "JSON depth rendering range: robot_base.z" << minimum << maximum;
}

void MainWindow::publishCanvasCache(QVector<pointcloud::Point3D> points,
                                    bool preserveTempWorkpieceSession) {
    const qsizetype incomingCount = points.size();

    // No GUI or OpenGL state may be touched once closeEvent() has started.
    // A QFutureWatcher completion can already be queued when the window closes.
    if (m_closing) {
        qWarning() << "Display cache publication discarded during shutdown, points="
                   << incomingCount;
        return;
    }

    // The CPU display cache is the authoritative input for every downstream
    // operation. A temporarily unavailable OpenGL widget must never discard a
    // successfully loaded cloud or leave the UI reporting zero displayed
    // points while m_rawPoints is populated.
    if (!preserveTempWorkpieceSession && m_tempWorkpieceSessionActive) {
        m_tempWorkpieceSessionActive = false;
        m_tempWorkpieceSourceIndices.clear();
        m_tempWorkpieceOutputDirectory.clear();
        m_tempWorkpieceCreatedAtIso8601.clear();
        m_tempWorkpieceScanId.clear();
    }
    m_points = std::move(points);
    ++m_canvasRevision;
    qInfo() << "Main display cache published, points=" << m_points.size()
            << "incoming=" << incomingCount
            << "canvas=" << static_cast<const void *>(m_canvas)
            << "closing=" << m_closing;
    m_selectedPointIndices.clear();
    resetSecondPlaneVerification();
    m_axisSelectionActive = false;
    m_xAxisPointIndex = m_yAxisPointIndex = -1;
    m_automaticAxisPointSelection = {};
    m_planeCenter = {};
    m_planeCenterValid = false;
    m_threePlaneResult = {};
    m_workpieceCoordinate = {};
    m_threePointSelectionActive = false;
    m_planeCandidateConfirmed = false;
    m_planeFinalizationPending = false;
    if (m_canvas) {
        m_canvas->setSelectionMode(false);
        // setCloud() clears every derived point-state layer. Do not call
        m_canvas->setCloud(m_points);
    } else {
        qCritical() << "Main display cache is ready but PointCloudCanvas is null";
    }
    if (m_threeOutput) m_threeOutput->clear();
    clearPlaneEdgeUi();
    updatePlaneExtractionUi();
    updatePlaneEdgeUi();
    if (m_canvasInfo) {
        m_canvasInfo->setText(tr("当前显示 %1 个点  ·  原始 %2 个点  ·  全量直接标记")
                                  .arg(QLocale().toString(m_points.size()))
                                  .arg(QLocale().toString(m_rawPoints.size())));
    }
}

bool MainWindow::pointTaskRunning() const {
    return m_loading || (m_noiseWatcher && m_noiseWatcher->isRunning())
        || (m_tempWorkpieceWatcher && m_tempWorkpieceWatcher->isRunning())
        || (m_tempWorkpieceFinalizeWatcher && m_tempWorkpieceFinalizeWatcher->isRunning())
        || (m_threePlaneWatcher && m_threePlaneWatcher->isRunning())
        || (m_edgeWatcher && m_edgeWatcher->isRunning())
        || (m_planeImageWatcher && m_planeImageWatcher->isRunning());
}

void MainWindow::resetSecondPlaneVerification() {
    m_secondPlanePointIndices.clear();
    m_secondPlaneSelectionActive = false;
    m_secondPlaneValidated = false;
    m_secondPlaneSamePlane = false;
    m_secondPlaneNormalAngle = 0.0f;
    m_secondPlaneMaximumDistance = 0.0f;
}

void MainWindow::startPlanePointSelection() {
    if (pointTaskRunning()) {
        statusBar()->showMessage(tr("点云处理任务正在运行"));
        return;
    }
    if (m_points.isEmpty()) {
        statusBar()->showMessage(tr("请先加载点云"));
        return;
    }
    m_selectedPointIndices.clear();
    resetSecondPlaneVerification();
    m_axisSelectionActive = false;
    m_xAxisPointIndex = m_yAxisPointIndex = -1;
    m_automaticAxisPointSelection = {};
    m_planeCenter = {};
    m_planeCenterValid = false;
    m_threePlaneResult = {};
    m_workpieceCoordinate = {};
    ++m_coordinateFrameRevision;
    clearPlaneEdgeUi();
    m_planeCandidateConfirmed = false;
    m_planeFinalizationPending = false;
    m_threePointSelectionActive = true;
    m_canvas->clearPlaneResult();
    m_canvas->clearWorkpieceCoordinateSystem();
    m_canvas->setSelectedIndices({});
    m_canvas->setSelectionMode(true);
    updatePlaneExtractionUi();
    updatePlaneEdgeUi();
    statusBar()->showMessage(tr("请选择第 1 个平面控制点（至少 3 个）"));
}

void MainWindow::abandonPlanePointSelection() {
    if (m_threePlaneWatcher && m_threePlaneWatcher->isRunning()) {
        statusBar()->showMessage(tr("平面提取正在运行"));
        return;
    }
    m_selectedPointIndices.clear();
    resetSecondPlaneVerification();
    m_threePlaneResult = {};
    m_planeCenter = {};
    m_planeCenterValid = false;
    m_automaticAxisPointSelection = {};
    m_workpieceCoordinate = {};
    ++m_coordinateFrameRevision;
    clearPlaneEdgeUi();
    m_planeCandidateConfirmed = false;
    m_planeFinalizationPending = false;
    m_threePointSelectionActive = false;
    if (m_canvas) {
        m_canvas->setSelectionMode(false);
        m_canvas->setSelectedIndices({});
        m_canvas->clearPlaneResult();
        m_canvas->clearWorkpieceCoordinateSystem();
    }
    if (m_threeOutput) m_threeOutput->clear();
    updatePlaneExtractionUi();
    updatePlaneEdgeUi();
    statusBar()->showMessage(tr("已放弃取点"));
}

void MainWindow::undoPlanePointSelection() {
    if (pointTaskRunning()) {
        statusBar()->showMessage(tr("点云处理任务正在运行"));
        return;
    }
    if (m_selectedPointIndices.isEmpty()) return;
    m_selectedPointIndices.removeLast();
    resetSecondPlaneVerification();
    m_axisSelectionActive = false;
    m_xAxisPointIndex = m_yAxisPointIndex = -1;
    m_automaticAxisPointSelection = {};
    m_planeCenter = {};
    m_planeCenterValid = false;
    m_threePlaneResult = {};
    m_workpieceCoordinate = {};
    ++m_coordinateFrameRevision;
    clearPlaneEdgeUi();
    m_planeCandidateConfirmed = false;
    m_planeFinalizationPending = false;
    m_threePointSelectionActive = true;
    m_canvas->clearPlaneResult();
    m_canvas->clearWorkpieceCoordinateSystem();
    m_canvas->setSelectedIndices(m_selectedPointIndices);
    m_canvas->setSelectionMode(true);
    updatePlaneExtractionUi();
    updatePlaneEdgeUi();
    statusBar()->showMessage(tr("请选择第 %1 个平面控制点").arg(m_selectedPointIndices.size() + 1));
}

void MainWindow::updatePlaneExtractionUi() {
    const bool running = m_threePlaneWatcher && m_threePlaneWatcher->isRunning();
    const bool hasMinimumPlanePoints = m_selectedPointIndices.size() >= 3;
    const bool hasCandidate = m_threePlaneResult.ok;
    const bool verificationPassed = m_secondPlaneValidated && m_secondPlaneSamePlane;
    if (m_pickPointsButton) m_pickPointsButton->setEnabled(!running && !m_points.isEmpty());
    if (m_abandonPointsButton)
        m_abandonPointsButton->setEnabled(!running && !m_selectedPointIndices.isEmpty());
    if (m_undoPointButton)
        m_undoPointButton->setEnabled(!running && !m_selectedPointIndices.isEmpty());
    if (m_determinePlaneButton)
        m_determinePlaneButton->setEnabled(!running && hasMinimumPlanePoints && !hasCandidate);
    if (m_confirmCandidateButton)
        m_confirmCandidateButton->setEnabled(!running && hasCandidate
                                             && (m_planeFinalizationPending
                                                 || verificationPassed)
                                             && !m_planeCandidateConfirmed);
    if (m_cancelCandidateButton)
        m_cancelCandidateButton->setEnabled(!running && hasCandidate);
    if (m_pickSecondPlaneButton)
        m_pickSecondPlaneButton->setEnabled(!running && hasCandidate
                                            && !m_planeFinalizationPending
                                            && !m_planeCandidateConfirmed);
    if (m_cancelSecondPlaneButton)
        m_cancelSecondPlaneButton->setEnabled(!running && m_secondPlaneSelectionActive);
    if (ui->btn_pick_axes) {
        ui->btn_pick_axes->setEnabled(false);
        ui->btn_pick_axes->setVisible(false);
    }
    if (ui->btn_clear_axes)
        ui->btn_clear_axes->setEnabled(false);
    if (ui->btn_clear_axes)
        ui->btn_clear_axes->setVisible(false);
    if (!m_threeOutput || hasCandidate) return;
    QStringList lines;
    lines << tr("第一组平面控制点：%1 个").arg(m_selectedPointIndices.size());
    for (int i = 0; i < m_selectedPointIndices.size(); ++i) {
        const int index = m_selectedPointIndices[i];
        if (index < 0 || index >= m_points.size()) continue;
        const auto &p = m_points[index];
        lines << tr("控制点 P%1 [%2]  (%3, %4, %5)")
                     .arg(i + 1).arg(index)
                     .arg(p.x, 0, 'g', 8).arg(p.y, 0, 'g', 8).arg(p.z, 0, 'g', 8);
    }
    if (m_threePointSelectionActive && m_selectedPointIndices.size() < 3)
        lines << QString() << tr("请选择第 %1 个平面控制点").arg(m_selectedPointIndices.size() + 1);
    else if (hasMinimumPlanePoints)
        lines << QString() << tr("已选择 %1 个平面控制点，可继续取点或点击“确定平面”")
                     .arg(m_selectedPointIndices.size());
    m_threeOutput->setPlainText(lines.join(QLatin1Char('\n')));
}


void MainWindow::updatePlaneEdgeUi() {
    const bool running = m_edgeWatcher && m_edgeWatcher->isRunning();
    const bool imageRunning = m_planeImageWatcher && m_planeImageWatcher->isRunning();
    const bool planeReady = m_planeCandidateConfirmed && m_threePlaneResult.ok;
    if (m_edgeApplyButton)
        m_edgeApplyButton->setEnabled(!running && planeReady);
    if (m_selectEdgeButton)
        m_selectEdgeButton->setEnabled(!running && m_planeEdgeResult.ok
                                       && !m_planeEdgeResult.edgeIndices.isEmpty());
    if (m_clearEdgeSelectionButton)
        m_clearEdgeSelectionButton->setEnabled(!running && !m_selectedEdgeIndices.isEmpty());
    if (m_extractPlaneImageButton)
        m_extractPlaneImageButton->setEnabled(!running && !imageRunning
                                               && planeReady);
    if (m_savePlaneImageButton)
        m_savePlaneImageButton->setEnabled(planeImageSaveReady());
    if (m_finalizeTempWorkpieceButton) {
        QString finalizeError;
        const bool visible = m_tempWorkpieceSessionActive;
        m_finalizeTempWorkpieceButton->setVisible(visible);
        m_finalizeTempWorkpieceButton->setEnabled(
            visible && tempWorkpieceFinalizeReady(&finalizeError));
        m_finalizeTempWorkpieceButton->setToolTip(finalizeError);
    }
}

void MainWindow::openMultiFrameRegistration()
{
    if (pointTaskRunning()) {
        statusBar()->showMessage(tr("点云处理任务正在运行"));
        return;
    }
    RegistrationDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted || !dialog.hasSuccessfulResult()) return;

    QVector<pointcloud::Point3D> points = dialog.takePoints();
    if (points.isEmpty()) {
        QMessageBox::critical(this, tr("多帧配准失败"), tr("正式输出不包含可发布的点云"));
        return;
    }
    m_rawPoints = std::move(points);
    m_pendingPath = dialog.outputPly();
    m_sourceFiles = {m_pendingPath};
    m_sourceDirectory = QFileInfo(m_pendingPath).absolutePath();
    m_folderScanOnly = false;
    {
        const QSignalBlocker blocker(m_fileList);
        m_fileList->clear();
        m_fileList->addItem(QFileInfo(m_pendingPath).fileName());
        m_fileList->setCurrentRow(0);
    }
    publishCanvasCache(m_rawPoints);
    applyDepthRenderRange(m_points);
    m_fileInfo->setText(tr("多帧配准\n正式 PLY\n%1\n点数  %2")
                            .arg(m_pendingPath)
                            .arg(QLocale().toString(m_rawPoints.size())));
    statusBar()->showMessage(tr("多帧配准结果已发布到当前画布"));
}

void MainWindow::openTempScanningInfo() {
    if (pointTaskRunning()) {
        statusBar()->showMessage(tr("点云处理任务正在运行"));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开扫描 JSON"), QString(),
        tr("扫描 JSON (*.json);;所有文件 (*.*)"));
    if (path.isEmpty()) return;

    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    pcv::interface::TempScanningInfo scanInfo;
    QString parseError;
    if (!pcv::interface::parseTempScanningInfo(absolutePath, &scanInfo, &parseError)) {
        QMessageBox::warning(this, tr("扫描 JSON 无效"), parseError);
        return;
    }

    const QString jsonDirectory = QFileInfo(absolutePath).absolutePath();
    const auto displayPath = [jsonDirectory](const QString &input) {
        const QFileInfo inputInfo(input);
        return QDir::cleanPath(inputInfo.isAbsolute()
            ? inputInfo.absoluteFilePath()
            : QFileInfo(QDir(jsonDirectory).filePath(input)).absoluteFilePath());
    };
    const auto poseText = [](const pointcloud::RobotPose &pose) {
        return QStringLiteral("[%1, %2, %3, %4, %5, %6]")
            .arg(pose.x, 0, 'g', 12).arg(pose.y, 0, 'g', 12).arg(pose.z, 0, 'g', 12)
            .arg(pose.rz, 0, 'g', 12).arg(pose.ry, 0, 'g', 12).arg(pose.rx, 0, 'g', 12);
    };
    const QString layoutName = QStringLiteral("LineProfileXz");
    QDialog inputDialog(this);
    inputDialog.setWindowTitle(tr("临时扫描坐标转换"));
    inputDialog.setModal(true);
    auto *form = new QFormLayout(&inputDialog);
    auto *jsonValue = new QLabel(absolutePath, &inputDialog);
    auto *plyValue = new QLabel(displayPath(scanInfo.pointCloudFile), &inputDialog);
    auto *xmlValue = new QLabel(displayPath(scanInfo.calibrationFile), &inputDialog);
    for (QLabel *label : {jsonValue, plyValue, xmlValue}) {
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setWordWrap(true);
    }
    form->addRow(tr("输入 JSON"), jsonValue);
    form->addRow(tr("点云 PLY"), plyValue);
    form->addRow(tr("手眼 XML"), xmlValue);
    form->addRow(tr("坐标方向"), new QLabel(
        QStringLiteral("%1 -> %2").arg(scanInfo.calibrationSourceFrame,
                                        scanInfo.calibrationTargetFrame), &inputDialog));
    form->addRow(tr("点云布局"), new QLabel(layoutName, &inputDialog));
    form->addRow(tr("Start 位姿"), new QLabel(poseText(scanInfo.robotPoseStart), &inputDialog));
    form->addRow(tr("End 位姿"), new QLabel(poseText(scanInfo.robotPoseEnd), &inputDialog));
    if (!scanInfo.warning.isEmpty()) {
        auto *warning = new QLabel(tr("提示：%1").arg(scanInfo.warning), &inputDialog);
        warning->setWordWrap(true);
        warning->setStyleSheet(QStringLiteral("color:#a06000;"));
        form->addRow(tr("输入提示"), warning);
    }

    QString calibrationOverridePath;
    auto *changeXml = new QPushButton(tr("更换 XML..."), &inputDialog);
    form->addRow(QString(), changeXml);
    connect(changeXml, &QPushButton::clicked, &inputDialog, [&]() {
        const QString selected = QFileDialog::getOpenFileName(
            &inputDialog, tr("选择手眼标定 XML"), xmlValue->text(),
            tr("XML 文件 (*.xml);;所有文件 (*.*)"));
        if (selected.isEmpty()) return;
        calibrationOverridePath = QFileInfo(selected).absoluteFilePath();
        xmlValue->setText(calibrationOverridePath);
    });

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &inputDialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("执行转换"));
    connect(buttons, &QDialogButtonBox::accepted, &inputDialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &inputDialog, &QDialog::reject);
    form->addRow(buttons);
    inputDialog.resize(760, inputDialog.sizeHint().height());
    if (inputDialog.exec() != QDialog::Accepted) {
        statusBar()->showMessage(tr("已取消扫描 JSON 转换"));
        return;
    }

    pcv::interface::TempWorkpieceOptions options;
    options.scanningInfoPath = absolutePath;
    options.calibrationOverridePath = calibrationOverridePath;

    if (!m_tempWorkpieceWatcher) {
        m_tempWorkpieceWatcher =
            new QFutureWatcher<pcv::interface::TempWorkpiecePreparation>(this);
        connect(m_tempWorkpieceWatcher,
                &QFutureWatcher<pcv::interface::TempWorkpiecePreparation>::finished,
                this, &MainWindow::tempWorkpieceFinished);
    }

    m_progress->show();
    m_progress->setRange(0, 0);
    statusBar()->showMessage(tr("正在处理扫描 JSON..."));
    const auto capturedOptions = options;
    m_tempWorkpieceWatcher->setFuture(QtConcurrent::run([capturedOptions]() {
        QString workerError;
        return pcv::interface::prepareTempWorkpiece(capturedOptions, &workerError);
    }));
}

void MainWindow::tempWorkpieceFinished() {
    pcv::interface::TempWorkpiecePreparation result =
        m_tempWorkpieceWatcher->future().takeResult();
    if (m_closing) {
        qWarning() << "Temp workpiece result discarded because the window is closing";
        return;
    }

    m_progress->hide();
    if (!result.success) {
        const QString detail = result.errorCode.isEmpty()
            ? result.message
            : QStringLiteral("%1: %2").arg(result.errorCode, result.message);
        const bool inputFailure = result.errorCode == QStringLiteral("PCV_INPUT_001")
            || result.errorCode == QStringLiteral("PCV_INPUT_002")
            || result.errorCode == QStringLiteral("PCV_CONTRACT_001");
        if (inputFailure)
            QMessageBox::warning(this, tr("扫描 JSON 处理失败"), detail);
        else
            QMessageBox::critical(this, tr("扫描 JSON 处理失败"), detail);
        statusBar()->showMessage(detail);
        return;
    }

    const QString layoutName = QStringLiteral("LineProfileXz");
    m_rawPoints = result.robotBasePoints;
    m_tempWorkpieceSourceIndices = std::move(result.sourceIndices);
    m_tempWorkpieceScanId = result.scanId;
    m_tempWorkpieceOutputDirectory = result.outputDirectory;
    m_tempWorkpieceCreatedAtIso8601 = result.createdAtIso8601;
    m_pendingPath = result.resolvedPointCloudFile;
    m_sourceFiles = {m_pendingPath};
    m_sourceDirectory = QFileInfo(m_pendingPath).absolutePath();
    publishCanvasCache(std::move(result.robotBasePoints), true);
    applyDepthRenderRange(m_points);
    m_tempWorkpieceSessionActive = true;
    m_tempWorkpieceSessionRevision = m_canvasRevision;
    ui->tw_main->setCurrentWidget(ui->wgt_plane);
    startPlanePointSelection();
    m_fileInfo->setText(
        tr("扫描 JSON\nscan_id  %1\nlayout  %2\n转换点数  %3\nPLY\n%4\nXML\n%5\n后续输出目录（本阶段未写入）\n%6\n请在平面提取页人工选择 P1、P2、P3")
            .arg(result.scanId, layoutName)
            .arg(QLocale().toString(result.convertedPointCount))
            .arg(QFileInfo(result.resolvedPointCloudFile).absoluteFilePath())
            .arg(QFileInfo(result.resolvedCalibrationFile).absoluteFilePath())
            .arg(QFileInfo(result.outputDirectory).absoluteFilePath()));
    statusBar()->showMessage(
        tr("扫描 JSON 已转换并发布到当前画布：%1，已生成 %2 个点；请依次人工选择 P1、P2、P3")
            .arg(layoutName)
            .arg(QLocale().toString(result.convertedPointCount)));
    if (!result.warning.isEmpty())
        statusBar()->showMessage(statusBar()->currentMessage() + QStringLiteral("；") + result.warning);
}

bool MainWindow::planeImageSaveReady(QString *error) const {
    auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (pointTaskRunning()) return fail(QStringLiteral("边缘分割或平面图像任务仍在运行，请稍候"));
    if (!m_planeImageResult.ok) return fail(QStringLiteral("当前没有有效的平面 2D 图像"));
    if (m_planeImageResult.image.isNull() || m_planeImageResult.image.width() <= 0
        || m_planeImageResult.image.height() <= 0)
        return fail(QStringLiteral("平面 2D 图像为空或尺寸无效"));
    if (m_planeImageResult.image.format() != QImage::Format_Grayscale8)
        return fail(QStringLiteral("平面 2D 图像必须是 Grayscale8 单通道格式"));
    if (!m_threePlaneResult.ok) return fail(QStringLiteral("当前没有有效的拟合平面"));
    if (m_threePlaneResult.planeIndices.isEmpty())
        return fail(QStringLiteral("当前平面没有可保存的点索引"));
    if (!m_workpieceCoordinate.valid)
        return fail(QStringLiteral("请先确定有效的工件坐标系"));
    if (m_planeImageInputRevision != m_canvasRevision)
        return fail(QStringLiteral("当前图像对应的画布版本已失效，请重新提取"));
    if (m_planeImageCoordinateRevision != m_coordinateFrameRevision)
        return fail(QStringLiteral("当前图像对应的工件坐标系版本已失效，请重新提取"));
    for (int index : m_threePlaneResult.planeIndices) {
        if (index < 0 || index >= m_points.size())
            return fail(QStringLiteral("当前平面索引已超出画布点云范围，请重新提取"));
    }
    return true;
}

bool MainWindow::tempWorkpieceFinalizeReady(QString *error) const {
    auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (!m_tempWorkpieceSessionActive)
        return fail(QStringLiteral("当前不是临时扫描会话"));
    if (m_tempWorkpieceSessionRevision != m_canvasRevision)
        return fail(QStringLiteral("临时扫描会话已过期，请重新打开扫描 JSON"));
    if (!m_secondPlaneValidated || !m_secondPlaneSamePlane)
        return fail(QStringLiteral("请先通过第二组三点平面验证"));
    if (!m_planeCandidateConfirmed || !m_workpieceCoordinate.valid)
        return fail(QStringLiteral("请先确定有效的 WObj1 坐标系"));
    if (m_tempWorkpieceSourceIndices.size() != m_points.size())
        return fail(QStringLiteral("临时扫描 source index 与当前画布不一致"));
    if (m_tempWorkpieceOutputDirectory.trimmed().isEmpty())
        return fail(QStringLiteral("临时工件输出目录无效"));
    QString imageError;
    if (!planeImageSaveReady(&imageError)) return fail(imageError);
    if (!m_planeImageResult.edgeMask)
        return fail(QStringLiteral("临时工件输出必须使用边缘分割后的平面 Mask"));
    if (m_planeImageResult.occupiedCellCount <= 0)
        return fail(QStringLiteral("边缘 Mask 没有有效前景像素，无法提交临时工件"));
    if (m_planeImageResult.roiIndices.size() < 3)
        return fail(QStringLiteral("最终矩形 ROI 内没有足够的真实平面点"));
    for (const int index : m_planeImageResult.roiIndices) {
        if (index < 0 || index >= m_points.size())
            return fail(QStringLiteral("当前 ROI 索引已超出画布点云范围，请重新提取"));
    }
    return true;
}

void MainWindow::clearPlaneEdgeUi() {
    m_planeEdgeResult = {};
    m_planeImageResult = {};
    m_edgeSelectionActive = false;
    m_selectedEdgeIndices.clear();
    if (m_canvas) {
        m_canvas->setEdgeSelectionMode(false, {});
        m_canvas->setSelectedEdgeIndices({});
    }
    if (m_edgeOutput) m_edgeOutput->clear();
    if (m_planeImagePreview) {
        m_planeImagePreview->setPixmap({});
        m_planeImagePreview->setText(tr("确认平面后执行边缘分割"));
    }
    updatePlaneEdgeUi();
}

void MainWindow::extractPlaneImage() {
    if (pointTaskRunning() || !m_planeCandidateConfirmed || !m_threePlaneResult.ok) return;
    pointcloud::PlaneEdgeOptions options;
    options.edgeGridSize = float(m_edgeGridSize->value());
    options.maximumEdgeGridCells = 4000000;
    options.useImageFrame = m_planeCandidateConfirmed && m_workpieceCoordinate.valid;
    // Always derive the export rectangle from the extracted plane bounds.
    // The workpiece frame supplies axes/origin only; legacy manual crop sizes
    // must not alter the PNG after edge segmentation.
    options.autoImageBounds = true;
    options.imageMargin = float(m_defaults.imageMarginMm);
    options.imagePixelSize = float(m_defaults.imagePixelSizeMm);
    options.imageRoundIncrement = float(m_defaults.imageRoundIncrementMm);
    options.maximumImagePixels = m_defaults.maximumImagePixels;
    if (options.useImageFrame) {
        options.imageOrigin = m_workpieceCoordinate.originInRobotBase;
        options.imageAxisU = m_workpieceCoordinate.axisXInRobotBase;
        options.imageAxisV = m_workpieceCoordinate.axisYInRobotBase;
        options.imageCropWidth = float(m_planeImageWidth->value());
        options.imageCropHeight = float(m_planeImageHeight->value());
    }
    const QVector<pointcloud::Point3D> source = m_points;
    const QVector<int> planeIndices = m_threePlaneResult.planeIndices;
    const pointcloud::PlaneModel model = m_threePlaneResult.model;
    m_planeImageInputRevision = m_canvasRevision;
    m_planeImageCoordinateRevision = m_coordinateFrameRevision;
    m_extractPlaneImageButton->setEnabled(false);
    m_edgeOutput->setPlainText(tr("正在提取平面 2D 图像..."));
    if (!m_planeImageWatcher) {
        m_planeImageWatcher = new QFutureWatcher<pointcloud::PlaneImageResult>(this);
        connect(m_planeImageWatcher, &QFutureWatcher<pointcloud::PlaneImageResult>::finished,
                this, &MainWindow::planeImageExtractionFinished);
    }
    m_planeImageWatcher->setFuture(QtConcurrent::run([source, planeIndices, model, options]() {
        return pointcloud::extractPlaneImage(source, planeIndices, model, options);
    }));
}

void MainWindow::planeImageExtractionFinished() {
    const pointcloud::PlaneImageResult result = m_planeImageWatcher->result();
    if (m_planeImageInputRevision != m_canvasRevision
        || m_planeImageCoordinateRevision != m_coordinateFrameRevision) {
        m_planeImageResult = {};
        m_planeImagePreview->setPixmap({});
        m_planeImagePreview->setText(tr("画布或工件坐标系已变化，请重新生成平面图像"));
        m_edgeOutput->setPlainText(tr("画布缓存已变化，旧平面图像已丢弃。"));
        updatePlaneEdgeUi();
        return;
    }
    m_planeImageResult = result;
    if (!result.ok) {
        m_planeImagePreview->setPixmap({});
        m_planeImagePreview->setText(result.error);
        m_edgeOutput->setPlainText(result.error);
        updatePlaneEdgeUi();
        return;
    }
    m_planeImagePreview->setPixmap(QPixmap::fromImage(result.image).scaled(
        m_planeImagePreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_edgeOutput->setPlainText(tr("平面 2D 图像已提取\n尺寸：%1 × %2 px\n栅格尺寸：%3 mm\n占用像素：%4\n"
                                 "工件坐标映射点：%5\n矩形 ROI 点：%6\n平面外拒绝：%7\n矩形外裁剪：%8\nMask 类型：%9")
        .arg(result.image.width()).arg(result.image.height())
        .arg(result.gridSize, 0, 'g', 6).arg(QLocale().toString(result.occupiedCellCount))
        .arg(QLocale().toString(result.mappedPlanePointCount))
        .arg(QLocale().toString(result.roiIndices.size()))
        .arg(QLocale().toString(result.rejectedNonPlanePointCount))
        .arg(QLocale().toString(result.rejectedOutsideRectangleCount))
        .arg(result.edgeMask ? tr("边缘 Mask") : tr("填充平面 Mask")));
    updatePlaneEdgeUi();
    if (m_tempWorkpieceSessionActive && result.edgeMask) {
        statusBar()->showMessage(tr("边缘 PNG 已生成，正在提交临时工件四件套"));
        finalizeTempWorkpiece();
    }
}

void MainWindow::finalizeTempWorkpiece() {
    QString validationError;
    if (!tempWorkpieceFinalizeReady(&validationError)) {
        statusBar()->showMessage(validationError);
        return;
    }
    const QStringList outputNames{
        QStringLiteral("baseline_robot_base.ply"),
        QStringLiteral("roi_template_robot_base.ply"),
        QStringLiteral("plane_mask.png"),
        QStringLiteral("temp_workpiece_info.json")};
    QStringList existingOutputs;
    for (const QString &name : outputNames) {
        if (QFileInfo::exists(QDir(m_tempWorkpieceOutputDirectory).filePath(name)))
            existingOutputs.push_back(name);
    }
    if (!existingOutputs.isEmpty()) {
        const QString question = tr("输出目录中已存在以下正式文件：\n%1\n\n是否覆盖？")
            .arg(existingOutputs.join(QStringLiteral("\n")));
        if (QMessageBox::question(this, tr("确认覆盖正式输出"), question,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            statusBar()->showMessage(tr("已取消覆盖，人工确认结果仍保留在当前会话"));
            return;
        }
    }

    const QVector<int> roiIndices = m_planeImageResult.roiIndices;

    pcv::interface::TempWorkpiecePreparation prepared;
    prepared.success = true;
    prepared.scanId = m_tempWorkpieceScanId;
    prepared.resolvedPointCloudFile = m_pendingPath;
    prepared.createdAtIso8601 = m_tempWorkpieceCreatedAtIso8601;
    prepared.robotBasePoints = m_points;
    prepared.sourceIndices = m_tempWorkpieceSourceIndices;
    pcv::interface::TempWorkpieceFinalizeOptions options;
    options.outputDirectory = m_tempWorkpieceOutputDirectory;
    options.allowOverwrite = true;
    options.planeIndices = m_threePlaneResult.planeIndices;
    options.roiIndices = roiIndices;
    options.planeMask = m_planeImageResult.image;
    options.planeModel = QVector4D(m_threePlaneResult.model.a, m_threePlaneResult.model.b,
                                   m_threePlaneResult.model.c, m_threePlaneResult.model.d);
    options.rmsErrorMm = m_threePlaneResult.rmsError;
    options.planeDistanceToleranceMm = m_planeImageResult.usedPlaneDistanceTolerance;
    options.originInRobotBase = m_workpieceCoordinate.originInRobotBase;
    options.axisXInRobotBase = m_workpieceCoordinate.axisXInRobotBase;
    options.axisYInRobotBase = m_workpieceCoordinate.axisYInRobotBase;
    options.axisZInRobotBase = m_workpieceCoordinate.axisZInRobotBase;
    options.abcDeg = QVector3D(m_workpieceCoordinate.poseA, m_workpieceCoordinate.poseB,
                               m_workpieceCoordinate.poseC);
    options.TBaseWorkpiece = m_workpieceCoordinate.workpieceToRobotBase;
    options.TWorkpieceBase = m_workpieceCoordinate.robotBaseToWorkpiece;
    if (!m_tempWorkpieceFinalizeWatcher) {
        m_tempWorkpieceFinalizeWatcher = new QFutureWatcher<pcv::interface::TempWorkpieceResult>(this);
        connect(m_tempWorkpieceFinalizeWatcher,
                &QFutureWatcher<pcv::interface::TempWorkpieceResult>::finished,
                this, &MainWindow::tempWorkpieceFinalizeFinished);
    }
    m_progress->show();
    m_progress->setRange(0, 0);
    statusBar()->showMessage(tr("正在事务性提交临时工件四件套..."));
    updatePlaneEdgeUi();
    m_tempWorkpieceFinalizeWatcher->setFuture(QtConcurrent::run([prepared, options]() {
        QString workerError;
        return pcv::interface::finalizeTempWorkpiece(prepared, options, &workerError);
    }));
}

void MainWindow::tempWorkpieceFinalizeFinished() {
    const pcv::interface::TempWorkpieceResult result =
        m_tempWorkpieceFinalizeWatcher->future().takeResult();
    if (m_closing) return;
    m_progress->hide();
    if (!result.success) {
        const QString detail = result.errorCode.isEmpty()
            ? result.message : QStringLiteral("%1: %2").arg(result.errorCode, result.message);
        QMessageBox::critical(this, tr("临时工件输出失败"), detail);
        statusBar()->showMessage(detail);
        updatePlaneEdgeUi();
        return;
    }
    m_tempWorkpieceSessionActive = false;
    m_tempWorkpieceSessionRevision = 0;
    m_tempWorkpieceSourceIndices.clear();
    m_tempWorkpieceScanId.clear();
    m_tempWorkpieceOutputDirectory.clear();
    m_tempWorkpieceCreatedAtIso8601.clear();
    updatePlaneExtractionUi();
    updatePlaneEdgeUi();
    statusBar()->showMessage(tr("临时工件四件套已提交：%1\n%2\n%3\n%4")
        .arg(result.tempWorkpieceInfoPath, result.baselineRobotBasePly,
             result.roiTemplateRobotBasePly, result.planeMaskPng));
}

void MainWindow::handleCanvasPointPicked(int index) {
    if (m_secondPlaneSelectionActive) {
        if (index < 0) {
            statusBar()->showMessage(tr("鼠标位置没有可选点"));
            return;
        }
        if (m_secondPlanePointIndices.contains(index)
            || m_selectedPointIndices.contains(index)) {
            statusBar()->showMessage(tr("第二组三点不能与已有选点重复"));
            return;
        }
        m_secondPlanePointIndices.push_back(index);
        QVector<int> marked = m_selectedPointIndices;
        marked += m_secondPlanePointIndices;
        m_canvas->setSelectedIndices(marked);
        if (m_secondPlanePointIndices.size() < 3) {
            statusBar()->showMessage(tr("请选择第二组三点中的第 %1 个点")
                                     .arg(m_secondPlanePointIndices.size() + 1));
            return;
        }
        m_secondPlaneSelectionActive = false;
        m_canvas->setSelectionMode(false);
        validateSecondPlaneSelection();
        return;
    }
    if (!m_threePointSelectionActive || pointTaskRunning()) return;
    if (index < 0) {
        statusBar()->showMessage(tr("鼠标位置没有可选点"));
        return;
    }
    if (m_selectedPointIndices.contains(index)) {
        statusBar()->showMessage(tr("该点已经被选择"));
        return;
    }
    m_selectedPointIndices.push_back(index);
    m_canvas->setSelectedIndices(m_selectedPointIndices);
    updatePlaneExtractionUi();
    if (m_selectedPointIndices.size() < 3) {
        statusBar()->showMessage(tr("请选择第 %1 个点").arg(m_selectedPointIndices.size() + 1));
        return;
    }

    updatePlaneExtractionUi();
    statusBar()->showMessage(tr("已选择 %1 个平面控制点，可继续取点或点击“确定平面”")
                             .arg(m_selectedPointIndices.size()));
}

void MainWindow::determinePlaneCandidate() {
    runPlaneExtraction(false);
}

void MainWindow::startSecondPlanePointSelection() {
    if (pointTaskRunning() || !m_threePlaneResult.ok || m_planeCandidateConfirmed) {
        statusBar()->showMessage(tr("请先生成尚未确定的第一平面候选结果"));
        return;
    }
    resetSecondPlaneVerification();
    m_axisSelectionActive = false;
    m_xAxisPointIndex = m_yAxisPointIndex = -1;
    m_workpieceCoordinate = {};
    ++m_coordinateFrameRevision;
    clearPlaneEdgeUi();
    m_canvas->clearWorkpieceCoordinateSystem();
    m_canvas->setSelectedIndices(m_selectedPointIndices);
    m_secondPlaneSelectionActive = true;
    m_canvas->setSelectionMode(true);
    statusBar()->showMessage(tr("请在同一平面上选择第二组三点"));
    updatePlaneExtractionUi();
}

void MainWindow::cancelSecondPlanePointSelection() {
    if (pointTaskRunning()) return;
    resetSecondPlaneVerification();
    if (m_canvas) {
        m_canvas->setSelectionMode(false);
        m_canvas->setSelectedIndices(m_selectedPointIndices);
    }
    statusBar()->showMessage(tr("已取消第二组三点校验"));
    updatePlaneExtractionUi();
}

void MainWindow::validateSecondPlaneSelection() {
    if (m_secondPlanePointIndices.size() != 3 || !m_threePlaneResult.ok) return;
    const float angleTolerance = float(m_defaults.planeAngleToleranceDeg);
    const float distanceTolerance = float(m_defaults.planeDistanceToleranceMm);
    bool autoConfirmJsonFrame = false;
    const pointcloud::PlaneConsistencyResult validation =
        pointcloud::validatePlaneConsistency(
            m_points, m_threePlaneResult.model, m_selectedPointIndices,
            m_secondPlanePointIndices, angleTolerance, distanceTolerance);
    m_secondPlaneNormalAngle = validation.normalAngleDegrees;
    m_secondPlaneMaximumDistance = validation.maximumDistanceMm;
    m_threeOutput->appendPlainText(tr("\n第二组三点校验：法向夹角 %1°，最大距离 %2 mm")
        .arg(validation.normalAngleDegrees, 0, 'f', 3)
        .arg(validation.maximumDistanceMm, 0, 'f', 4));
    if (validation.status == pointcloud::PlaneConsistencyStatus::Passed) {
        m_secondPlaneValidated = true;
        m_secondPlaneSamePlane = true;
        autoConfirmJsonFrame = m_tempWorkpieceSessionActive;
        statusBar()->showMessage(m_tempWorkpieceSessionActive
            ? tr("第二组三点确认属于同一平面，JSON 流程将自动建立 WObj1")
            : tr("第二组三点确认属于同一平面，系统将按机器人基坐标轴自动建立 WObj1"));
    } else if (validation.status == pointcloud::PlaneConsistencyStatus::Collinear
               || validation.status == pointcloud::PlaneConsistencyStatus::InvalidInput
               || validation.status == pointcloud::PlaneConsistencyStatus::ReusedPoint) {
        const QString error = validation.error;
        resetSecondPlaneVerification();
        m_canvas->setSelectedIndices(m_selectedPointIndices);
        QMessageBox::warning(this, tr("第二组三点无效"), error);
        statusBar()->showMessage(tr("第二组三点已清除，请重新选择"));
    } else {
        const QString error = validation.error;
        m_threePlaneResult = {};
        m_selectedPointIndices.clear();
        resetSecondPlaneVerification();
        m_planeCenter = {};
        m_planeCenterValid = false;
        m_xAxisPointIndex = m_yAxisPointIndex = -1;
        m_axisSelectionActive = false;
        m_workpieceCoordinate = {};
        ++m_coordinateFrameRevision;
        clearPlaneEdgeUi();
        m_planeCandidateConfirmed = false;
        m_planeFinalizationPending = false;
        m_threePointSelectionActive = true;
        m_canvas->clearPlaneResult();
        m_canvas->clearWorkpieceCoordinateSystem();
        m_canvas->setSelectedIndices({});
        m_canvas->setSelectionMode(true);
        QMessageBox::warning(this, tr("平面不一致"),
            tr("第二组三点不属于当前确认平面。\n法向夹角：%1°（阈值 %2°）\n"
               "最大距离：%3 mm（阈值 %4 mm）\n请检查选点或台阶。")
                .arg(validation.normalAngleDegrees, 0, 'f', 3).arg(angleTolerance, 0, 'f', 1)
                .arg(validation.maximumDistanceMm, 0, 'f', 4).arg(distanceTolerance, 0, 'f', 2));
        m_threeOutput->appendPlainText(error);
        statusBar()->showMessage(tr("当前候选已清除，请重新选择第一组平面控制点"));
    }
    updatePlaneExtractionUi();
    updatePlaneEdgeUi();
    if (autoConfirmJsonFrame)
        confirmPlaneCandidate();
}

void MainWindow::startWorkpieceAxisSelection() {
    m_axisSelectionActive = false;
    statusBar()->showMessage(tr("WObj1 已按机器人基坐标 X/Y 轴投影自动建立，无需人工选轴点"));
}

void MainWindow::clearWorkpieceAxisSelection() {
    if (pointTaskRunning()) return;
    m_axisSelectionActive = false;
    m_xAxisPointIndex = m_yAxisPointIndex = -1;
    m_automaticAxisPointSelection = {};
    m_workpieceCoordinate = {};
    ++m_coordinateFrameRevision;
    clearPlaneEdgeUi();
    m_canvas->setSelectionMode(false);
    m_canvas->setWorkpieceCoordinateSystem({});
    QVector<int> marked = m_selectedPointIndices;
    marked += m_secondPlanePointIndices;
    m_canvas->setSelectedIndices(marked);
    statusBar()->showMessage(tr("已清除自动辅助轴点和 WObj1"));
    updatePlaneExtractionUi();
}

void MainWindow::runPlaneExtraction(bool deferFinalClassification) {
    if (pointTaskRunning()) return;
    if (m_selectedPointIndices.size() < 3) {
        statusBar()->showMessage(tr("请先在画布中指定至少三个点"));
        return;
    }
    resetSecondPlaneVerification();
    m_axisSelectionActive = false;
    m_xAxisPointIndex = m_yAxisPointIndex = -1;
    m_planeCenter = {};
    m_planeCenterValid = false;
    m_workpieceCoordinate = {};
    ++m_coordinateFrameRevision;
    clearPlaneEdgeUi();
    m_planeCandidateConfirmed = false;
    m_planeFinalizationPending = false;
    if (m_canvas) m_canvas->clearWorkpieceCoordinateSystem();
    pointcloud::ThreePointPlaneOptions options;
    options.initialTolerance = 1.0f;
    options.surfaceTolerance = float(m_defaults.planeDistanceToleranceMm);
    options.ransacIterations = 300;
    options.minInliers = qMin(100, qMax(3, int(m_points.size() / 4)));
    options.useZAxisResidual = true;
    options.maxNormalTiltDegrees = 45.0f;
    options.deferFinalClassification = deferFinalClassification;
    if (!m_threePlaneWatcher) {
        m_threePlaneWatcher = new QFutureWatcher<pointcloud::ThreePointPlaneResult>(this);
        connect(m_threePlaneWatcher, &QFutureWatcher<pointcloud::ThreePointPlaneResult>::finished,
                this, &MainWindow::planeExtractionFinished);
    }
    const QVector<pointcloud::Point3D> source = m_points;
    const QVector<int> seeds = m_selectedPointIndices;
    m_threePlaneInputRevision = m_canvasRevision;
    m_threePointSelectionActive = false;
    m_canvas->setSelectionMode(false);
    m_progress->show();
    m_progress->setRange(0, 0);
    statusBar()->showMessage(deferFinalClassification
        ? tr("快速拟合候选平面...") : tr("正在完成平面分类和连通域处理..."));
    updatePlaneExtractionUi();
    m_threePlaneWatcher->setFuture(QtConcurrent::run([source, seeds, options]() {
        return pointcloud::extractPlaneFromPoints(source, seeds, options);
    }));
}

void MainWindow::planeExtractionFinished() {
    const pointcloud::ThreePointPlaneResult result = m_threePlaneWatcher->result();
    m_progress->hide();
    if (m_threePlaneInputRevision != m_canvasRevision) {
        statusBar()->showMessage(tr("画布缓存已变化，已丢弃旧三点平面结果"));
        return;
    }
    if (!result.ok) {
        m_threePlaneResult = {};
        m_planeCenter = {};
        m_planeCenterValid = false;
        resetSecondPlaneVerification();
        m_planeFinalizationPending = false;
        m_threePointSelectionActive = true;
        m_canvas->setSelectionMode(true);
        m_canvas->setSelectedIndices(m_selectedPointIndices);
        m_threeOutput->appendPlainText(QStringLiteral("\n") + result.error);
        m_threeOutput->appendPlainText(tr("已保留当前控制点；可继续取点或撤销后重试"));
        updatePlaneExtractionUi();
        statusBar()->showMessage(result.error);
        return;
    }
    const pointcloud::PlaneBoundsCenterResult center =
        pointcloud::calculatePlaneBoundsCenter(m_points, result.planeIndices);
    if (!center.ok) {
        m_threePlaneResult = {};
        m_planeCenter = {};
        m_planeCenterValid = false;
        resetSecondPlaneVerification();
        m_planeFinalizationPending = false;
        m_canvas->clearPlaneResult();
        m_threeOutput->appendPlainText(QStringLiteral("\n") + center.error);
        updatePlaneExtractionUi();
        statusBar()->showMessage(center.error);
        return;
    }
    m_threePlaneResult = result;
    m_planeCenter = center.center;
    m_planeCenterValid = true;
    resetSecondPlaneVerification();
    m_xAxisPointIndex = m_yAxisPointIndex = -1;
    m_automaticAxisPointSelection = {};
    m_axisSelectionActive = false;
    ++m_coordinateFrameRevision;
    clearPlaneEdgeUi();
    m_planeCandidateConfirmed = false;
    m_planeFinalizationPending = result.deferred;
    m_canvas->setExtractedPlane(result.planeIndices);
    const auto &plane = result.model;
    QStringList lines;
    lines << tr("第一组平面控制点：%1 个").arg(m_selectedPointIndices.size());
    for (int i = 0; i < m_selectedPointIndices.size(); ++i) {
        const auto &p = m_points[m_selectedPointIndices[i]];
        lines << tr("控制点 P%1  (%2, %3, %4)").arg(i + 1)
                     .arg(p.x, 0, 'g', 8).arg(p.y, 0, 'g', 8).arg(p.z, 0, 'g', 8);
    }
    lines << QString()
          << tr("%1 x + %2 y + %3 z + %4 = 0")
                 .arg(plane.a, 0, 'g', 9).arg(plane.b, 0, 'g', 9)
                 .arg(plane.c, 0, 'g', 9).arg(plane.d, 0, 'g', 9)
          << tr("初始候选点：%1").arg(QLocale().toString(result.candidateIndices.size()))
          << tr("候选平面点：%1").arg(QLocale().toString(result.planeIndices.size()))
          << tr("全部连通区域：%1").arg(result.connectedComponentCount)
          << tr("显著连通面：%1").arg(result.significantComponentCount)
          << tr("分离平面点：%1")
                 .arg(QLocale().toString(result.disconnectedPlaneIndices.size()))
          << tr("PCA 平面性：%1").arg(result.planarity, 0, 'f', 6)
          << tr("PCA 精拟合轮次：%1").arg(result.pcaRefinementCount)
          << tr("Z 方向 RMS：%1 mm").arg(result.rmsError, 0, 'g', 7)
          << tr("平面包围盒中心（WObj1 原点 O）：X %1  Y %2  Z %3 mm")
                 .arg(m_planeCenter.x(), 0, 'f', 3)
                 .arg(m_planeCenter.y(), 0, 'f', 3)
                 .arg(m_planeCenter.z(), 0, 'f', 3)
          << QString() << (result.deferred
              ? tr("快速候选平面已生成，请确认后完成全量分类")
              : tr("候选平面已生成，请进行第二组三点验证"));
    m_threeOutput->setPlainText(lines.join(QLatin1Char('\n')));
    updatePlaneExtractionUi();
    updatePlaneEdgeUi();
    if (!result.deferred) {
        statusBar()->showMessage(tr("平面已生成"));
    } else {
        statusBar()->showMessage(tr("候选平面已生成"));
    }
}

void MainWindow::confirmPlaneCandidate() {
    if (!m_threePlaneResult.ok || pointTaskRunning()) return;
    if (m_planeFinalizationPending) {
        runPlaneExtraction(false);
        return;
    }
    if (!m_secondPlaneValidated || !m_secondPlaneSamePlane) {
        statusBar()->showMessage(tr("请先完成并通过第二组三点平面验证"));
        return;
    }
    if (!m_planeCenterValid) {
        statusBar()->showMessage(tr("当前平面包围盒中心无效，无法建立 WObj1"));
        return;
    }
    if (m_selectedPointIndices.size() < 3) {
        statusBar()->showMessage(tr("建立 WObj1 需要有效的第一组平面控制点"));
        return;
    }
    pointcloud::WorkpieceCoordinateSystem frame;
    const QVector3D fittedNormal(m_threePlaneResult.model.a,
                                 m_threePlaneResult.model.b,
                                 m_threePlaneResult.model.c);
    m_automaticAxisPointSelection = pointcloud::selectAutomaticWorkpieceAxisPoints(
        m_points, m_threePlaneResult.planeIndices, m_planeCenter, fittedNormal);
    m_xAxisPointIndex = m_automaticAxisPointSelection.xPointIndex;
    m_yAxisPointIndex = m_automaticAxisPointSelection.yPointIndex;
    if (!m_automaticAxisPointSelection.ok) {
        const QString error = QStringLiteral("无法完成 O/X+/Y+ 自动三点取点：%1")
            .arg(m_automaticAxisPointSelection.error);
        statusBar()->showMessage(error);
        m_threeOutput->appendPlainText(QStringLiteral("\n") + error);
        return;
    }
    frame = pointcloud::buildWorkpieceCoordinateSystemFromThreePoints(
        m_points, m_planeCenter, m_xAxisPointIndex, m_yAxisPointIndex);
    if (!frame.valid) {
        statusBar()->showMessage(frame.error);
        m_threeOutput->appendPlainText(QStringLiteral("\n") + frame.error);
        return;
    }
    m_workpieceCoordinate = frame;
    ++m_coordinateFrameRevision;
    clearPlaneEdgeUi();
    m_planeCandidateConfirmed = true;
    m_threePointSelectionActive = false;
    m_secondPlaneSelectionActive = false;
    m_canvas->setSelectionMode(false);
    m_canvas->setWorkpieceCoordinateSystem(m_workpieceCoordinate);
    QVector<int> marked = m_selectedPointIndices;
    marked += m_secondPlanePointIndices;
    if (m_xAxisPointIndex >= 0 && !marked.contains(m_xAxisPointIndex))
        marked.push_back(m_xAxisPointIndex);
    if (m_yAxisPointIndex >= 0 && !marked.contains(m_yAxisPointIndex))
        marked.push_back(m_yAxisPointIndex);
    m_canvas->setSelectedIndices(marked);
    QStringList frameLines;
    frameLines << tr("候选平面已确定，平面包围盒中心作为 O/WObj1 原点")
               << tr("O/X+/Y+ 三点：O 为 WObj1 原点，X+/Y+ 为机器人轴投影方向的自动辅助点")
               << tr("工件原点（机器人基坐标）：X %1  Y %2  Z %3 mm")
                      .arg(frame.originInRobotBase.x(), 0, 'f', 3)
                      .arg(frame.originInRobotBase.y(), 0, 'f', 3)
                      .arg(frame.originInRobotBase.z(), 0, 'f', 3)
               << tr("O 点（机器人基坐标）：[%1, %2, %3]")
                      .arg(frame.originInRobotBase.x(), 0, 'f', 6)
                      .arg(frame.originInRobotBase.y(), 0, 'f', 6)
                      .arg(frame.originInRobotBase.z(), 0, 'f', 6);
    const auto appendBasePoint = [this, &frameLines](const QString &name, int index) {
        if (index < 0 || index >= m_points.size()) {
            frameLines << tr("%1 点（机器人基坐标）：无效索引 #%2")
                              .arg(name).arg(index);
            return;
        }
        const pointcloud::Point3D &point = m_points[index];
        frameLines << tr("%1 点（机器人基坐标）：[%2, %3, %4]（索引 #%5）")
                          .arg(name)
                          .arg(point.x, 0, 'f', 6)
                          .arg(point.y, 0, 'f', 6)
                          .arg(point.z, 0, 'f', 6)
                          .arg(index);
    };
    appendBasePoint(QStringLiteral("X+"), m_xAxisPointIndex);
    appendBasePoint(QStringLiteral("Y+"), m_yAxisPointIndex);
    frameLines << tr("O/X+/Y+ 三点均为机器人基坐标系 XYZ 坐标，作为 WObj1 输出依据")
               << tr("工件姿态：A %1°  B %2°  C %3°")
                      .arg(frame.poseA, 0, 'f', 4)
                      .arg(frame.poseB, 0, 'f', 4)
                      .arg(frame.poseC, 0, 'f', 4)
               << tr("姿态约定：A=Rx、B=Ry、C=Rz；Rz(C) × Ry(B) × Rx(A)")
               << tr("X 轴（O -> 自动 X+，三点法计算）：[%1, %2, %3]")
                      .arg(frame.axisXInRobotBase.x(), 0, 'g', 8)
                      .arg(frame.axisXInRobotBase.y(), 0, 'g', 8)
                      .arg(frame.axisXInRobotBase.z(), 0, 'g', 8)
               << tr("Y 轴（O -> 自动 Y+，三点法正交化）：[%1, %2, %3]")
                      .arg(frame.axisYInRobotBase.x(), 0, 'g', 8)
                      .arg(frame.axisYInRobotBase.y(), 0, 'g', 8)
                      .arg(frame.axisYInRobotBase.z(), 0, 'g', 8)
               << tr("Z 轴：[%1, %2, %3]")
                      .arg(frame.axisZInRobotBase.x(), 0, 'g', 8)
                      .arg(frame.axisZInRobotBase.y(), 0, 'g', 8)
                      .arg(frame.axisZInRobotBase.z(), 0, 'g', 8)
               << tr("正交误差：%1").arg(frame.orthogonalityError, 0, 'g', 6)
               << tr("T_base_workpiece：");
    if (m_automaticAxisPointSelection.ok) {
        frameLines << tr("自动 X 辅助点：#%1，实际距离 %2 mm%3")
                          .arg(m_automaticAxisPointSelection.xPointIndex)
                          .arg(m_automaticAxisPointSelection.xActualDistanceMm, 0, 'f', 3)
                          .arg(m_automaticAxisPointSelection.xUsedFallback
                                   ? tr("（fallback）") : QString())
                   << tr("自动 Y 辅助点：#%1，实际距离 %2 mm%3")
                          .arg(m_automaticAxisPointSelection.yPointIndex)
                          .arg(m_automaticAxisPointSelection.yActualDistanceMm, 0, 'f', 3)
                          .arg(m_automaticAxisPointSelection.yUsedFallback
                                   ? tr("（fallback）") : QString())
                   << tr("自动取点目标距离：%1 mm")
                          .arg(m_automaticAxisPointSelection.targetDistanceMm, 0, 'f', 3);
    } else {
        frameLines << tr("自动辅助点警告：%1")
                          .arg(m_automaticAxisPointSelection.error);
    }
    for (int row = 0; row < 4; ++row) {
        frameLines << QStringLiteral("[%1  %2  %3  %4]")
            .arg(frame.workpieceToRobotBase(row, 0), 0, 'g', 9)
            .arg(frame.workpieceToRobotBase(row, 1), 0, 'g', 9)
            .arg(frame.workpieceToRobotBase(row, 2), 0, 'g', 9)
            .arg(frame.workpieceToRobotBase(row, 3), 0, 'g', 9);
    }
    m_threeOutput->appendPlainText(QStringLiteral("\n")
                                   + frameLines.join(QLatin1Char('\n')));
    updatePlaneExtractionUi();
    updatePlaneEdgeUi();
    if (m_tempWorkpieceSessionActive) {
        statusBar()->showMessage(tr("WObj1 已自动建立，正在执行工件坐标系下的边缘分割"));
        applyPlaneEdgeSegmentation();
        return;
    }
    statusBar()->showMessage(tr("工件坐标系已确定：原点 X %1 Y %2 Z %3 mm，A %4 B %5 C %6°")
                                .arg(frame.originInRobotBase.x(), 0, 'f', 3)
                                .arg(frame.originInRobotBase.y(), 0, 'f', 3)
                                .arg(frame.originInRobotBase.z(), 0, 'f', 3)
                                .arg(frame.poseA, 0, 'f', 3)
                                .arg(frame.poseB, 0, 'f', 3)
                                .arg(frame.poseC, 0, 'f', 3));
}

void MainWindow::cancelPlaneCandidate() {
    if (!m_threePlaneResult.ok || pointTaskRunning()) return;
    m_threePlaneResult = {};
    resetSecondPlaneVerification();
    m_planeCenter = {};
    m_planeCenterValid = false;
    m_axisSelectionActive = false;
    m_xAxisPointIndex = m_yAxisPointIndex = -1;
    m_automaticAxisPointSelection = {};
    m_workpieceCoordinate = {};
    ++m_coordinateFrameRevision;
    clearPlaneEdgeUi();
    m_planeCandidateConfirmed = false;
    m_threePointSelectionActive = false;
    m_planeFinalizationPending = false;
    m_canvas->clearPlaneResult();
    m_canvas->clearWorkpieceCoordinateSystem();
    m_canvas->setSelectionMode(false);
    updatePlaneExtractionUi();
    updatePlaneEdgeUi();
    statusBar()->showMessage(tr("已取消确定平面，可重新确定或撤销取点"));
}

void MainWindow::applyPlaneEdgeSegmentation() {
    if (pointTaskRunning()) {
        statusBar()->showMessage(tr("已有点云处理任务正在运行"));
        return;
    }
    if (!m_planeCandidateConfirmed || !m_threePlaneResult.ok) {
        statusBar()->showMessage(tr("请先在平面提取页确定候选平面"));
        return;
    }
    pointcloud::PlaneEdgeOptions options;
    options.edgeGridSize = float(m_edgeGridSize->value());
    options.morphologyCloseRadius = m_edgeCloseRadius->value();
    options.morphologyOpenRadius = m_edgeOpenRadius->value();
    options.useImageFrame = m_workpieceCoordinate.valid;
    if (options.useImageFrame) {
        options.imageOrigin = m_workpieceCoordinate.originInRobotBase;
        options.imageAxisU = m_workpieceCoordinate.axisXInRobotBase;
        options.imageAxisV = m_workpieceCoordinate.axisYInRobotBase;
    }
    if (!m_edgeWatcher) {
        m_edgeWatcher = new QFutureWatcher<pointcloud::PlaneEdgeResult>(this);
        connect(m_edgeWatcher, &QFutureWatcher<pointcloud::PlaneEdgeResult>::finished,
                this, &MainWindow::planeEdgeSegmentationFinished);
    }
    const QVector<pointcloud::Point3D> source = m_points;
    const QVector<int> planeIndices = m_threePlaneResult.planeIndices;
    const pointcloud::PlaneModel model = m_threePlaneResult.model;
    m_edgeInputRevision = m_canvasRevision;
    clearPlaneEdgeUi();
    m_progress->show();
    m_progress->setRange(0, 0);
    m_edgeOutput->setPlainText(tr("正在生成平面 Mask、边缘和 2D 图像..."));
    updatePlaneEdgeUi();
    statusBar()->showMessage(tr("后台执行平面边缘分割..."));
    m_edgeWatcher->setFuture(QtConcurrent::run([source, planeIndices, model, options]() {
        return pointcloud::segmentPlaneEdges(source, planeIndices, model, options);
    }));
}

void MainWindow::planeEdgeSegmentationFinished() {
    const pointcloud::PlaneEdgeResult result = m_edgeWatcher->result();
    m_progress->hide();
    if (m_edgeInputRevision != m_canvasRevision) {
        m_edgeOutput->setPlainText(tr("画布缓存已变化，旧边缘结果已丢弃。"));
        updatePlaneEdgeUi();
        statusBar()->showMessage(tr("画布缓存已变化，已丢弃旧边缘结果"));
        return;
    }
    if (!result.ok) {
        m_planeEdgeResult = {};
        m_edgeOutput->setPlainText(result.error);
        updatePlaneEdgeUi();
        statusBar()->showMessage(tr("边缘分割失败"));
        return;
    }
    m_planeEdgeResult = result;
    m_canvas->setPlaneResult(m_threePlaneResult.planeIndices,
                             result.edgeIndices, result.contours);
    const int holeCount = int(std::count_if(
        result.contours.cbegin(), result.contours.cend(),
        [](const pointcloud::PlaneContour &contour) { return contour.hole; }));
    m_edgeOutput->setPlainText(
        tr("边缘点：%1\n有序轮廓：%2\n孔洞轮廓：%3\n占用栅格：%4\n"
           "栅格尺寸：%5 mm\n图像尺寸：%6 × %7 px\n物理范围：%8 × %9 mm")
            .arg(QLocale().toString(result.edgeIndices.size()))
            .arg(QLocale().toString(result.contours.size()))
            .arg(QLocale().toString(holeCount))
            .arg(QLocale().toString(result.occupiedCellCount))
            .arg(result.gridSize, 0, 'g', 6)
            .arg(result.image.width()).arg(result.image.height())
            .arg(result.width, 0, 'g', 7).arg(result.height, 0, 'g', 7));
    m_planeImagePreview->setPixmap({});
    m_planeImagePreview->setText(tr("正在按边缘 Mask 生成可保存 2D 图像..."));
    // Re-rasterize the confirmed plane with the canonical export frame. This
    // makes an edge-segmentation run obey the same 50 mm margin, 10 mm
    // physical rounding and 0.05 mm/px contract as direct plane extraction.
    pointcloud::PlaneEdgeOptions imageOptions;
    imageOptions.maximumImagePixels = m_defaults.maximumImagePixels;
    imageOptions.useImageFrame = m_workpieceCoordinate.valid;
    imageOptions.autoImageBounds = true;
    imageOptions.imageMargin = float(m_defaults.imageMarginMm);
    imageOptions.imagePixelSize = float(m_defaults.imagePixelSizeMm);
    imageOptions.imageRoundIncrement = float(m_defaults.imageRoundIncrementMm);
    imageOptions.imageOrigin = m_workpieceCoordinate.originInRobotBase;
    imageOptions.imageAxisU = m_workpieceCoordinate.axisXInRobotBase;
    imageOptions.imageAxisV = m_workpieceCoordinate.axisYInRobotBase;
    const QVector<pointcloud::Point3D> imageSource = m_points;
    const QVector<int> imageIndices = m_threePlaneResult.planeIndices;
    const pointcloud::PlaneModel imageModel = m_threePlaneResult.model;
    const pointcloud::PlaneEdgeResult edgeMask = result;
    m_planeImageInputRevision = m_canvasRevision;
    m_planeImageCoordinateRevision = m_coordinateFrameRevision;
    if (!m_planeImageWatcher) {
        m_planeImageWatcher = new QFutureWatcher<pointcloud::PlaneImageResult>(this);
        connect(m_planeImageWatcher, &QFutureWatcher<pointcloud::PlaneImageResult>::finished,
                this, &MainWindow::planeImageExtractionFinished);
    }
    m_planeImageWatcher->setFuture(QtConcurrent::run(
        [imageSource, imageIndices, imageModel, imageOptions, edgeMask]() {
            return pointcloud::rasterizePlaneEdgeMask(imageSource, imageIndices,
                                                      imageModel, imageOptions,
                                                      edgeMask);
        }));
    m_edgeOutput->appendPlainText(tr("\n正在按边缘 Mask、自动边界生成可保存 2D 图像..."));
    updatePlaneEdgeUi();
    statusBar()->showMessage(tr("边缘分割和 2D 平面图生成完成"));
}

void MainWindow::startEdgePointSelection() {
    if (pointTaskRunning() || !m_planeEdgeResult.ok) return;
    m_edgeSelectionActive = true;
    m_selectedEdgeIndices.clear();
    m_canvas->setEdgeSelectionMode(true, m_planeEdgeResult.edgeIndices);
    m_canvas->setSelectedEdgeIndices({});
    updatePlaneEdgeUi();
    statusBar()->showMessage(tr("请在画布中选择边缘真实点，Esc 取消"));
}

void MainWindow::clearEdgePointSelection() {
    m_edgeSelectionActive = false;
    m_selectedEdgeIndices.clear();
    if (m_canvas) {
        m_canvas->setEdgeSelectionMode(false, {});
        m_canvas->setSelectedEdgeIndices({});
    }
    updatePlaneEdgeUi();
    statusBar()->showMessage(tr("已清除边缘点选择"));
}

void MainWindow::handleCanvasEdgePointPicked(int index) {
    if (!m_edgeSelectionActive || pointTaskRunning()) return;
    if (index < 0 || !m_planeEdgeResult.edgeIndices.contains(index)) {
        statusBar()->showMessage(tr("请选择黄色边缘真实点"));
        return;
    }
    if (!m_selectedEdgeIndices.contains(index))
        m_selectedEdgeIndices.push_back(index);
    m_canvas->setSelectedEdgeIndices(m_selectedEdgeIndices);
    updatePlaneEdgeUi();
    statusBar()->showMessage(tr("已选择边缘点：%1").arg(m_selectedEdgeIndices.size()));
}

void MainWindow::savePlaneImage() {
    QString validationError;
    if (!planeImageSaveReady(&validationError)) {
        QMessageBox::warning(this, tr("无法保存平面图像"), validationError);
        statusBar()->showMessage(validationError);
        return;
    }
    const QImage &image = m_planeImageResult.image;
    if (image.isNull()) {
        statusBar()->showMessage(tr("请先提取平面 2D 图像"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("保存平面 2D 图片"), QStringLiteral("plane_2d.png"),
        tr("PNG 图片 (*.png)"));
    if (path.isEmpty()) return;
    // Keep the contract writer's atomic PNG/PLY/JSON output, while honoring
    // the directory selected in the save dialog.
    {
        const QString baseName = QFileInfo(path).completeBaseName();
        pcv::output::JobContext context{
            pcv::output::defaultRuntimeRoot(),
            QStringLiteral("job_%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss_zzz"))),
            baseName, baseName};
        context.destinationDirectory = QFileInfo(path).absolutePath();
        pcv::output::PlaneOutputMetadata metadata;
        metadata.sourcePointCloud = QFileInfo(m_pendingPath).absoluteFilePath();
        metadata.sourcePlyEncoding = QStringLiteral("ascii_or_binary");
        metadata.originInRobotBase = m_workpieceCoordinate.originInRobotBase;
        metadata.axisXInRobotBase = m_workpieceCoordinate.axisXInRobotBase;
        metadata.axisYInRobotBase = m_workpieceCoordinate.axisYInRobotBase;
        metadata.axisZInRobotBase = m_workpieceCoordinate.axisZInRobotBase;
        metadata.abcDeg = QVector3D(m_workpieceCoordinate.poseA, m_workpieceCoordinate.poseB,
                                    m_workpieceCoordinate.poseC);
        metadata.TBaseWorkpiece = m_workpieceCoordinate.workpieceToRobotBase;
        metadata.TWorkpieceBase = m_workpieceCoordinate.robotBaseToWorkpiece;
        metadata.planeEquation = QVector4D(m_threePlaneResult.model.a, m_threePlaneResult.model.b,
                                           m_threePlaneResult.model.c, m_threePlaneResult.model.d);
        metadata.rmsErrorMm = m_threePlaneResult.rmsError;
        metadata.distanceToleranceMm = m_planeImageResult.usedPlaneDistanceTolerance;
        metadata.physicalWidthMm = m_planeImageResult.width;
        metadata.physicalHeightMm = m_planeImageResult.height;
        metadata.pixelSizeMm = m_planeImageResult.pixelSize > 0.0f
            ? m_planeImageResult.pixelSize : 0.05;
        metadata.marginMm = m_planeImageResult.margin;
        metadata.roundIncrementMm = 10.0;
        metadata.automaticBounds = m_planeImageResult.automaticBounds;
        metadata.edgeMask = m_planeImageResult.edgeMask;
        metadata.diagnostics = QJsonObject{
            {QStringLiteral("plane_point_count"), m_threePlaneResult.planeIndices.size()},
            {QStringLiteral("input_index_count"), m_planeImageResult.inputIndexCount},
            {QStringLiteral("mapped_plane_point_count"), m_planeImageResult.mappedPlanePointCount},
            {QStringLiteral("rejected_non_plane_point_count"), m_planeImageResult.rejectedNonPlanePointCount},
            {QStringLiteral("rejected_invalid_point_count"), m_planeImageResult.rejectedInvalidPointCount},
            {QStringLiteral("rejected_outside_rectangle_count"), m_planeImageResult.rejectedOutsideRectangleCount},
            {QStringLiteral("occupied_pixel_count"), m_planeImageResult.occupiedCellCount},
            {QStringLiteral("edge_mask"), m_planeImageResult.edgeMask}};
        const auto output = pcv::output::writePlaneOutput(
            context, image, m_points, m_threePlaneResult.planeIndices, metadata);
        if (!output.success) {
            QMessageBox::warning(this, tr("平面输出失败"),
                                 output.errorCode + QStringLiteral(": ") + output.message);
            statusBar()->showMessage(tr("平面输出失败"));
        } else {
            const QDir outputDirectory(context.destinationDirectory);
            statusBar()->showMessage(
                tr("平面 PNG、PLY、JSON 已成套保存：\n%1\n%2\n%3")
                    .arg(outputDirectory.filePath(output.planePng),
                         outputDirectory.filePath(output.planeRobotBasePly),
                         outputDirectory.filePath(output.planeJson)));
        }
        return;
    }
}

void MainWindow::applyNoiseRemoval() {
    if (m_loading || m_points.isEmpty()) {
        statusBar()->showMessage(tr("请先完成点云加载"));
        return;
    }
    if (pointTaskRunning()) {
        statusBar()->showMessage(tr("已有点云处理任务正在运行"));
        return;
    }
    pointcloud::NoiseOptions options;
    options.voxelEnabled = m_voxelNoise->isChecked(); options.voxelSize = float(m_voxelSize->value());
    options.statisticalEnabled = m_statisticalNoise->isChecked(); options.meanK = m_meanK->value();
    options.stddevMultiplier = float(m_stddev->value());
    if (!m_noiseWatcher) {
        m_noiseWatcher = new QFutureWatcher<pointcloud::NoiseResult>(this);
        connect(m_noiseWatcher, &QFutureWatcher<pointcloud::NoiseResult>::finished,
                this, &MainWindow::noiseFinished);
    }
    m_noiseApply->setEnabled(false);
    m_progress->show(); m_progress->setRange(0, 0);
    statusBar()->showMessage(tr("后台执行噪点去除..."));
    // The canvas cache is the only valid input for all follow-up processing.
    const QVector<pointcloud::Point3D> source = m_points;
    m_noiseInputCount = source.size();
    m_noiseInputRevision = m_canvasRevision;
    m_noiseWatcher->setFuture(QtConcurrent::run([source, options]() {
        return pointcloud::removeNoise(source, options);
    }));
}

void MainWindow::noiseFinished() {
    const pointcloud::NoiseResult result = m_noiseWatcher->result();
    m_noiseApply->setEnabled(true); m_progress->hide();
    if (m_noiseInputRevision != m_canvasRevision) {
        statusBar()->showMessage(tr("画布缓存已变化，已丢弃旧去噪结果"));
        return;
    }
    if (!result.ok) {
        QMessageBox::warning(this, tr("噪点去除失败"), result.error);
        statusBar()->showMessage(tr("噪点去除未改变当前点云"));
        return;
    }
    publishCanvasCache(result.points);
    m_canvasInfo->setText(tr("清理后显示 %1 点 · 原始 %2 点")
                          .arg(QLocale().toString(m_points.size()))
                          .arg(QLocale().toString(m_rawPoints.size())));
    const QString completion = tr("当前处理层完成：%1 → %2 点；后续处理将基于此结果")
        .arg(QLocale().toString(m_noiseInputCount))
        .arg(QLocale().toString(result.points.size()));
    statusBar()->showMessage(result.error.isEmpty()
                                 ? completion
                                 : tr("%1；%2").arg(completion, result.error));
}

void MainWindow::closeEvent(QCloseEvent *event) {
    m_closing = true;
    qWarning() << "MainWindow closeEvent, spontaneous=" << event->spontaneous()
               << "visible=" << isVisible()
               << "loading=" << m_loading
               << "loadRunning="
               << (m_loadWatcher && m_loadWatcher->isRunning());
    setEnabled(false);
    if (m_canvas) {
        m_canvas->setUpdatesEnabled(false);
        m_canvas->hide();
    }

    // Prevent queued watcher completions from entering slots that manipulate
    // widgets or QPixmap/QImage objects while Qt is destroying the GUI tree.
    if (m_loadWatcher) disconnect(m_loadWatcher, nullptr, this, nullptr);
    if (m_tempWorkpieceWatcher) disconnect(m_tempWorkpieceWatcher, nullptr, this, nullptr);
    if (m_tempWorkpieceFinalizeWatcher)
        disconnect(m_tempWorkpieceFinalizeWatcher, nullptr, this, nullptr);
    if (m_noiseWatcher) disconnect(m_noiseWatcher, nullptr, this, nullptr);
    if (m_threePlaneWatcher) disconnect(m_threePlaneWatcher, nullptr, this, nullptr);
    if (m_edgeWatcher) disconnect(m_edgeWatcher, nullptr, this, nullptr);
    if (m_planeImageWatcher) disconnect(m_planeImageWatcher, nullptr, this, nullptr);
    if (m_loading && m_loadWatcher && m_loadWatcher->isRunning()) {
        statusBar()->showMessage(tr("正在结束后台加载，请稍候..."));
        m_loadWatcher->waitForFinished();
        m_loading = false;
    }
    if (m_tempWorkpieceWatcher && m_tempWorkpieceWatcher->isRunning()) {
        statusBar()->showMessage(tr("正在结束后台扫描 JSON 处理，请稍候..."));
        m_tempWorkpieceWatcher->waitForFinished();
    }
    if (m_tempWorkpieceFinalizeWatcher && m_tempWorkpieceFinalizeWatcher->isRunning()) {
        statusBar()->showMessage(tr("正在结束后台临时工件输出，请稍候..."));
        m_tempWorkpieceFinalizeWatcher->waitForFinished();
    }
    if (m_noiseWatcher && m_noiseWatcher->isRunning()) {
        statusBar()->showMessage(tr("正在结束后台噪点处理，请稍候..."));
        m_noiseWatcher->waitForFinished();
    }
    if (m_threePlaneWatcher && m_threePlaneWatcher->isRunning()) {
        statusBar()->showMessage(tr("正在结束后台平面拟合，请稍候..."));
        m_threePlaneWatcher->waitForFinished();
    }
    if (m_edgeWatcher && m_edgeWatcher->isRunning()) {
        statusBar()->showMessage(tr("正在结束后台边缘处理，请稍候..."));
        m_edgeWatcher->waitForFinished();
    }
    if (m_planeImageWatcher && m_planeImageWatcher->isRunning()) {
        statusBar()->showMessage(tr("正在结束后台平面图像提取，请稍候..."));
        m_planeImageWatcher->waitForFinished();
    }
    event->accept();
}


