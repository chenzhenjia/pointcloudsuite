#include <pcv/render/pointcloud_canvas.h>

PointCloudCanvas::PointCloudCanvas(QWidget *parent)
        : QOpenGLWidget(parent), m_vertexBuffer(QOpenGLBuffer::VertexBuffer),
          m_stateBuffer(QOpenGLBuffer::VertexBuffer),
          m_contourBuffer(QOpenGLBuffer::VertexBuffer) {
        setMinimumSize(620, 480);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setToolTip(tr("左键旋转，右键平移，滚轮缩放"));
    }

PointCloudCanvas::~PointCloudCanvas()  {
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

void PointCloudCanvas::setCloud(QVector<pointcloud::Point3D> points) {
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
        ++m_renderRevision;
        validateRenderSnapshot();
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

void PointCloudCanvas::setSelectionMode(bool enabled) {
        if (QThread::currentThread() != thread()) return;
        m_selectionMode = enabled;
        setToolTip(enabled ? tr("左键选择点，右键平移，Esc 取消，Backspace 撤销")
                           : tr("左键旋转，右键平移，滚轮缩放"));
        setFocus();
    }

void PointCloudCanvas::setEdgeSelectionMode(bool enabled, const QVector<int> &edgeIndices) {
        if (QThread::currentThread() != thread()) return;
        m_edgeSelectionMode = enabled;
        m_edgeIndices = edgeIndices;
        setToolTip(enabled ? tr("左键选择边缘真实点，Esc 取消")
                           : tr("左键旋转，右键平移，滚轮缩放"));
        setFocus();
    }

void PointCloudCanvas::setSelectedEdgeIndices(const QVector<int> &indices) {
        if (QThread::currentThread() != thread()) return;
        m_selectedEdgeIndices = indices;
        update();
    }

QVector<int> PointCloudCanvas::pickRectangleForSelection(const QRectF &selection) {
        return pickRectangle(selection);
    }

void PointCloudCanvas::setSelectedIndices(const QVector<int> &indices) {
        if (QThread::currentThread() != thread()) return;
        m_selectedIndices = indices;
        update();
    }

void PointCloudCanvas::setPlaneResult(const QVector<int> &planeIndices, const QVector<int> &edgeIndices,
                        const QVector<pcv::render::Contour> &contours) {
        if (QThread::currentThread() != thread()) return;
        QString renderError;
        if (!pcv::render::validateContours(contours, &renderError)) {
            qWarning() << "PointCloudCanvas rejected contour snapshot:" << renderError;
            return;
        }
        m_planeResultIndices = planeIndices;
        m_edgeResultIndices = edgeIndices;
        m_pointStates.fill(NormalPoint, m_points.size());
        for (int index : m_planeResultIndices)
            if (index >= 0 && index < m_pointStates.size()) m_pointStates[index] = PlanePoint;
        for (int index : m_edgeResultIndices)
            if (index >= 0 && index < m_pointStates.size()) m_pointStates[index] = EdgePoint;
        m_contourVertices.clear();
        m_contourRanges.clear();
        for (const pcv::render::Contour &contour : contours) {
            if (contour.points.size() < 2) continue;
            const int start = m_contourVertices.size();
            for (const QVector3D &point : contour.points)
                m_contourVertices.push_back({point.x(), point.y(), point.z()});
            m_contourRanges.push_back({start, int(contour.points.size())});
        }
        m_stateUploadPending = true;
        m_contourUploadPending = true;
        validateRenderSnapshot();
        update();
    }

void PointCloudCanvas::setExtractedPlane(const QVector<int> &planeIndices) {
        setPlaneResult(planeIndices, {}, {});
    }

void PointCloudCanvas::clearPlaneResult() {
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

void PointCloudCanvas::setWorkpieceCoordinateSystem(const pcv::render::CoordinateFrame &frame) {
        if (QThread::currentThread() != thread()) return;
        QString renderError;
        if (!pcv::render::validateCoordinateFrame(frame, &renderError)) {
            qWarning() << "PointCloudCanvas rejected coordinate frame:" << renderError;
            return;
        }
        m_workpieceCoordinate = frame;
        update();
    }

void PointCloudCanvas::clearWorkpieceCoordinateSystem() {
        if (QThread::currentThread() != thread()) return;
        m_workpieceCoordinate = {};
        update();
    }

void PointCloudCanvas::setPointSize(int size) {
        if (QThread::currentThread() != thread()) return;
        m_pointSize = qBound(1, size, 8);
        update();
    }

void PointCloudCanvas::setDisplayOptions(int colorMode, double overlay, double mapMin,
                           double mapMax) {
        if (QThread::currentThread() != thread()) return;
        m_colorMode = colorMode;
        m_overlay = float(qBound(0.0, overlay, 1.0));
        m_mapMin = float(mapMin);
        m_mapMax = float(mapMax);
        update();
    }

void PointCloudCanvas::resetView() {
        if (QThread::currentThread() != thread()) return;
        m_yaw = 38.0f;
        m_pitch = -28.0f;
        m_zoom = 1.0f;
        m_pan = QPointF();
        update();
    }

void PointCloudCanvas::initializeGL()  {
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

void PointCloudCanvas::paintGL()  {
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

void PointCloudCanvas::mousePressEvent(QMouseEvent *event)  {
        m_lastMousePosition = event->position();
        m_selectionOrigin = event->position();
        m_pressedButton = event->button();
        m_mouseMoved = false;
    }

void PointCloudCanvas::mouseMoveEvent(QMouseEvent *event)  {
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

void PointCloudCanvas::mouseReleaseEvent(QMouseEvent *event)  {
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

void PointCloudCanvas::resizeGL(int width, int height)  {
        Q_UNUSED(width)
        Q_UNUSED(height)
        m_pickingFbo.reset();
    }

void PointCloudCanvas::wheelEvent(QWheelEvent *event)  {
        m_zoom = qBound(0.08f,
                        m_zoom * (event->angleDelta().y() > 0 ? 1.15f : 0.87f),
                        30.0f);
        update();
    }

void PointCloudCanvas::validateRenderSnapshot() {
        pcv::render::RenderSnapshot snapshot;
        snapshot.points = m_points;
        snapshot.states.resize(m_pointStates.size());
        for (int i = 0; i < m_pointStates.size(); ++i) {
            snapshot.states[i] = m_pointStates[i] == PlanePoint
                ? pcv::render::PointState::Plane
                : (m_pointStates[i] == EdgePoint
                    ? pcv::render::PointState::Edge
                    : pcv::render::PointState::Normal);
        }
        snapshot.revision = m_renderRevision;
        QString error;
        if (!pcv::render::validateSnapshot(snapshot, &error)) {
            m_uploadError = error;
            qWarning() << "PointCloudCanvas render snapshot rejected:" << error;
        }
    }

void PointCloudCanvas::updateBounds() {
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

void PointCloudCanvas::uploadCloudIfNeeded() {
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

void PointCloudCanvas::uploadStatesIfNeeded() {
        if (!m_stateUploadPending || !m_stateBuffer.isCreated()) return;
        m_vertexArray.bind();
        m_stateBuffer.bind();
        m_stateBuffer.allocate(m_pointStates.constData(), int(m_pointStates.size()));
        m_stateBuffer.release();
        m_vertexArray.release();
        m_stateUploadPending = false;
    }

void PointCloudCanvas::uploadContoursIfNeeded() {
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

int PointCloudCanvas::pickPoint(const QPointF &position) {
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

QVector<int> PointCloudCanvas::pickRectangle(const QRectF &selection) {
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

QMatrix4x4 PointCloudCanvas::rotationTransform() const {
        QMatrix4x4 rotation;
        rotation.rotate(m_pitch, 1.0f, 0.0f, 0.0f);
        rotation.rotate(m_yaw, 0.0f, 0.0f, 1.0f);
        return rotation;
    }

QMatrix4x4 PointCloudCanvas::viewTransform() const {
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

void PointCloudCanvas::drawOverlay() {
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
