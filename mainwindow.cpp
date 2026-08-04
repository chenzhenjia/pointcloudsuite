#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFutureWatcher>
#include <QFileDialog>
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
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVector4D>
#include <QVector2D>
#include <QVector3D>
#include <QWheelEvent>
#include <QSignalBlocker>
#include <QCloseEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <limits>
#include <cmath>

class PointCloudCanvas final : public QOpenGLWidget,
                               protected QOpenGLFunctions_3_3_Core {
public:
    explicit PointCloudCanvas(QWidget *parent = nullptr)
        : QOpenGLWidget(parent), m_vertexBuffer(QOpenGLBuffer::VertexBuffer) {
        setMinimumSize(620, 480);
        setMouseTracking(true);
        setToolTip(tr("左键旋转，右键平移，滚轮缩放"));
    }

    ~PointCloudCanvas() override = default;

    void setCloud(const QVector<pointcloud::Point3D> &points) {
        m_points = points;
        updateBounds();
        m_uploadError.clear();
        m_uploadPending = true;
        update();
    }

    void setPointSize(int size) {
        m_pointSize = qBound(1, size, 8);
        update();
    }

    void setDisplayOptions(int colorMode, double overlay, double mapMin,
                           double mapMax) {
        m_colorMode = colorMode;
        m_overlay = float(qBound(0.0, overlay, 1.0));
        m_mapMin = float(mapMin);
        m_mapMax = float(mapMax);
        update();
    }

    void resetView() {
        m_yaw = 38.0f;
        m_pitch = -28.0f;
        m_zoom = 1.0f;
        m_pan = QPointF();
        update();
    }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
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
            out float heightValue;
            out vec3 normalValue;
            void main() {
                vec3 normalized = (vertexPosition - cloudCenter) * (2.0 / cloudSpan);
                gl_Position = transform * vec4(normalized, 1.0);
                gl_Position.xy += viewPan * gl_Position.w;
                gl_PointSize = pointSize;
                normalValue = vertexNormal;
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
            out vec4 fragmentColor;
            void main() {
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
                fragmentColor = vec4(color * light, max(0.02, overlay));
            }
        )";

        if (!m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader)
            || !m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader)
            || !m_program.link()) {
            m_initializationError = m_program.log();
            return;
        }

        if (!m_vertexArray.create() || !m_vertexBuffer.create()) {
            m_initializationError = tr("无法创建 OpenGL 顶点缓冲区");
            return;
        }
        m_uploadPending = true;
    }

    void paintGL() override {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (m_initializationError.isEmpty() && !m_points.isEmpty()) {
            uploadCloudIfNeeded();
            if (m_uploadError.isEmpty()) {
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
                m_vertexArray.bind();
                glDrawArrays(GL_POINTS, 0, int(m_points.size()));
                m_vertexArray.release();
                m_program.release();
            }
        }

        drawOverlay();
    }

    void mousePressEvent(QMouseEvent *event) override {
        m_lastMousePosition = event->position();
        m_pressedButton = event->button();
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        const QPointF delta = event->position() - m_lastMousePosition;
        m_lastMousePosition = event->position();
        if (m_pressedButton == Qt::LeftButton) {
            m_yaw += float(delta.x()) * 0.45f;
            m_pitch = qBound(-89.0f, m_pitch + float(delta.y()) * 0.45f, 89.0f);
        } else if (m_pressedButton == Qt::RightButton) {
            m_pan += QPointF(2.0 * delta.x() / qMax(1, width()),
                             -2.0 * delta.y() / qMax(1, height()));
        }
        update();
    }

    void mouseReleaseEvent(QMouseEvent *) override {
        m_pressedButton = Qt::NoButton;
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
            m_uploadPending = false;
            return;
        }

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
            m_uploadPending = false;
        } else {
            m_program.bind();
            m_program.enableAttributeArray(0);
            m_program.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(pointcloud::Point3D));
            m_program.enableAttributeArray(1);
            m_program.setAttributeBuffer(1, GL_FLOAT, int(3 * sizeof(float)), 3,
                                         sizeof(pointcloud::Point3D));
            m_program.release();
            m_uploadPending = false;
        }
        m_vertexBuffer.release();
        m_vertexArray.release();
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
            const QString coordinateText = tr("中心 (mm)  X %1  Y %2  Z %3")
                .arg(QString::number(m_center.x(), 'f', 2))
                .arg(QString::number(m_center.y(), 'f', 2))
                .arg(QString::number(m_center.z(), 'f', 2));
            const QString extentText = tr("范围 (mm)  %1 × %2 × %3")
                .arg(QString::number(m_spanX, 'f', 2))
                .arg(QString::number(m_spanY, 'f', 2))
                .arg(QString::number(m_spanZ, 'f', 2));
            painter.setPen(QColor(196, 207, 222));
            const int right = width() - 18;
            painter.drawText(right - painter.fontMetrics().horizontalAdvance(coordinateText),
                             27, coordinateText);
            painter.setPen(QColor(145, 160, 180));
            painter.drawText(right - painter.fontMetrics().horizontalAdvance(extentText),
                             49, extentText);
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
    QOpenGLShaderProgram m_program;
    QOpenGLVertexArrayObject m_vertexArray;
    QOpenGLBuffer m_vertexBuffer;
    QVector3D m_center;
    QPointF m_lastMousePosition;
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
};

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    buildUi();
}

MainWindow::~MainWindow() {
    if (m_loadWatcher && m_loadWatcher->isRunning()) {
        m_loadWatcher->waitForFinished();
    }
    if (m_noiseWatcher && m_noiseWatcher->isRunning()) {
        m_noiseWatcher->waitForFinished();
    }
    if (m_loadWatcher) {
        disconnect(m_loadWatcher, nullptr, this, nullptr);
        m_loadWatcher->disconnect(this);
    }
    if (m_noiseWatcher) {
        disconnect(m_noiseWatcher, nullptr, this, nullptr);
        m_noiseWatcher->disconnect(this);
    }
    if (m_canvas) {
        m_canvas->setUpdatesEnabled(false);
        m_canvas->hide();
    }
}

void MainWindow::buildUi() {
    setWindowTitle(tr("点云预览工作台"));
    resize(1400, 860);
    setMinimumSize(1080, 680);
    setStyleSheet(R"(
        QMainWindow, QWidget#workspace, QWidget { background: #101215; color: #e8edf4; }
        QMenuBar { background: #171a1f; color: #e8edf4; border-bottom: 1px solid #303640; }
        QMenuBar::item { padding: 7px 11px; background: transparent; }
        QMenuBar::item:selected, QMenu::item:selected { background: #244d73; }
        QMenu { background: #1b1f25; color: #e8edf4; border: 1px solid #3a424d; }
        QWidget#sidePanel { background: #171a1f; border: 1px solid #303640; }
        QLabel#sectionTitle { color: #a8b6c8; font-size: 12px; font-weight: 600; }
        QLabel#mainTitle { color: #f3f7fc; font-size: 20px; font-weight: 650; }
        QLabel#subTitle { color: #9aa8ba; }
        QPushButton { min-height: 32px; padding: 3px 12px; border: 1px solid #3d4652;
                      border-radius: 4px; background: #22272e; color: #e8edf4; }
        QPushButton:hover { border-color: #4e9bdd; background: #293b4c; }
        QPushButton#primaryButton { color: #ffffff; background: #1769aa; border-color: #1769aa; }
        QPushButton#primaryButton:hover { background: #2084cf; }
        QComboBox, QSpinBox, QDoubleSpinBox { min-height: 30px; padding: 0 7px; border: 1px solid #3d4652;
                             border-radius: 3px; background: #20252c; color: #e8edf4; }
        QComboBox QAbstractItemView { background: #20252c; color: #e8edf4; selection-background-color: #244d73; }
        QListWidget { border: 1px solid #303640; background: #111419; color: #dbe3ed; outline: 0; }
        QListWidget::item { min-height: 30px; padding: 3px 6px; }
        QListWidget::item:selected { background: #244d73; color: #ffffff; }
        QTabWidget::pane { border: 0; border-top: 1px solid #303640; }
        QTabBar::tab { padding: 9px 17px; background: transparent; color: #9aa8ba; }
        QTabBar::tab:selected { color: #66b5ff; border-bottom: 2px solid #2e91d8; }
        QCheckBox { color: #dbe3ed; spacing: 6px; }
        QStatusBar { background: #171a1f; border-top: 1px solid #303640; color: #a8b6c8; }
        QSplitter::handle { background: #101215; width: 6px; }
    )");

    auto *fileMenu = menuBar()->addMenu(tr("文件"));
    auto *openAction = fileMenu->addAction(tr("打开 PLY..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openPointCloud);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出"), qApp, &QApplication::quit);
    auto *featureMenu = menuBar()->addMenu(tr("功能"));
    auto addRatioAction = [this, featureMenu](const QString &text, int denominator, const QKeySequence &shortcut = {}) {
        auto *action = featureMenu->addAction(text);
        action->setCheckable(true);
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, [this, denominator]() { setDownsampleRatio(denominator); });
        m_ratioActions.push_back(action);
        return action;
    };
    addRatioAction(tr("降采样：关闭"), 1, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_0));
    addRatioAction(tr("降采样：1/2"), 2, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_2));
    addRatioAction(tr("降采样：1/4"), 4, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_4));
    addRatioAction(tr("降采样：1/8"), 8, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_8));
    addRatioAction(tr("降采样：1/16"), 16);
    m_ratioActions.first()->setChecked(true);
    auto *debugMenu = menuBar()->addMenu(tr("调试"));
    debugMenu->addAction(tr("显示运行状态"), this, [this]() {
        statusBar()->showMessage(tr("缓存：后台加载 · 渲染：OpenGL 全量直接标记 · 降采样：功能菜单"), 6000);
    });
    debugMenu->addAction(tr("显示 OpenGL 信息"), this, [this]() {
        QMessageBox::information(this, tr("调试信息"),
                                 tr("渲染后端：OpenGL\n点绘制：GL_POINTS 直接标记\n缓存：.pcvbin\nOctree LOD：已回滚"));
    });

    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("workspace"));
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(12, 10, 12, 8);
    layout->setSpacing(10);

    auto *header = new QHBoxLayout;
    auto *titleColumn = new QVBoxLayout;
    titleColumn->setSpacing(1);
    auto *title = new QLabel(tr("点云预览工作台"));
    title->setObjectName(QStringLiteral("mainTitle"));
    auto *subtitle = new QLabel(tr("PLY  ·  3D VIEW"));
    subtitle->setObjectName(QStringLiteral("subTitle"));
    titleColumn->addWidget(title);
    titleColumn->addWidget(subtitle);
    header->addLayout(titleColumn);
    header->addStretch();
    auto *openButton = new QPushButton(tr("打开 PLY"));
    openButton->setObjectName(QStringLiteral("primaryButton"));
    openButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openPointCloud);
    header->addWidget(openButton);
    layout->addLayout(header);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter, 1);

    auto *leftPanel = new QWidget;
    leftPanel->setObjectName(QStringLiteral("sidePanel"));
    leftPanel->setMinimumWidth(220);
    leftPanel->setMaximumWidth(310);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(14, 14, 14, 14);
    leftLayout->setSpacing(9);
    auto *fileTitle = new QLabel(tr("数据源"));
    fileTitle->setObjectName(QStringLiteral("sectionTitle"));
    leftLayout->addWidget(fileTitle);
    m_fileList = new QListWidget;
    leftLayout->addWidget(m_fileList, 1);
    m_fileInfo = new QLabel(tr("尚未加载点云"));
    m_fileInfo->setWordWrap(true);
    m_fileInfo->setObjectName(QStringLiteral("subTitle"));
    leftLayout->addWidget(m_fileInfo);
    splitter->addWidget(leftPanel);

    auto *center = new QWidget;
    auto *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(6);
    m_canvas = new PointCloudCanvas;
    centerLayout->addWidget(m_canvas, 1);
    m_canvasInfo = new QLabel(tr("就绪"));
    m_canvasInfo->setObjectName(QStringLiteral("subTitle"));
    centerLayout->addWidget(m_canvasInfo);
    splitter->addWidget(center);

    auto *rightPanel = new QWidget;
    rightPanel->setObjectName(QStringLiteral("sidePanel"));
    rightPanel->setMinimumWidth(270);
    rightPanel->setMaximumWidth(340);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(10, 8, 10, 12);
    auto *tabs = new QTabWidget;

    auto *renderPage = new QWidget;
    auto *renderForm = new QFormLayout(renderPage);
    renderForm->setContentsMargins(8, 14, 8, 8);
    renderForm->setVerticalSpacing(12);
    m_pointSize = new QSpinBox;
    m_pointSize->setRange(1, 8);
    m_pointSize->setValue(1);
    m_pointSize->setToolTip(tr("设置直接像素标记的尺寸，1 表示单像素绘制"));
    renderForm->addRow(tr("像素标记尺寸"), m_pointSize);
    connect(m_pointSize, qOverload<int>(&QSpinBox::valueChanged),
            m_canvas, &PointCloudCanvas::setPointSize);
    auto *resetViewButton = new QPushButton(tr("重置视角"));
    resetViewButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    resetViewButton->setToolTip(tr("恢复默认三维视角、缩放和平移"));
    connect(resetViewButton, &QPushButton::clicked, m_canvas, &PointCloudCanvas::resetView);
    renderForm->addRow(resetViewButton);
    auto *renderTitle = new QLabel(tr("渲染效果"));
    renderTitle->setObjectName(QStringLiteral("sectionTitle"));
    renderForm->addRow(renderTitle);
    m_colorMode = new QComboBox;
    m_colorMode->addItems({tr("彩色高度"), tr("灰阶"), tr("法向量")});
    m_colorMode->setToolTip(tr("选择点云颜色映射方式"));
    renderForm->addRow(tr("颜色样式"), m_colorMode);
    m_overlay = new QDoubleSpinBox;
    m_overlay->setRange(0.0, 1.0);
    m_overlay->setSingleStep(0.05);
    m_overlay->setDecimals(3);
    m_overlay->setValue(1.0);
    m_overlay->setToolTip(tr("设置点云叠加透明度，范围 0.000–1.000"));
    renderForm->addRow(tr("叠加比例"), m_overlay);
    m_mapMin = new QDoubleSpinBox;
    m_mapMin->setRange(-1.0e9, 1.0e9);
    m_mapMin->setDecimals(2);
    m_mapMin->setToolTip(tr("设置高度颜色映射的最小值，单位 mm"));
    renderForm->addRow(tr("映射最小值"), m_mapMin);
    m_mapMax = new QDoubleSpinBox;
    m_mapMax->setRange(-1.0e9, 1.0e9);
    m_mapMax->setDecimals(2);
    m_mapMax->setToolTip(tr("设置高度颜色映射的最大值，单位 mm"));
    renderForm->addRow(tr("映射最大值"), m_mapMax);
    connect(m_colorMode, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::updateRenderSettings);
    connect(m_overlay, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MainWindow::updateRenderSettings);
    connect(m_mapMin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MainWindow::updateRenderSettings);
    connect(m_mapMax, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MainWindow::updateRenderSettings);
    tabs->addTab(renderPage, tr("显示"));
    auto *dataPage = new QWidget;
    auto *dataForm = new QFormLayout(dataPage);
    dataForm->setContentsMargins(8, 14, 8, 8);
    dataForm->setVerticalSpacing(12);
    auto *dataTitle = new QLabel(tr("数据处理"));
    dataTitle->setObjectName(QStringLiteral("sectionTitle"));
    dataForm->addRow(dataTitle);
    m_ratio = new QComboBox;
    m_ratio->addItems({tr("关闭（100%）"), tr("1/2（50%）"), tr("1/4（25%）"),
                       tr("1/8（12.5%）"), tr("1/16（6.25%）")});
    m_ratio->setToolTip(tr("按固定比例减少工作点云；原始点云保留，可随时切换"));
    dataForm->addRow(tr("降采样比例"), m_ratio);
    connect(m_ratio, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) { setDownsampleRatio(1 << index); });
    dataForm->addRow(new QLabel(tr("改变比例后立即更新显示，原始点云保持不变")));
    tabs->addTab(dataPage, tr("数据"));
    auto *debugPage = new QWidget;
    auto *debugForm = new QFormLayout(debugPage);
    debugForm->setContentsMargins(8, 14, 8, 8);
    auto *debugTitle = new QLabel(tr("调试状态"));
    debugTitle->setObjectName(QStringLiteral("sectionTitle"));
    debugForm->addRow(debugTitle);
    debugForm->addRow(new QLabel(tr("加载：后台线程 + .pcvbin 缓存")));
    debugForm->addRow(new QLabel(tr("绘制：OpenGL GL_POINTS 全量直接标记")));
    debugForm->addRow(new QLabel(tr("显示层：不使用 Octree LOD")));
    tabs->addTab(debugPage, tr("调试"));
    auto *cleanPage = new QWidget;
    auto *cleanLayout = new QVBoxLayout(cleanPage);
    cleanLayout->setContentsMargins(8, 14, 8, 8);
    auto *cleanTitle = new QLabel(tr("点云清理 · 后台处理"));
    cleanTitle->setObjectName(QStringLiteral("sectionTitle"));
    cleanLayout->addWidget(cleanTitle);
    m_voxelNoise = new QCheckBox(tr("体素降采样（预处理）"));
    m_voxelNoise->setChecked(true);
    m_voxelNoise->setToolTip(tr("统一点间距并降低邻域计算量；不等同于去噪"));
    cleanLayout->addWidget(m_voxelNoise);
    auto *voxelRow = new QHBoxLayout;
    voxelRow->addWidget(new QLabel(tr("体素尺寸 mm")));
    m_voxelSize = new QDoubleSpinBox;
    m_voxelSize->setRange(0.001, 1000.0); m_voxelSize->setDecimals(3); m_voxelSize->setValue(0.25);
    voxelRow->addWidget(m_voxelSize); cleanLayout->addLayout(voxelRow);
    m_statisticalNoise = new QCheckBox(tr("统计离群值去除（推荐）"));
    m_statisticalNoise->setChecked(true);
    m_statisticalNoise->setToolTip(tr("按邻域平均距离剔除孤立飞点"));
    cleanLayout->addWidget(m_statisticalNoise);
    auto *statRow = new QHBoxLayout;
    statRow->addWidget(new QLabel(tr("邻域 K"))); m_meanK = new QSpinBox;
    m_meanK->setRange(4, 128); m_meanK->setValue(45); statRow->addWidget(m_meanK);
    statRow->addWidget(new QLabel(tr("阈值倍数"))); m_stddev = new QDoubleSpinBox;
    m_stddev->setRange(0.1, 5.0); m_stddev->setSingleStep(0.1); m_stddev->setValue(1.3); statRow->addWidget(m_stddev);
    cleanLayout->addLayout(statRow);
    m_radiusNoise = new QCheckBox(tr("半径滤波去除（可选）"));
    m_radiusNoise->setToolTip(tr("要求指定半径内存在足够邻居，适合清理局部稀疏点"));
    cleanLayout->addWidget(m_radiusNoise);
    auto *radiusRow = new QHBoxLayout;
    radiusRow->addWidget(new QLabel(tr("半径 mm"))); m_radius = new QDoubleSpinBox;
    m_radius->setRange(0.001, 1000.0); m_radius->setDecimals(3); m_radius->setValue(1.00); radiusRow->addWidget(m_radius);
    radiusRow->addWidget(new QLabel(tr("最少邻居"))); m_minNeighbors = new QSpinBox;
    m_minNeighbors->setRange(1, 128); m_minNeighbors->setValue(10); radiusRow->addWidget(m_minNeighbors);
    cleanLayout->addLayout(radiusRow);
    m_noiseApply = new QPushButton(tr("应用噪点去除"));
    m_noiseApply->setToolTip(tr("后台执行当前勾选的串联流程，原始点云可恢复"));
    cleanLayout->addWidget(m_noiseApply);
    auto *restoreButton = new QPushButton(tr("恢复原始点云"));
    restoreButton->setToolTip(tr("撤销噪点去除和降采样，恢复完整原始点云"));
    cleanLayout->addWidget(restoreButton);
    cleanLayout->addStretch();
    connect(m_noiseApply, &QPushButton::clicked, this, &MainWindow::applyNoiseRemoval);
    connect(restoreButton, &QPushButton::clicked, this, [this]() {
        m_filteredPoints.clear(); m_downsampleDenominator = 1; refreshDisplayCloud(); statusBar()->showMessage(tr("已恢复原始点云"));
    });
    tabs->addTab(cleanPage, tr("点云清理"));

    rightLayout->addWidget(tabs, 1);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({245, 850, 290});

    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(3);
    m_progress->hide();
    layout->addWidget(m_progress);

    setCentralWidget(root);
    statusBar()->showMessage(tr("就绪"));
}

void MainWindow::openPointCloud() {
    if (m_loading) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开 PLY"), QString(), tr("PLY 文件 (*.ply);;所有文件 (*.*)"));
    if (path.isEmpty()) return;

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
    m_loadWatcher->setFuture(QtConcurrent::run(pointcloud::loadPlyCachedResult, path));
}

void MainWindow::loadFinished() {
    const pointcloud::LoadResult result = m_loadWatcher->result();
    if (!result.ok) {
        m_loading = false;
        m_progress->hide();
        QMessageBox::critical(this, tr("打开失败"), result.error);
        statusBar()->showMessage(tr("加载失败"));
        return;
    }
    m_rawPoints = result.points;
    m_filteredPoints.clear();
    float minZ = m_rawPoints.first().z;
    float maxZ = minZ;
    for (const auto &point : m_rawPoints) {
        minZ = qMin(minZ, point.z);
        maxZ = qMax(maxZ, point.z);
    }
    m_mapMin->setValue(minZ);
    m_mapMax->setValue(maxZ > minZ ? maxZ : minZ + 1.0);
    refreshDisplayCloud();

    const QFileInfo fileInfo(m_pendingPath);
    m_fileList->clear();
    m_fileList->addItem(fileInfo.fileName());
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
    statusBar()->showMessage(result.usedCache ? tr("缓存加载完成") : tr("加载完成，已建立二进制缓存"));
}

void MainWindow::downsample() {
    if (m_loading) return;
    if (m_rawPoints.isEmpty()) {
        statusBar()->showMessage(tr("比例已设置，将在加载点云后生效"));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_points = pointcloud::proportionalDownsample(m_rawPoints, m_downsampleDenominator);
    refreshDisplayCloud();
    QApplication::restoreOverrideCursor();

    statusBar()->showMessage(tr("比例已更新：%1 → %2 点，显示层无抽样")
                                 .arg(QLocale().toString(m_rawPoints.size()))
                                 .arg(QLocale().toString(m_points.size())));
}

void MainWindow::updateRenderSettings() {
    if (!m_canvas) return;
    m_canvas->setDisplayOptions(m_colorMode->currentIndex(), m_overlay->value(),
                                m_mapMin->value(), m_mapMax->value());
}

void MainWindow::refreshDisplayCloud() {
    if (m_rawPoints.isEmpty()) return;
    const QVector<pointcloud::Point3D> &source = m_filteredPoints.isEmpty() ? m_rawPoints : m_filteredPoints;
    m_points = pointcloud::proportionalDownsample(source, m_downsampleDenominator);
    m_canvas->setCloud(m_points);
    if (m_canvasInfo) m_canvasInfo->setText(tr("当前显示 %1 个点  ·  原始 %2 个点  ·  全量直接标记")
                          .arg(QLocale().toString(m_points.size()))
                          .arg(QLocale().toString(m_rawPoints.size())));
}

void MainWindow::setDownsampleRatio(int denominator) {
    m_downsampleDenominator = qMax(1, denominator);
    const int index = denominator <= 1 ? 0 : qBound(0, qRound(std::log2(double(denominator))), 4);
    if (m_ratio && m_ratio->currentIndex() != index) {
        QSignalBlocker blocker(m_ratio);
        m_ratio->setCurrentIndex(index);
    }
    for (int i = 0; i < m_ratioActions.size(); ++i)
        m_ratioActions[i]->setChecked((1 << i) == m_downsampleDenominator);
    downsample();
    statusBar()->showMessage(tr("功能：降采样比例已切换为 1/%1").arg(m_downsampleDenominator));
}

void MainWindow::applyNoiseRemoval() {
    if (m_loading || m_rawPoints.isEmpty()) {
        statusBar()->showMessage(tr("请先完成点云加载"));
        return;
    }
    if (m_noiseWatcher && m_noiseWatcher->isRunning()) return;
    pointcloud::NoiseOptions options;
    options.voxelEnabled = m_voxelNoise->isChecked(); options.voxelSize = float(m_voxelSize->value());
    options.statisticalEnabled = m_statisticalNoise->isChecked(); options.meanK = m_meanK->value();
    options.stddevMultiplier = float(m_stddev->value()); options.radiusEnabled = m_radiusNoise->isChecked();
    options.radius = float(m_radius->value()); options.minNeighbors = m_minNeighbors->value();
    if (!m_noiseWatcher) {
        m_noiseWatcher = new QFutureWatcher<pointcloud::NoiseResult>(this);
        connect(m_noiseWatcher, &QFutureWatcher<pointcloud::NoiseResult>::finished,
                this, &MainWindow::noiseFinished);
    }
    m_noiseApply->setEnabled(false);
    m_progress->show(); m_progress->setRange(0, 0);
    statusBar()->showMessage(tr("后台执行噪点去除..."));
    const QVector<pointcloud::Point3D> source = m_filteredPoints.isEmpty() ? m_rawPoints : m_filteredPoints;
    m_noiseInputCount = source.size();
    m_noiseWatcher->setFuture(QtConcurrent::run([source, options]() {
        return pointcloud::removeNoise(source, options);
    }));
}

void MainWindow::noiseFinished() {
    const pointcloud::NoiseResult result = m_noiseWatcher->result();
    m_noiseApply->setEnabled(true); m_progress->hide();
    if (!result.ok) {
        QMessageBox::warning(this, tr("噪点去除失败"), result.error);
        statusBar()->showMessage(tr("噪点去除未改变当前点云"));
        return;
    }
    m_filteredPoints = result.points;
    m_points = pointcloud::proportionalDownsample(m_filteredPoints, m_downsampleDenominator);
    m_canvas->setCloud(m_points);
    m_canvasInfo->setText(tr("清理后显示 %1 点 · 原始 %2 点")
                          .arg(QLocale().toString(m_points.size()))
                          .arg(QLocale().toString(m_rawPoints.size())));
    statusBar()->showMessage(tr("当前处理层完成：%1 → %2 点；后续处理将基于此结果")
                             .arg(QLocale().toString(m_noiseInputCount))
                             .arg(QLocale().toString(result.points.size())));
}

void MainWindow::closeEvent(QCloseEvent *event) {
    setEnabled(false);
    if (m_canvas) {
        m_canvas->setUpdatesEnabled(false);
        m_canvas->hide();
    }
    if (m_loading && m_loadWatcher && m_loadWatcher->isRunning()) {
        statusBar()->showMessage(tr("正在结束后台加载，请稍候..."));
        m_loadWatcher->waitForFinished();
        m_loading = false;
    }
    if (m_noiseWatcher && m_noiseWatcher->isRunning()) {
        statusBar()->showMessage(tr("正在结束后台噪点处理，请稍候..."));
        m_noiseWatcher->waitForFinished();
    }
    event->accept();
}
