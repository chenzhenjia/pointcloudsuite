#include "mainwindow.h"
#include "renderdialog.h"
#include "process_memory_usage_v1.h"
#ifdef PCV_BUILD_EXPERIMENTAL_CAMERA
#include "cameradialog.h"
#endif
#include <stitchingwindow.h>
#include "selected_plane_launcher.h"
#include "ui_mainwindow.h"
#include <pcv/interface/temp_workpiece_interface.h>
#include <pcv/io/artifact_transaction.h>
#include <pcv/output/plane_output.h>
#include <pcv/registration/handeye_transform.h>
#include <pcv/render/pointcloud_canvas.h>
#include <pcv/render/pointcloud_canvas_contract.h>

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
#include <QToolButton>
#include <QTabBar>
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
#include <QTimer>
#include <QDialog>
#include <QSignalBlocker>
#include <QtMath>
#include <QDateTime>
#include <QDir>
#include <QGridLayout>
#include <QPixmap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <atomic>
#include <limits>
#include <cmath>
#include <functional>
#include <memory>

namespace {
pcv::render::CoordinateFrame toRenderCoordinateFrame(
    const pointcloud::WorkpieceCoordinateSystem &frame) {
    pcv::render::CoordinateFrame result;
    result.originInRobotBase = frame.originInRobotBase;
    result.axisXInRobotBase = frame.axisXInRobotBase;
    result.axisYInRobotBase = frame.axisYInRobotBase;
    result.axisZInRobotBase = frame.axisZInRobotBase;
    result.workpieceToRobotBase = frame.workpieceToRobotBase;
    result.robotBaseToWorkpiece = frame.robotBaseToWorkpiece;
    result.poseA = frame.poseA;
    result.poseB = frame.poseB;
    result.poseC = frame.poseC;
    result.valid = frame.valid;
    return result;
}

QVector<pcv::render::Contour> toRenderContours(
    const QVector<pointcloud::PlaneContour> &contours) {
    QVector<pcv::render::Contour> result;
    result.reserve(contours.size());
    for (const pointcloud::PlaneContour &contour : contours) {
        pcv::render::Contour renderContour;
        renderContour.hole = contour.hole;
        renderContour.points.reserve(contour.points.size());
        for (const pointcloud::Point3D &point : contour.points)
            renderContour.points.push_back(QVector3D(point.x, point.y, point.z));
        result.push_back(std::move(renderContour));
    }
    return result;
}

// Plain-PLY line: values collected by the "PLY 线路参数" dialog.
struct PlyLineParameters {
    QString pointCloudFile;
    QString calibrationFile;
    QString outputDirectory;
    pointcloud::RobotPose startPose;
    pointcloud::RobotPose endPose;
};

QString normalizedAbsolutePath(const QString &path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QStringList workpieceArtifactNames() {
    return {QStringLiteral("baseline_robot_base.ply"),
            QStringLiteral("roi_template_robot_base.ply"),
            QStringLiteral("plane_mask.png"),
            QStringLiteral("temp_workpiece_info.json")};
}

// Returns the names of the formal four-artifact files that already exist in
// the selected directory. The plain-PLY line refuses to continue instead of
// asking per file during the automatic run.
QStringList existingWorkpieceArtifacts(const QString &directory) {
    QStringList existing;
    const QDir dir(normalizedAbsolutePath(directory));
    for (const QString &name : workpieceArtifactNames()) {
        if (QFileInfo::exists(dir.filePath(name))) existing.push_back(name);
    }
    return existing;
}

// The output directory must be a real user directory: never a drive root, a
// cache/build tree or the source tree itself.
QString plyLineOutputDirectoryIssue(const QString &directory) {
    const QString clean = normalizedAbsolutePath(directory);
    if (clean.isEmpty()) return QObject::tr("请选择输出目录");
    const QFileInfo info(clean);
    if (info.exists() && !info.isDir())
        return QObject::tr("输出目录不是文件夹：%1").arg(clean);
    if (QDir(clean).isRoot())
        return QObject::tr("输出目录不能是盘符根目录：%1").arg(clean);
    const QStringList components = clean.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &component : components) {
        const QString lower = component.toLower();
        const bool reserved = lower.startsWith(QStringLiteral("build"))
            || lower.startsWith(QStringLiteral("cmake-build"))
            || lower.startsWith(QStringLiteral(".pcv"))
            || lower == QStringLiteral("cache") || lower == QStringLiteral(".cache")
            || lower == QStringLiteral("cmakefiles")
            || lower == QStringLiteral("runtime_data");
        if (reserved)
            return QObject::tr("输出目录不能位于缓存或构建目录中：%1").arg(clean);
    }
    if (QFileInfo::exists(QDir(clean).filePath(QStringLiteral("CMakeCache.txt"))))
        return QObject::tr("输出目录是 CMake 构建目录：%1").arg(clean);
    QDir ancestor(clean);
    while (true) {
        if (QFileInfo::exists(ancestor.filePath(QStringLiteral("CMakeLists.txt")))
            && QDir(ancestor.filePath(QStringLiteral("modules"))).exists()
            && QDir(ancestor.filePath(QStringLiteral("apps"))).exists())
            return QObject::tr("输出目录位于源码树中：%1").arg(clean);
        if (!ancestor.cdUp()) break;
    }
    return QString();
}

pcv::roi::WorkpieceCoordinateSystem toScanCoordinateFrame(
    const pointcloud::WorkpieceCoordinateSystem &frame) {
    pcv::roi::WorkpieceCoordinateSystem result;
    result.originInRobotBase = frame.originInRobotBase;
    result.axisXInRobotBase = frame.axisXInRobotBase;
    result.axisYInRobotBase = frame.axisYInRobotBase;
    result.axisZInRobotBase = frame.axisZInRobotBase;
    result.workpieceToRobotBase = frame.workpieceToRobotBase;
    result.robotBaseToWorkpiece = frame.robotBaseToWorkpiece;
    result.poseA = frame.poseA;
    result.poseB = frame.poseB;
    result.poseC = frame.poseC;
    result.orthogonalityError = frame.orthogonalityError;
    result.error = frame.error;
    result.valid = frame.valid;
    return result;
}

bool poseIsFinite(const pointcloud::RobotPose &pose) {
    const double values[6] = {pose.x, pose.y, pose.z, pose.rx, pose.ry, pose.rz};
    for (const double value : values) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

// Collects the plain-PLY hand-eye parameters. The dialog validates the
// calibration XML (load + rigid transform check) and the output directory on
// every change and only enables OK when the whole parameter set is usable.
bool collectPlyLineParameters(QWidget *parent, const QString &plyPath,
                              PlyLineParameters *parameters) {
    if (!parameters) return false;
    const QFileInfo plyInfo(plyPath);
    if (!plyInfo.exists() || !plyInfo.isFile()) {
        QMessageBox::warning(parent, QObject::tr("PLY 线路参数"),
                             QObject::tr("PLY 文件不存在：%1").arg(plyPath));
        return false;
    }
    const QString plyAbsolute = normalizedAbsolutePath(plyPath);
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("PLY 线路参数"));
    dialog.setModal(true);
    auto *form = new QFormLayout(&dialog);

    auto *plyValue = new QLabel(plyAbsolute, &dialog);
    plyValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    plyValue->setWordWrap(true);
    form->addRow(QObject::tr("点云 PLY"), plyValue);

    auto *xmlRow = new QWidget(&dialog);
    auto *xmlLayout = new QHBoxLayout(xmlRow);
    xmlLayout->setContentsMargins(0, 0, 0, 0);
    auto *xmlValue = new QLabel(QObject::tr("未选择"), xmlRow);
    xmlValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    xmlValue->setWordWrap(true);
    auto *chooseXml = new QPushButton(QObject::tr("选择 XML..."), xmlRow);
    xmlLayout->addWidget(xmlValue, 1);
    xmlLayout->addWidget(chooseXml);
    form->addRow(QObject::tr("手眼标定 XML"), xmlRow);

    constexpr double kLinearLimit = 1.0e7;
    constexpr double kAngularLimit = 3.6e4;
    const auto makeSpin = [&dialog](double minimum, double maximum, int decimals) {
        auto *spin = new QDoubleSpinBox(&dialog);
        spin->setRange(minimum, maximum);
        spin->setDecimals(decimals);
        spin->setSingleStep(decimals > 4 ? 0.1 : 1.0);
        spin->setKeyboardTracking(false);
        return spin;
    };
    const auto makePoseRow = [&](const QString &title, QVector<QDoubleSpinBox *> *values) {
        auto *row = new QWidget(&dialog);
        auto *grid = new QGridLayout(row);
        grid->setContentsMargins(0, 0, 0, 0);
        const QStringList names{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z"),
                                QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
        for (int index = 0; index < 6; ++index) {
            const bool linear = index < 3;
            auto *spin = makeSpin(linear ? -kLinearLimit : -kAngularLimit,
                                  linear ? kLinearLimit : kAngularLimit,
                                  linear ? 4 : 6);
            grid->addWidget(new QLabel(names.at(index), row), index / 3, (index % 3) * 2);
            grid->addWidget(spin, index / 3, (index % 3) * 2 + 1);
            values->push_back(spin);
        }
        form->addRow(title, row);
    };
    QVector<QDoubleSpinBox *> startValues;
    QVector<QDoubleSpinBox *> endValues;
    makePoseRow(QObject::tr("Start 位姿 [X,Y,Z,A,B,C]"), &startValues);
    makePoseRow(QObject::tr("End 位姿 [X,Y,Z,A,B,C]"), &endValues);

    auto *poseHint = new QLabel(QObject::tr("单位：X/Y/Z mm，A/B/C deg；A=Rx、B=Ry、C=Rz；R = Rz(C) × Ry(B) × Rx(A)"),
                                &dialog);
    poseHint->setWordWrap(true);
    form->addRow(QString(), poseHint);

    form->addRow(QObject::tr("点云布局"),
                 new QLabel(QStringLiteral("LineProfileXz"), &dialog));
    form->addRow(QObject::tr("坐标方向"),
                 new QLabel(QStringLiteral("camera -> robot_base"), &dialog));
    auto *matrixValue = new QLabel(&dialog);
    matrixValue->setWordWrap(true);
    matrixValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QObject::tr("手眼矩阵校验"), matrixValue);

    QString outputDirectory = plyInfo.absolutePath();
    auto *outputRow = new QWidget(&dialog);
    auto *outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    auto *outputValue = new QLabel(outputDirectory, outputRow);
    outputValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outputValue->setWordWrap(true);
    auto *chooseOutput = new QPushButton(QObject::tr("选择目录..."), outputRow);
    outputLayout->addWidget(outputValue, 1);
    outputLayout->addWidget(chooseOutput);
    form->addRow(QObject::tr("输出目录"), outputRow);

    auto *issueValue = new QLabel(&dialog);
    issueValue->setWordWrap(true);
    form->addRow(QObject::tr("校验结果"), issueValue);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("确认并转换"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    QString calibrationPath;
    const auto currentPose = [](const QVector<QDoubleSpinBox *> &values) {
        pointcloud::RobotPose pose;
        pose.x = values.at(0)->value();
        pose.y = values.at(1)->value();
        pose.z = values.at(2)->value();
        pose.rx = values.at(3)->value();
        pose.ry = values.at(4)->value();
        pose.rz = values.at(5)->value();
        return pose;
    };
    const auto refresh = [&]() {
        QString problem;
        // Every XML change runs loadHandEyeCalibration() and
        // validateRigidTransform() before the parameters may be accepted.
        if (calibrationPath.isEmpty()) {
            matrixValue->setText(QObject::tr("未校验：请选择手眼标定 XML"));
            matrixValue->setStyleSheet(QStringLiteral("color:#a06000;"));
            problem = QObject::tr("请选择手眼标定 XML 文件");
        } else if (!QFileInfo::exists(calibrationPath)) {
            const QString detail = QObject::tr("文件不存在：%1").arg(calibrationPath);
            matrixValue->setText(QObject::tr("无效：%1").arg(detail));
            matrixValue->setStyleSheet(QStringLiteral("color:#a00000;"));
            problem = QObject::tr("手眼标定 XML 无效：%1").arg(detail);
        } else {
            pointcloud::HandEyeCalibration calibration;
            QString calibrationError;
            if (!pointcloud::loadHandEyeCalibration(calibrationPath, &calibration,
                                                    &calibrationError)) {
                matrixValue->setText(QObject::tr("无效：%1；%2")
                                         .arg(calibrationPath, calibrationError));
                matrixValue->setStyleSheet(QStringLiteral("color:#a00000;"));
                problem = QObject::tr("手眼标定 XML 无效：%1").arg(calibrationError);
            } else {
                QString rigidError;
                if (!pointcloud::validateRigidTransform(calibration.flangeFromDepth,
                                                        &rigidError)) {
                    matrixValue->setText(QObject::tr("无效（非刚体变换）：%1；%2")
                                             .arg(calibrationPath, rigidError));
                    matrixValue->setStyleSheet(QStringLiteral("color:#a00000;"));
                    problem = QObject::tr("手眼矩阵不是有效刚体变换：%1").arg(rigidError);
                } else {
                    matrixValue->setText(QObject::tr("有效（camera -> robot_base）：%1")
                                             .arg(calibrationPath));
                    matrixValue->setStyleSheet(QStringLiteral("color:#206020;"));
                }
            }
        }
        if (problem.isEmpty()
            && (!poseIsFinite(currentPose(startValues)) || !poseIsFinite(currentPose(endValues))))
            problem = QObject::tr("Start/End 位姿必须全部为有限数值");
        if (problem.isEmpty()) problem = plyLineOutputDirectoryIssue(outputDirectory);
        if (problem.isEmpty()) {
            const QStringList existing = existingWorkpieceArtifacts(outputDirectory);
            if (!existing.isEmpty())
                problem = QObject::tr("输出目录已存在正式文件，请更换目录：%1")
                              .arg(existing.join(QStringLiteral("、")));
        }
        issueValue->setText(problem.isEmpty()
            ? QObject::tr("参数校验通过：可以执行 PLY -> robot_base 手眼转换")
            : problem);
        issueValue->setStyleSheet(problem.isEmpty() ? QStringLiteral("color:#206020;")
                                                    : QStringLiteral("color:#a00000;"));
        if (buttons->button(QDialogButtonBox::Ok))
            buttons->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
    };

    QObject::connect(chooseXml, &QPushButton::clicked, &dialog, [&]() {
        const QString selected = QFileDialog::getOpenFileName(
            &dialog, QObject::tr("选择手眼标定 XML"),
            calibrationPath.isEmpty() ? plyInfo.absolutePath() : calibrationPath,
            QObject::tr("XML 文件 (*.xml);;所有文件 (*.*)"));
        if (selected.isEmpty()) return;
        calibrationPath = normalizedAbsolutePath(selected);
        xmlValue->setText(calibrationPath);
        refresh();
    });
    QObject::connect(chooseOutput, &QPushButton::clicked, &dialog, [&]() {
        const QString selected = QFileDialog::getExistingDirectory(
            &dialog, QObject::tr("选择四件套输出目录"), outputDirectory);
        if (selected.isEmpty()) return;
        outputDirectory = normalizedAbsolutePath(selected);
        outputValue->setText(outputDirectory);
        refresh();
    });
    for (QDoubleSpinBox *spin : startValues)
        QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
                         [&refresh](double) { refresh(); });
    for (QDoubleSpinBox *spin : endValues)
        QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
                         [&refresh](double) { refresh(); });

    refresh();
    dialog.resize(760, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted) return false;
    parameters->pointCloudFile = plyAbsolute;
    parameters->calibrationFile = calibrationPath;
    parameters->outputDirectory = normalizedAbsolutePath(outputDirectory);
    parameters->startPose = currentPose(startValues);
    parameters->endPose = currentPose(endValues);
    return true;
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : MainWindow(pcv::config::PointCloudViewDefaults{}, parent) {}

MainWindow::MainWindow(const pcv::config::PointCloudViewDefaults &defaults, QWidget *parent)
    : QMainWindow(parent), m_defaults(defaults) {
    buildUi();
    updateMemoryUsage();
    m_memoryTimer->start();
}

MainWindow::~MainWindow() {
    // Stop every completion signal before waiting.  QObject children are
    // destroyed after MainWindow members, so allowing a queued watcher slot
    // to run during teardown can access vectors/widgets whose lifetime has
    // already ended.
    m_closing = true;
    cancelAndWaitWorkpieceTemplate();
    if (m_memoryTimer) m_memoryTimer->stop();
    if (m_planeCancelToken) m_planeCancelToken->store(true);
    if (m_prepareCancelToken) m_prepareCancelToken->store(true);
    if (m_scanPlanningCancelToken) m_scanPlanningCancelToken->store(true);
    if (m_textureCancelToken) m_textureCancelToken->store(true);
    if (m_loadWatcher) disconnect(m_loadWatcher, nullptr, this, nullptr);
    if (m_noiseWatcher) disconnect(m_noiseWatcher, nullptr, this, nullptr);
    if (m_threePlaneWatcher) disconnect(m_threePlaneWatcher, nullptr, this, nullptr);
    if (m_edgeWatcher) disconnect(m_edgeWatcher, nullptr, this, nullptr);
    if (m_planeImageWatcher) disconnect(m_planeImageWatcher, nullptr, this, nullptr);
    if (m_scanPlanningWatcher) disconnect(m_scanPlanningWatcher, nullptr, this, nullptr);
    if (m_textureMappingWatcher) disconnect(m_textureMappingWatcher, nullptr, this, nullptr);
    if (m_stitchingWindow) {
        m_stitchingWindow->cancelAndWait();
        disconnect(m_stitchingWindow, nullptr, this, nullptr);
    }
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
    if (m_scanPlanningWatcher && m_scanPlanningWatcher->isRunning()) {
        m_scanPlanningWatcher->waitForFinished();
    }
    if (m_textureMappingWatcher && m_textureMappingWatcher->isRunning()) {
        m_textureMappingWatcher->waitForFinished();
    }
#ifdef PCV_BUILD_EXPERIMENTAL_CAMERA
    if (m_cameraDialog) {
        // Stop the SDK stream before the dialog is destroyed so the callback
        // thread never touches a torn-down Qt object.
        m_cameraDialog->close();
        delete m_cameraDialog;
        m_cameraDialog = nullptr;
    }
#endif
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
    m_fileInfo = ui->lbl_device_empty;
    m_canvasInfo = ui->lbl_canvas_info;
    ui->vbl_canvas->removeWidget(m_canvasInfo);
    m_canvasInfo->setParent(statusBar());
    // Preserve the preferred text width, but let the status bar shrink it
    // when the permanent memory readout needs space.
    m_canvasInfo->setMinimumWidth(1);
    m_canvasInfo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    statusBar()->addPermanentWidget(m_canvasInfo);
    m_memoryInfo = new QLabel(statusBar());
    m_memoryInfo->setObjectName(QStringLiteral("lbl_memory_info"));
    m_memoryInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_memoryInfo->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    m_memoryInfo->setToolTip(tr("当前 pointcloudview.exe 进程内存。\n工作集：当前驻留物理内存，包含共享页。\n私有提交量：进程独占的已提交内存，可能部分已换出。\n每 1 秒刷新，GUI 阻塞时会延迟。\n包含本进程的窗口、后台任务和缓存；不包含独立进程或 GPU 专用显存。\n1 MiB = 1024² 字节，1 GiB = 1024³ 字节。"));
    statusBar()->addPermanentWidget(m_memoryInfo);
    m_memoryTimer = new QTimer(this);
    m_memoryTimer->setObjectName(QStringLiteral("memory_monitor_timer"));
    m_memoryTimer->setInterval(1000);
    m_memoryTimer->setTimerType(Qt::CoarseTimer);
    connect(m_memoryTimer, &QTimer::timeout, this, &MainWindow::updateMemoryUsage);
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
    m_selectTextureImageButton = ui->btn_select_texture_image;
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
    connect(m_canvas, &QOpenGLWidget::frameSwapped, this, [this]() {
        if (!m_planeFramePending || m_closing) return;
        m_planeFramePending = false;
        if (m_threePlaneInputRevision != m_canvasRevision) return;
        qInfo() << "plane_fit_visible_ms=" << m_planeElapsed.nsecsElapsed() / 1.0e6
                << "revision=" << m_threePlaneInputRevision;
    });
    auto *canvasLayout = new QVBoxLayout(ui->wgt_canvas_host);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->addWidget(m_canvas);
    connect(ui->act_open, &QAction::triggered, this, &MainWindow::openPointCloudSource);
#ifdef PCV_BUILD_EXPERIMENTAL_CAMERA
    auto *cameraAction = new QAction(tr("相机实时取流（点云图）..."), this);
    connect(cameraAction, &QAction::triggered, this, &MainWindow::openCameraStream);
#endif
    auto *fileMenu = menuBar()->addMenu(tr("文件"));
    fileMenu->addAction(ui->act_open);
    fileMenu->addSeparator();
#ifdef PCV_BUILD_EXPERIMENTAL_CAMERA
    fileMenu->addAction(cameraAction);
#endif
    fileMenu->addSeparator();
    fileMenu->addAction(ui->act_exit);
    connect(ui->act_exit, &QAction::triggered, qApp, &QApplication::quit);
    connect(ui->btn_open_point_cloud, &QPushButton::clicked, this, &MainWindow::openPointCloudSource);
    connect(ui->btn_open_scanning_info, &QPushButton::clicked,
            this, &MainWindow::openTempScanningInfo);
    ui->lbl_header_subtitle->hide();
    ui->lbl_main_title->setStyleSheet(QStringLiteral("font-size: 12pt; font-weight: 600;"));
    connect(ui->btn_multiframe_registration, &QPushButton::clicked,
            this, &MainWindow::openMultiFrameRegistration);
#ifdef PCV_BUILD_EXPERIMENTAL_CAMERA
    connect(ui->btn_camera_device, &QPushButton::clicked,
            this, &MainWindow::openCameraStream);
    connect(ui->act_device_list, &QAction::triggered, this, &MainWindow::openCameraStream);
    connect(ui->act_camera_stream, &QAction::triggered, this, &MainWindow::openCameraStream);
#else
    ui->btn_camera_device->setVisible(false);
    ui->act_device_list->setEnabled(false);
    ui->act_camera_stream->setEnabled(false);
#endif
    connect(ui->act_coordinate_transform, &QAction::triggered,
            this, &MainWindow::openTempScanningInfo);
    connect(ui->act_file_io, &QAction::triggered,
            this, &MainWindow::openPointCloudSource);
    connect(ui->act_canvas_reset, &QAction::triggered,
            m_canvas, &PointCloudCanvas::resetView);
    connect(ui->act_render_settings, &QAction::triggered,
            this, &MainWindow::openRenderSettings);
    m_scanPlanningPage = new QWidget(ui->tw_main);
    m_scanPlanningPage->setObjectName(QStringLiteral("wgt_scan_planning"));
    auto *scanPageLayout = new QVBoxLayout(m_scanPlanningPage);
    auto *scanForm = new QFormLayout;
    const auto makeScanSpin = [this](const QString &name, double minimum, double maximum,
                                     double value, int decimals = 3) {
        auto *spin = new QDoubleSpinBox(m_scanPlanningPage);
        spin->setObjectName(name);
        spin->setRange(minimum, maximum);
        spin->setDecimals(decimals);
        spin->setValue(value);
        spin->setKeyboardTracking(false);
        return spin;
    };
    m_scanMode = new QComboBox(m_scanPlanningPage);
    m_scanMode->setObjectName(QStringLiteral("cb_scan_mode"));
    m_scanMode->addItem(tr("局部 ROI 扫描"), int(pcv::scan::ScanPlanningMode::LocalRoi));
    m_scanMode->addItem(tr("全局扫描"), int(pcv::scan::ScanPlanningMode::Global));
    m_scanMode->setCurrentIndex(0);
    scanForm->addRow(tr("扫描模式"), m_scanMode);
    m_scanHeight = makeScanSpin(QStringLiteral("dsb_scan_height"), 0.0, 1.0e7, 50.0);
    scanForm->addRow(tr("扫描高度 +Z (mm)"), m_scanHeight);
    m_scanCenter = new QLabel(tr("规划后显示扫描中心"), m_scanPlanningPage);
    m_scanCenter->setObjectName(QStringLiteral("lbl_scan_center"));
    m_scanCenter->setTextInteractionFlags(Qt::TextSelectableByMouse);
    scanForm->addRow(tr("扫描中心 XYZ"), m_scanCenter);
    m_scanRange = new QLabel(tr("规划后显示 WObj1 XY 范围"), m_scanPlanningPage);
    m_scanRange->setObjectName(QStringLiteral("lbl_scan_range"));
    m_scanRange->setTextInteractionFlags(Qt::TextSelectableByMouse);
    scanForm->addRow(tr("扫描范围"), m_scanRange);
    m_scanCameraWidth = makeScanSpin(QStringLiteral("dsb_scan_camera_width"), 0.001, 1.0e7, 100.0);
    scanForm->addRow(tr("相机扫描宽度 X (mm)"), m_scanCameraWidth);
    m_scanOverlap = makeScanSpin(QStringLiteral("dsb_scan_overlap"), 0.0, 99.999, 50.0);
    scanForm->addRow(tr("相邻扫描带重叠 (mm)"), m_scanOverlap);
    m_scanOverlap->setMaximum(m_scanCameraWidth->value() - 0.001);
    scanForm->addRow(tr("固定行程方向"), new QLabel(tr("+Y → -Y"), m_scanPlanningPage));
    scanForm->addRow(tr("行程延长"), new QLabel(tr("两端各延长 50 mm"), m_scanPlanningPage));
    scanPageLayout->addLayout(scanForm);

    auto *scanButtonRow = new QHBoxLayout;
    m_planScanButton = new QPushButton(tr("规划路径"), m_scanPlanningPage);
    m_planScanButton->setObjectName(QStringLiteral("btn_plan_scan"));
    m_exportScanButton = new QPushButton(tr("导出项目 JSON"), m_scanPlanningPage);
    m_exportScanButton->setObjectName(QStringLiteral("btn_export_scan_json"));
    m_exportScanButton->setEnabled(false);
    m_exportInoRobotButton = new QPushButton(tr("导出 InoRobot P.json"), m_scanPlanningPage);
    m_exportInoRobotButton->setObjectName(QStringLiteral("btn_export_inorobot_json"));
    m_exportInoRobotButton->setEnabled(false);
    scanButtonRow->addWidget(m_planScanButton);
    scanButtonRow->addWidget(m_exportScanButton);
    scanButtonRow->addWidget(m_exportInoRobotButton);
    scanButtonRow->addStretch(1);
    scanPageLayout->addLayout(scanButtonRow);
    m_scanOutput = new QPlainTextEdit(m_scanPlanningPage);
    m_scanOutput->setObjectName(QStringLiteral("pte_scan_output"));
    m_scanOutput->setReadOnly(true);
    m_scanOutput->setPlaceholderText(tr("规划成功后逐行显示 P 点"));
    scanPageLayout->addWidget(m_scanOutput, 1);
    ui->tw_main->addTab(m_scanPlanningPage, tr("扫描规划"));

    connect(m_planScanButton, &QPushButton::clicked, this, &MainWindow::planScanTrajectory);
    connect(m_exportScanButton, &QPushButton::clicked, this, &MainWindow::exportScanTrajectory);
    connect(m_exportInoRobotButton, &QPushButton::clicked, this, &MainWindow::exportInoRobotTrajectory);
    connect(m_scanMode, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { invalidateScanTrajectory(); });
    const QVector<QDoubleSpinBox *> scanInputs{m_scanHeight, m_scanCameraWidth, m_scanOverlap};
    for (QDoubleSpinBox *input : scanInputs) {
        connect(input, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this](double) { invalidateScanTrajectory(); });
    }
    connect(m_scanCameraWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double width) {
                m_scanOverlap->setMaximum(qMax(0.0, width - 0.001));
            });

    const auto createToolDialog = [this](QDialog **dialog, const QString &title,
                                         const QSize &size) {
        *dialog = new QDialog(this);
        (*dialog)->setWindowTitle(title);
        (*dialog)->setModal(false);
        (*dialog)->setWindowModality(Qt::NonModal);
        (*dialog)->setAttribute(Qt::WA_DeleteOnClose, false);
        (*dialog)->resize(size);
        auto *layout = new QVBoxLayout(*dialog);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(8);
        return layout;
    };
    auto *sourceLayout = createToolDialog(&m_sourceDialog, tr("数据源"), QSize(420, 520));
    ui->lbl_source_title->setParent(m_sourceDialog);
    ui->lw_files->setParent(m_sourceDialog);
    ui->lbl_device_empty->setParent(m_sourceDialog);
    ui->btn_device_placeholder->setParent(m_sourceDialog);
    sourceLayout->addWidget(ui->lbl_source_title);
    sourceLayout->addWidget(ui->lw_files, 1);
    sourceLayout->addWidget(ui->lbl_device_empty);
    sourceLayout->addWidget(ui->btn_device_placeholder);
    ui->wgt_source_panel->hide();

    const auto detachPage = [this](QWidget *page, QDialog *dialog) {
        const int index = ui->tw_main->indexOf(page);
        if (index >= 0) ui->tw_main->removeTab(index);
        page->setParent(dialog);
        dialog->layout()->addWidget(page);
        page->show();
    };
    auto *cleanLayout = createToolDialog(&m_cleanDialog, tr("点云清理"), QSize(440, 300));
    detachPage(ui->wgt_clean, m_cleanDialog);
    Q_UNUSED(cleanLayout);
    auto *planeLayout = createToolDialog(&m_planeDialog, tr("平面与 WObj1"), QSize(680, 620));
    detachPage(ui->wgt_plane, m_planeDialog);
    Q_UNUSED(planeLayout);
    auto *edgeLayout = createToolDialog(&m_edgeDialog, tr("边缘与输出"), QSize(720, 720));
    detachPage(ui->wgt_edge, m_edgeDialog);
    Q_UNUSED(edgeLayout);
    auto *scanLayout = createToolDialog(&m_scanDialog, tr("扫描规划"), QSize(720, 700));
    detachPage(m_scanPlanningPage, m_scanDialog);
    Q_UNUSED(scanLayout);
    ui->tw_main->hide();

    m_operationBar = new QWidget(ui->wgt_central);
    m_operationBar->setObjectName(QStringLiteral("operationBar"));
    auto *operationLayout = new QHBoxLayout(m_operationBar);
    operationLayout->setContentsMargins(10, 5, 10, 5);
    operationLayout->setSpacing(8);
    m_operationLabel = new QLabel(m_operationBar);
    m_operationCount = new QLabel(m_operationBar);
    m_operationUndo = new QPushButton(tr("撤销"), m_operationBar);
    m_operationCancel = new QPushButton(tr("取消"), m_operationBar);
    m_operationConfirm = new QPushButton(tr("确认"), m_operationBar);
    operationLayout->addWidget(m_operationLabel);
    operationLayout->addWidget(m_operationCount);
    operationLayout->addStretch(1);
    operationLayout->addWidget(m_operationUndo);
    operationLayout->addWidget(m_operationCancel);
    operationLayout->addWidget(m_operationConfirm);
    ui->rootLayout->addWidget(m_operationBar);
    m_operationBar->hide();
    connect(m_operationUndo, &QPushButton::clicked, this, &MainWindow::undoPickingOperation);
    connect(m_operationCancel, &QPushButton::clicked, this, &MainWindow::cancelPickingOperation);
    connect(m_operationConfirm, &QPushButton::clicked, this, &MainWindow::confirmPickingOperation);

    ui->toolbar_workflow->hide();
    ui->btn_multiframe_registration->show();
    ui->btn_camera_device->hide();
    ui->splitter_main->setStretchFactor(0, 0);
    ui->splitter_main->setStretchFactor(1, 1);
    ui->splitter_main->setSizes({0, 1});

    auto *toolsMenu = menuBar()->addMenu(tr("工具"));
    auto addToolAction = [this, toolsMenu](const QString &text, auto slot) {
        auto *action = toolsMenu->addAction(text);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };
    addToolAction(tr("数据源"), &MainWindow::showSourceDialog);
    addToolAction(tr("点云清理"), &MainWindow::showCleanDialog);
    addToolAction(tr("平面与 WObj1"), &MainWindow::showPlaneDialog);
    addToolAction(tr("边缘与输出"), &MainWindow::showEdgeDialog);
    addToolAction(tr("扫描规划"), &MainWindow::showScanDialog);
    addToolAction(tr("所选平面拼接"), [this] {
        if (pointTaskRunning()) return;
        showSelectedPlaneDialog(float(m_defaults.planeDistanceToleranceMm), this);
    });
    toolsMenu->addSeparator();
    addToolAction(tr("渲染设置"), &MainWindow::openRenderSettings);
    auto *resetAction = toolsMenu->addAction(tr("重置视角"));
    connect(resetAction, &QAction::triggered, m_canvas, &PointCloudCanvas::resetView);
    toolsMenu->addSeparator();
#ifdef PCV_BUILD_EXPERIMENTAL_CAMERA
    addToolAction(tr("相机设备"), &MainWindow::openCameraStream);
#else
    auto *cameraToolAction = toolsMenu->addAction(tr("相机设备"));
    cameraToolAction->setEnabled(false);
#endif
    addToolAction(tr("多帧点云拼接"), &MainWindow::openMultiFrameRegistration);
    toolsMenu->addSeparator();
    addToolAction(tr("创建工件模板"), &MainWindow::createWorkpieceTemplate);
    addToolAction(tr("打开工件模板"), &MainWindow::openWorkpieceTemplate);
    connect(m_fileList, &QListWidget::currentRowChanged, this, &MainWindow::loadSelectedSource);
    connect(m_pointSize, qOverload<int>(&QSpinBox::valueChanged), m_canvas, &PointCloudCanvas::setPointSize);
    connect(m_pointSize, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { updateRenderSettings(); });
    connect(ui->btn_reset_view, &QPushButton::clicked, m_canvas, &PointCloudCanvas::resetView);
    connect(m_colorMode, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::updateRenderSettings);
    connect(m_overlay, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MainWindow::updateRenderSettings);
    connect(m_mapMin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MainWindow::updateRenderSettings);
    connect(m_mapMax, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MainWindow::updateRenderSettings);
    connect(m_noiseApply, &QPushButton::clicked, this, &MainWindow::applyNoiseRemoval);
    connect(ui->btn_restore_cloud, &QPushButton::clicked, this, [this]() {
        if (pointTaskRunning()) { statusBar()->showMessage(tr("点云处理任务正在运行")); return; }
        publishCanvasCache(m_rawPoints, false, m_rawCloudSource); statusBar()->showMessage(tr("已恢复原始点云"));
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
    connect(m_selectTextureImageButton, &QPushButton::clicked,
            this, &MainWindow::selectTextureImage);
    connect(m_savePlaneImageButton, &QPushButton::clicked, this, &MainWindow::savePlaneImage);
    connect(m_finalizeTempWorkpieceButton, &QPushButton::clicked,
            this, &MainWindow::finalizeTempWorkpiece);
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

void MainWindow::updateMemoryUsage() {
    if (m_closing || !m_memoryInfo) return;
    m_memoryInfo->setText(pcv::view::formatProcessMemoryUsage(
        pcv::view::readProcessMemoryUsage()));
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

#ifdef PCV_BUILD_EXPERIMENTAL_CAMERA
void MainWindow::openCameraStream() {
    if (m_templateTaskActive || m_templateFormActive) return;
    if (!m_cameraDialog) {
        m_cameraDialog = new CameraDialog(this);
        m_cameraDialog->cloudPublished =
            [this](const QVector<pointcloud::Point3D> &points) {
                if (m_closing || !m_canvas || m_templateTaskActive || m_templateFormActive
                    || (m_stitchingWindow && m_stitchingWindow->isBusy())) return;
                // Pipeline canvas.publish reaches this callback; streaming
                // frames bypass the display cache while world overlays remain.
                m_canvas->setCloudStreaming(points);
                m_livePreviewPointCount = points.size();
                if (m_canvasInfo) {
                    m_canvasInfo->setText(
                        tr("相机实时点云  %1 点").arg(QLocale().toString(points.size())));
                }
                refreshRenderDialogContent();
            };
        m_cameraDialog->cloudFrozen =
            [this](const QVector<pointcloud::Point3D> &points) {
                if (m_closing || points.isEmpty() || m_templateTaskActive || m_templateFormActive
                    || (m_stitchingWindow && m_stitchingWindow->isBusy())) return;
                m_rawPoints = points;
                m_rawCloudSource = {};
                m_rawCloudSource.entry = QStringLiteral("camera_raw");
                publishCanvasCache(points, false, m_rawCloudSource);
                statusBar()->showMessage(tr("已冻结相机帧，可执行去噪和平面处理"));
            };
    }
    m_cameraDialog->show();
    m_cameraDialog->raise();
    m_cameraDialog->activateWindow();
}
#endif

void MainWindow::openPointCloudSource() {
    if (workflowBlocksNewSource()) {
        statusBar()->showMessage(tr("当前流程正在执行（%1），请先结束或取消后再打开新的数据源")
                                     .arg(workflowStateName()));
        return;
    }
    QMessageBox dialog(QMessageBox::Question, tr("打开点云"),
                       tr("请选择点云来源"), QMessageBox::Cancel, this);
    QPushButton *fileButton = dialog.addButton(tr("打开单个 PLY"), QMessageBox::AcceptRole);
    QPushButton *textureButton = dialog.addButton(
        tr("PLY 纹理实验（原始坐标）"), QMessageBox::AcceptRole);
    QPushButton *folderButton = dialog.addButton(tr("扫描文件夹 PLY"), QMessageBox::AcceptRole);
    dialog.exec();
    if (dialog.clickedButton() == fileButton) openPointCloud();
    else if (dialog.clickedButton() == textureButton) openTextureExperimentPly();
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
        // Folder scanning publishes a new canvas and therefore replaces any
        // active temporary-workpiece session. Keep the old session untouched
        // when the operator declines that replacement.
        if (!confirmReplaceTempWorkpieceSession()) return;
        invalidateTempWorkpieceSession();
        invalidateTextureExperiment();
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
        m_fileInfo->setText(tr("文件夹扫描完成\nPLY 文件数  %1\n请在数据源窗口中选择要显示的文件。")
                                .arg(m_sourceFiles.size()));
        showSourceDialog();
        statusBar()->showMessage(tr("已扫描 %1 个 PLY，正在显示第一个文件").arg(m_sourceFiles.size()));
        loadSelectedSource();
    }
}

void MainWindow::loadSelectedSource() {
    const int row = m_fileList ? m_fileList->currentRow() : -1;
    if (row < 0 || row >= m_sourceFiles.size() || pointTaskRunning()) return;
    if (m_textureExperimentActive) invalidateTextureExperiment();
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
    if (workflowBlocksNewSource()) {
        statusBar()->showMessage(tr("当前流程正在执行（%1），请先结束或取消后再打开新的 PLY")
                                     .arg(workflowStateName()));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开 PLY"),
        m_sourceDirectory.isEmpty() ? QString() : m_sourceDirectory,
        tr("PLY 文件 (*.ply);;所有文件 (*.*)"));
    if (path.isEmpty()) return;

    // "打开 PLY" is the plain-PLY entry of the temporary-workpiece line. The
    // selected PLY is converted from the depth-camera frame to robot_base with
    // a hand-eye XML and manually entered Start/End poses before anything is
    // published; cancelling the dialog must leave the canvas, the point cloud
    // and any running temporary-workpiece session untouched.
    PlyLineParameters parameters;
    if (!collectPlyLineParameters(this, path, &parameters)) return;
    if (!confirmReplaceTempWorkpieceSession()) return;
    invalidateTempWorkpieceSession();
    invalidateTextureExperiment();

    pcv::interface::TempWorkpieceOptions options;
    options.useInMemoryScan = true;
    options.outputDirectory = parameters.outputDirectory;
    pcv::interface::TempScanningInfo &scan = options.scanInfo;
    scan.schemaVersion = QString::fromLatin1(pcv::interface::kTempScanningSchema);
    scan.kind = QString::fromLatin1(pcv::interface::kTempScanningKind);
    // A bare PLY carries no scanning metadata: scan_id, created_at, layout and
    // frames follow the fixed plain-PLY rules of the temporary workpiece line.
    scan.createdAtIso8601 = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    scan.scanId = QFileInfo(parameters.pointCloudFile).completeBaseName();
    scan.pointCloudFile = parameters.pointCloudFile;
    scan.pointCloudLayout = pointcloud::DepthPointLayout::LineProfileXz;
    scan.coordinateFrame = QStringLiteral("camera");
    scan.calibrationFile = parameters.calibrationFile;
    scan.calibrationSourceFrame = QStringLiteral("camera");
    scan.calibrationTargetFrame = QStringLiteral("robot_base");
    scan.robotPoseStart = parameters.startPose;
    scan.robotPoseEnd = parameters.endPose;

    startPlyLinePreparation(options);
}

void MainWindow::openTextureExperimentPly() {
    if (workflowBlocksNewSource()) {
        statusBar()->showMessage(tr("当前流程正在执行（%1），请先结束或取消后再打开新的 PLY")
                                     .arg(workflowStateName()));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择纹理实验 PLY"),
        m_sourceDirectory.isEmpty() ? QString() : m_sourceDirectory,
        tr("PLY 文件 (*.ply);;所有文件 (*.*)"));
    if (path.isEmpty()) return;

    if (!confirmReplaceTempWorkpieceSession()) return;

    invalidateTempWorkpieceSession();
    invalidateTextureExperiment();
    m_pendingTextureExperiment = true;
    m_pendingPath = path;
    m_folderScanOnly = false;
    m_loading = true;
    m_progress->show();
    m_progress->setRange(0, 0);
    statusBar()->showMessage(tr("正在读取纹理实验 PLY（保留原始坐标和点序）..."));
    if (!m_loadWatcher) {
        m_loadWatcher = new QFutureWatcher<pointcloud::LoadResult>(this);
        connect(m_loadWatcher, &QFutureWatcher<pointcloud::LoadResult>::finished,
                this, &MainWindow::loadFinished);
    }
    const QString loadPath = m_pendingPath;
    m_loadWatcher->setFuture(QtConcurrent::run([loadPath]() {
        return pointcloud::loadPlyResult(loadPath);
    }));
}

void MainWindow::startPlyLinePreparation(const pcv::interface::TempWorkpieceOptions &options) {
    m_pendingTempWorkpieceSource = TempWorkpieceSource::Ply;
    m_prepareCancelToken = std::make_shared<std::atomic_bool>(false);
    const pointcloud::CancellationToken token = m_prepareCancelToken;

    pcv::interface::TempWorkpieceOptions prepared = options;
    prepared.isCancelled = [token]() { return token && token->load(); };

    if (!m_tempWorkpieceWatcher) {
        m_tempWorkpieceWatcher =
            new QFutureWatcher<pcv::interface::TempWorkpiecePreparation>(this);
        connect(m_tempWorkpieceWatcher,
                &QFutureWatcher<pcv::interface::TempWorkpiecePreparation>::finished,
                this, &MainWindow::tempWorkpieceFinished);
    }

    m_progress->show();
    m_progress->setRange(0, 0);
    setWorkflowState(WorkflowState::ReadingPly,
                     tr("正在读取 PLY 并转换为 robot_base（LineProfileXz）..."));
    m_tempWorkpieceWatcher->setFuture(QtConcurrent::run([prepared]() {
        QString workerError;
        return pcv::interface::prepareTempWorkpiece(prepared, &workerError);
    }));
}

void MainWindow::reportPlyLinePreparation(
    const pcv::interface::TempWorkpiecePreparation &prepared) {
    const auto axisName = [](int axis) {
        switch (axis) {
        case 0: return QStringLiteral("X");
        case 1: return QStringLiteral("Y");
        case 2: return QStringLiteral("Z");
        default: return QStringLiteral("-");
        }
    };
    QStringList lines;
    lines << tr("PLY 线路手眼转换完成（camera -> robot_base）")
          << tr("scan_id：%1").arg(prepared.scanId)
          << tr("PLY：%1").arg(QFileInfo(prepared.resolvedPointCloudFile).absoluteFilePath())
          << tr("手眼 XML：%1").arg(QFileInfo(prepared.resolvedCalibrationFile).absoluteFilePath())
          << tr("坐标方向：%1 -> %2").arg(prepared.calibrationSourceFrame,
                                          prepared.calibrationTargetFrame)
          << tr("点云布局：LineProfileXz")
          << tr("created_at：%1").arg(prepared.createdAtIso8601)
          << tr("输出目录（四件套提交目标）：%1").arg(QFileInfo(prepared.outputDirectory).absoluteFilePath())
          << QString()
          << tr("输入点数：%1").arg(QLocale().toString(prepared.declaredPointCount))
          << tr("转换成功点数：%1").arg(QLocale().toString(prepared.convertedPointCount))
          << tr("rejectedInvalid：%1").arg(QLocale().toString(prepared.rejectedInvalidPointCount))
          << tr("rejectedRange：%1").arg(QLocale().toString(prepared.rejectedRangePointCount))
          << tr("inputYMinimum：%1 mm").arg(prepared.inputYMinimum, 0, 'f', 4)
          << tr("inputYMaximum：%1 mm").arg(prepared.inputYMaximum, 0, 'f', 4)
          << tr("signedTravel：%1 mm").arg(prepared.signedTravel, 0, 'f', 4)
          << tr("dominantTravelAxis：%1").arg(axisName(prepared.dominantTravelAxis))
          << (m_tempWorkpieceRasterCompatible
              ? tr("亮度栅格对应：可用（原始点序保留）")
              : tr("亮度栅格对应：不可用（存在过滤点或点序不完整）"));
    appendTempWorkpieceOutput(lines.join(QLatin1Char('\n')));
    m_fileInfo->setText(
        tr("PLY 线路\nscan_id  %1\nlayout  LineProfileXz\n输入点数  %2\n转换点数  %3\n"
           "输入 Y 范围  %4 ~ %5 mm\n扫描行程  %6 mm（主轴 %7）\nPLY\n%8\nXML\n%9\n"
           "输出目录（四件套目标）\n%10")
            .arg(prepared.scanId)
            .arg(QLocale().toString(prepared.declaredPointCount))
            .arg(QLocale().toString(prepared.convertedPointCount))
            .arg(prepared.inputYMinimum, 0, 'f', 3)
            .arg(prepared.inputYMaximum, 0, 'f', 3)
            .arg(prepared.signedTravel, 0, 'f', 3)
            .arg(axisName(prepared.dominantTravelAxis))
            .arg(QFileInfo(prepared.resolvedPointCloudFile).absoluteFilePath())
            .arg(QFileInfo(prepared.resolvedCalibrationFile).absoluteFilePath())
            .arg(QFileInfo(prepared.outputDirectory).absoluteFilePath()));
    statusBar()->showMessage(
        tr("PLY 已转换为 robot_base：输入 %1 点，有效 %2 点；请依次人工选择第一组 P1、P2、P3")
            .arg(QLocale().toString(prepared.declaredPointCount))
            .arg(QLocale().toString(prepared.convertedPointCount)));
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
    if (m_pendingTextureExperiment && result.ok) {
        if (result.declaredPointCount <= 0
            || result.declaredPointCount != result.points.size()) {
            result.ok = false;
            result.error = tr("PLY 声明点数无效或与读取点数不一致，无法保留完整原始点序");
        } else {
            const QVector<pointcloud::Point3D> &loadedPoints = result.points;
            for (const auto &point : loadedPoints) {
                if (!std::isfinite(point.x) || !std::isfinite(point.y)
                    || !std::isfinite(point.z)) {
                    result.ok = false;
                    result.error = tr("PLY 含无效 XYZ，无法保证亮度图与有效点一一对应");
                    break;
                }
            }
        }
    }
    if (!result.ok) {
        m_loading = false;
        m_pendingTextureExperiment = false;
        m_progress->hide();
        QMessageBox::critical(this, tr("打开失败"), result.error);
        statusBar()->showMessage(tr("加载失败"));
        return;
    }
    if (result.points.isEmpty()) {
        m_loading = false;
        m_pendingTextureExperiment = false;
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
    m_pendingSourcePlyEncoding = result.sourcePlyEncoding;
    m_rawPoints = std::move(result.points);
    m_rawCloudSource = {};
    m_rawCloudSource.entry = m_pendingTextureExperiment ? QStringLiteral("raw_texture_experiment")
                                                      : QStringLiteral("external_ply");
    m_rawCloudSource.files = {m_pendingPath};
    if (m_pendingTextureExperiment) {
        const QFileInfo fileInfo(m_pendingPath);
        m_sourceFiles = {m_pendingPath};
        m_sourceDirectory = fileInfo.absolutePath();
        {
            const QSignalBlocker blocker(m_fileList);
            m_fileList->clear();
            m_fileList->addItem(fileInfo.fileName());
            m_fileList->setCurrentRow(0);
        }
        m_tempWorkpieceDeclaredPointCount = result.declaredPointCount;
        m_tempWorkpieceSourceIndices.clear();
        m_tempWorkpieceSourceIndices.reserve(m_rawPoints.size());
        for (qsizetype index = 0; index < m_rawPoints.size(); ++index)
            m_tempWorkpieceSourceIndices.push_back(index);
        m_tempWorkpieceRejectedInvalidPointCount = 0;
        m_tempWorkpieceRejectedRangePointCount = 0;
        m_tempWorkpieceRasterCompatible =
            result.declaredPointCount > 0
            && result.declaredPointCount == m_rawPoints.size();
        publishCanvasCache(m_rawPoints, false, m_rawCloudSource);
        // The source mode stays independent of raster eligibility.
        m_textureExperimentActive = true;
        m_pendingTextureExperiment = false;
        m_fileInfo->setText(tr("纹理实验 PLY（原始坐标）\n原始点数  %1\n声明点数  %2\n"
                               "请完成两组三点平面校验后选择同次扫描亮度图")
                                .arg(QLocale().toString(m_rawPoints.size()))
                                .arg(QLocale().toString(result.declaredPointCount)));
        m_canvasInfo->setText(tr("纹理实验：当前显示 %1 个点 · 原始坐标 · 保留原始点序")
                              .arg(QLocale().toString(m_points.size())));
        m_progress->hide();
        m_loading = false;
        applyDepthRenderRange(m_points);
        showPlaneDialog();
        startPlanePointSelection();
        if (!m_tempWorkpieceRasterCompatible) {
            const QString detail = tr("PLY 声明点数无效或与读取点数不一致，已禁用严格纹理映射");
            m_threeOutput->appendPlainText(detail);
            statusBar()->showMessage(detail);
        } else {
            statusBar()->showMessage(tr("纹理实验 PLY 已加载，请选择第一组三点"));
        }
        for (const QString &warning : result.warnings)
            qWarning() << "Texture experiment PLY warning:" << warning;
        updatePlaneEdgeUi();
        return;
    }
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
    publishCanvasCache(m_rawPoints, false, m_rawCloudSource);
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
    for (const QString &warning : result.warnings)
        qWarning() << "PLY load warning:" << warning;
    statusBar()->showMessage(result.warnings.isEmpty()
        ? tr("加载完成")
        : tr("加载完成（有 %1 条警告）").arg(result.warnings.size()));
}

void MainWindow::updateRenderSettings() {
    if (!m_canvas) return;
    m_canvas->setDisplayOptions(m_colorMode->currentIndex(), m_overlay->value(),
                                m_mapMin->value(), m_mapMax->value());
    if (m_renderDialog) {
        m_renderDialog->setDisplaySettings(m_pointSize->value(), m_colorMode->currentIndex(),
                                           m_overlay->value(), m_mapMin->value(),
                                           m_mapMax->value());
        refreshRenderDialogContent();
    }

}

void MainWindow::openRenderSettings()
{
    if (!m_renderDialog) {
        m_renderDialog = new RenderDialog(this);
        m_renderDialog->setDisplaySettings(m_pointSize->value(), m_colorMode->currentIndex(),
                                           m_overlay->value(), m_mapMin->value(),
                                           m_mapMax->value());
        connect(m_renderDialog, &RenderDialog::settingsChanged,
                this, &MainWindow::applyRenderDialogSettings);
        connect(m_renderDialog, &RenderDialog::resetViewRequested,
                m_canvas, &PointCloudCanvas::resetView);
    }
    refreshRenderDialogContent();
    m_renderDialog->show();
    m_renderDialog->raise();
    m_renderDialog->activateWindow();
}

void MainWindow::applyRenderDialogSettings()
{
    if (!m_canvas || !m_renderDialog) return;
    const QSignalBlocker pointSizeBlocker(m_pointSize);
    const QSignalBlocker colorModeBlocker(m_colorMode);
    const QSignalBlocker overlayBlocker(m_overlay);
    const QSignalBlocker mapMinimumBlocker(m_mapMin);
    const QSignalBlocker mapMaximumBlocker(m_mapMax);
    m_pointSize->setValue(m_renderDialog->pointSize());
    m_colorMode->setCurrentIndex(m_renderDialog->colorMode());
    m_overlay->setValue(m_renderDialog->overlay());
    m_mapMin->setValue(m_renderDialog->mapMinimum());
    m_mapMax->setValue(m_renderDialog->mapMaximum());
    m_canvas->setPointSize(m_pointSize->value());
    m_canvas->setDisplayOptions(m_colorMode->currentIndex(), m_overlay->value(),
                                m_mapMin->value(), m_mapMax->value());
    m_canvas->setOverlayOptions(m_renderDialog->overlayOptions());
    refreshRenderDialogContent();
}

void MainWindow::refreshRenderDialogContent()
{
    if (!m_renderDialog) return;
    const QVector<pointcloud::Point3D> &points = m_points;
    QString source = m_pendingPath.isEmpty() ? tr("未加载文件")
                                             : QFileInfo(m_pendingPath).fileName();
    const QString livePreview = m_livePreviewPointCount > 0
        ? tr("\n实时预览点数：%1（未冻结）").arg(QLocale().toString(m_livePreviewPointCount))
        : QString();
    if (points.isEmpty()) {
        m_renderDialog->setContentText(tr("当前数据源：%1\n\n当前未加载可处理点云。\n"
                                          "相机实时帧仅预览时不会替换处理缓存。%2").arg(source, livePreview));
        return;
    }
    float minX = points.first().x, maxX = minX;
    float minY = points.first().y, maxY = minY;
    float minZ = points.first().z, maxZ = minZ;
    qsizetype validNormals = 0;
    for (const pointcloud::Point3D &point : points) {
        minX = qMin(minX, point.x); maxX = qMax(maxX, point.x);
        minY = qMin(minY, point.y); maxY = qMax(maxY, point.y);
        minZ = qMin(minZ, point.z); maxZ = qMax(maxZ, point.z);
        const float normalLengthSquared = point.nx * point.nx + point.ny * point.ny
            + point.nz * point.nz;
        if (std::isfinite(normalLengthSquared) && normalLengthSquared > 1.0e-8f)
            ++validNormals;
    }
    const QStringList colorModes = {tr("彩色高度"), tr("灰阶"), tr("法向量"), tr("纹理灰度")};
    const int colorIndex = qBound(0, m_colorMode->currentIndex(), colorModes.size() - 1);
    const auto options = m_renderDialog->overlayOptions();
    m_renderDialog->setContentText(
        tr("当前数据源：%1\n点云点数：%2\n有效法向量：%3\n\n"
           "X 范围：%4 ～ %5 mm\nY 范围：%6 ～ %7 mm\nZ 范围：%8 ～ %9 mm\n\n"
           "颜色样式：%10\n工件坐标系：%11\n"
           "坐标轴：%12  地板：%13  包围盒：%14\n"
           "法向量：%15  统计信息：%16")
            .arg(source)
            .arg(QLocale().toString(points.size()))
            .arg(QLocale().toString(validNormals))
            .arg(minX, 0, 'f', 2).arg(maxX, 0, 'f', 2)
            .arg(minY, 0, 'f', 2).arg(maxY, 0, 'f', 2)
            .arg(minZ, 0, 'f', 2).arg(maxZ, 0, 'f', 2)
            .arg(colorModes.at(colorIndex))
            .arg(m_workpieceCoordinate.valid ? tr("已建立") : tr("未建立"))
            .arg(options.showCoordinateAxes ? tr("显示") : tr("隐藏"))
            .arg(options.showFloorGrid ? tr("显示") : tr("隐藏"))
            .arg(options.showBoundingBox ? tr("显示") : tr("隐藏"))
            .arg(options.showNormals ? tr("显示") : tr("隐藏"))
            .arg(options.showStatistics ? tr("显示") : tr("隐藏")) + livePreview);
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
                                    bool preserveTempWorkpieceSession,
                                    CloudSourceContextV1 source) {
    const qsizetype incomingCount = points.size();

    // No GUI or OpenGL state may be touched once closeEvent() has started.
    // A QFutureWatcher completion can already be queued when the window closes.
    if (m_closing) {
        qWarning() << "Display cache publication discarded during shutdown, points="
                   << incomingCount;
        return;
    }
    m_livePreviewPointCount = 0;
    m_stitchingArtifactPaths.clear();

    // The CPU display cache is the authoritative input for every downstream
    // operation. A temporarily unavailable OpenGL widget must never discard a
    // successfully loaded cloud or leave the UI reporting zero displayed
    // points while m_rawPoints is populated.
    if (!preserveTempWorkpieceSession && hasTempWorkpieceSession()) {
        invalidateTempWorkpieceSession();
        // A canvas change also ends the plain-PLY session: its older points,
        // planes and ROI indices must not be reused. Existing formal output on
        // disk is never touched here.
        setWorkflowState(WorkflowState::Idle);
    }
    if (!preserveTempWorkpieceSession && m_textureExperimentActive) {
        invalidateTextureExperiment();
        setWorkflowState(WorkflowState::Idle);
    }
    m_points = std::move(points);
    if (m_planeCancelToken) m_planeCancelToken->store(true);
    m_planeFramePending = false;
    ++m_canvasRevision;
    m_currentCloudSource = std::move(source);
    m_cloudSourceRevision = m_canvasRevision;
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
    invalidateTextureOverlay();
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
    refreshRenderDialogContent();
}

bool MainWindow::pointTaskRunning() const {
    return m_templateTaskActive || m_templateFormActive || m_loading
        || (m_noiseWatcher && m_noiseWatcher->isRunning())
        || (m_stitchingWindow && m_stitchingWindow->isBusy())
        || (m_tempWorkpieceWatcher && m_tempWorkpieceWatcher->isRunning())
        || (m_tempWorkpieceFinalizeWatcher && m_tempWorkpieceFinalizeWatcher->isRunning())
        || (m_threePlaneWatcher && m_threePlaneWatcher->isRunning())
        || (m_edgeWatcher && m_edgeWatcher->isRunning())
        || (m_planeImageWatcher && m_planeImageWatcher->isRunning())
        || (m_textureMappingWatcher && m_textureMappingWatcher->isRunning())
        || (m_scanPlanningWatcher && m_scanPlanningWatcher->isRunning());
}

QString MainWindow::workflowStateName() const {
    switch (m_workflowState) {
    case WorkflowState::Idle: return tr("空闲");
    case WorkflowState::ReadingPly: return tr("读取 PLY");
    case WorkflowState::Transforming: return tr("坐标转换");
    case WorkflowState::SelectingPrimaryPlanePoints: return tr("第一组三点选点");
    case WorkflowState::ValidatingPrimaryPlane: return tr("第一组平面校验");
    case WorkflowState::SelectingSecondaryPlanePoints: return tr("第二组三点选点");
    case WorkflowState::ValidatingSecondaryPlane: return tr("第二组同面校验");
    case WorkflowState::FinalizingArtifacts: return tr("自动输出四件套");
    case WorkflowState::Completed: return tr("已完成");
    case WorkflowState::Failed: return tr("失败");
    case WorkflowState::Cancelled: return tr("已取消");
    }
    return tr("空闲");
}

// Automatic or worker-driven phases must not be replaced by a second
// data-source open. The interactive point-selection phases stay re-enterable so
// a user can restart a source without leaving the dialog.
bool MainWindow::workflowBlocksNewSource() const {
    if (pointTaskRunning()) return true;
    switch (m_workflowState) {
    case WorkflowState::ReadingPly:
    case WorkflowState::Transforming:
    case WorkflowState::ValidatingPrimaryPlane:
    case WorkflowState::ValidatingSecondaryPlane:
    case WorkflowState::FinalizingArtifacts:
        return true;
    default:
        break;
    }
    return false;
}

void MainWindow::setWorkflowState(WorkflowState state, const QString &statusText) {
    m_workflowState = state;
    if (!statusText.isEmpty()) statusBar()->showMessage(statusText);
    const char *source = "none";
    if (m_tempWorkpieceSource == TempWorkpieceSource::Json
        || m_pendingTempWorkpieceSource == TempWorkpieceSource::Json)
        source = "json";
    else if (m_tempWorkpieceSource == TempWorkpieceSource::Ply
             || m_pendingTempWorkpieceSource == TempWorkpieceSource::Ply)
        source = "ply";
    qInfo() << "Temporary workpiece workflow state:" << workflowStateName()
            << "source=" << source;
}

void MainWindow::showSourceDialog()
{
    if (!m_sourceDialog) return;
    m_sourceDialog->show();
    m_sourceDialog->raise();
    m_sourceDialog->activateWindow();
}

void MainWindow::showCleanDialog()
{
    if (!m_cleanDialog) return;
    m_cleanDialog->show();
    m_cleanDialog->raise();
    m_cleanDialog->activateWindow();
}

void MainWindow::showPlaneDialog()
{
    if (!m_planeDialog) return;
    m_planeDialog->show();
    m_planeDialog->raise();
    m_planeDialog->activateWindow();
}

void MainWindow::showEdgeDialog()
{
    if (!m_edgeDialog) return;
    m_edgeDialog->show();
    m_edgeDialog->raise();
    m_edgeDialog->activateWindow();
}

void MainWindow::showScanDialog()
{
    if (!m_scanDialog) return;
    m_scanDialog->show();
    m_scanDialog->raise();
    m_scanDialog->activateWindow();
}

void MainWindow::setPickingOperation(const QString &text, bool visible)
{
    if (!m_operationBar) return;
    m_operationBar->setVisible(visible);
    if (!visible) return;
    m_operationLabel->setText(text);
    const int selectedCount = m_edgeSelectionActive ? m_selectedEdgeIndices.size()
        : (m_secondPlaneSelectionActive ? m_secondPlanePointIndices.size()
                                        : m_selectedPointIndices.size());
    m_operationCount->setText(tr("已选点：%1").arg(selectedCount));
    m_operationUndo->setEnabled(selectedCount > 0 && !pointTaskRunning());
    m_operationCancel->setEnabled(!pointTaskRunning());
    m_operationConfirm->setText(m_edgeSelectionActive ? tr("完成") : tr("确认"));
    m_operationConfirm->setEnabled(
        (m_edgeSelectionActive || (!m_secondPlaneSelectionActive
                                   && m_selectedPointIndices.size() >= 3))
        && !pointTaskRunning());
}

void MainWindow::undoPickingOperation()
{
    if (m_edgeSelectionActive) {
        if (m_selectedEdgeIndices.isEmpty()) return;
        m_selectedEdgeIndices.removeLast();
        m_canvas->setSelectedEdgeIndices(m_selectedEdgeIndices);
        updatePlaneEdgeUi();
        setPickingOperation(tr("选择边缘真实点"), true);
        return;
    }
    if (m_secondPlaneSelectionActive) {
        if (m_secondPlanePointIndices.isEmpty()) return;
        m_secondPlanePointIndices.removeLast();
        QVector<int> marked = m_selectedPointIndices;
        marked += m_secondPlanePointIndices;
        m_canvas->setSelectedIndices(marked);
        updatePlaneExtractionUi();
        setPickingOperation(tr("选择第二组三点"), true);
        return;
    }
    undoPlanePointSelection();
}

void MainWindow::cancelPickingOperation()
{
    if (m_edgeSelectionActive) {
        clearEdgePointSelection();
    } else if (m_secondPlaneSelectionActive) {
        cancelSecondPlanePointSelection();
    } else {
        abandonPlanePointSelection();
    }
}

void MainWindow::confirmPickingOperation()
{
    if (m_edgeSelectionActive) {
        finishEdgePointSelection();
    } else if (!m_secondPlaneSelectionActive) {
        determinePlaneCandidate();
    }
}

bool MainWindow::hasTempWorkpieceSession() const {
    return m_tempWorkpieceSource != TempWorkpieceSource::None
        && m_tempWorkpieceSessionRevision != 0
        && !m_tempWorkpieceScanId.isEmpty()
        && !m_tempWorkpieceOutputDirectory.isEmpty();
}

bool MainWindow::confirmReplaceTempWorkpieceSession() {
    if (!hasTempWorkpieceSession()) return true;
    const auto answer = QMessageBox::question(
        this, tr("替换当前临时工件会话"),
        tr("当前临时工件会话尚未完成，仍可继续取点或重试。\n"
           "打开新的数据源会使当前会话失效，但保留当前画布供查看。\n\n"
           "是否放弃当前会话并打开新的数据源？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer == QMessageBox::Yes;
}

void MainWindow::invalidateTempWorkpieceSession() {
    m_tempWorkpieceSource = TempWorkpieceSource::None;
    m_pendingTempWorkpieceSource = TempWorkpieceSource::None;
    m_tempWorkpieceSessionRevision = 0;
    m_tempWorkpieceSourceIndices.clear();
    m_tempWorkpieceDeclaredPointCount = 0;
    m_tempWorkpieceRejectedInvalidPointCount = 0;
    m_tempWorkpieceRejectedRangePointCount = 0;
    m_tempWorkpieceRasterCompatible = false;
    m_tempWorkpieceScanId.clear();
    m_tempWorkpieceOutputDirectory.clear();
    m_tempWorkpieceCreatedAtIso8601.clear();
    updatePlaneEdgeUi();
}

void MainWindow::invalidateTextureExperiment() {
    m_textureExperimentActive = false;
    m_pendingTextureExperiment = false;
    m_tempWorkpieceSourceIndices.clear();
    m_tempWorkpieceDeclaredPointCount = 0;
    m_tempWorkpieceRejectedInvalidPointCount = 0;
    m_tempWorkpieceRejectedRangePointCount = 0;
    m_tempWorkpieceRasterCompatible = false;
    invalidateTextureOverlay();
    updatePlaneEdgeUi();
}

void MainWindow::failAutomaticTempWorkpieceStage(const QString &stage,
                                                  const QString &detail) {
    const QString message = tr("自动流程失败阶段：%1：%2").arg(stage, detail);
    appendTempWorkpieceOutput(message);
    setWorkflowState(WorkflowState::Failed, message);
    updatePlaneExtractionUi();
    updatePlaneEdgeUi();
}

void MainWindow::appendTempWorkpieceOutput(const QString &text) {
    if (!m_threeOutput || text.isEmpty()) return;
    if (!m_threeOutput->toPlainText().isEmpty()) m_threeOutput->appendPlainText(QString());
    m_threeOutput->appendPlainText(text);
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
    invalidateTextureOverlay();
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
    if (m_planeDialog) m_planeDialog->hide();
    setPickingOperation(tr("选择第一组三点"), true);
    updatePlaneExtractionUi();
    updatePlaneEdgeUi();
    setWorkflowState(WorkflowState::SelectingPrimaryPlanePoints);
    statusBar()->showMessage(tr("请选择第 1 个平面控制点（至少 3 个）"));
}

void MainWindow::abandonPlanePointSelection() {
    if (m_threePlaneWatcher && m_threePlaneWatcher->isRunning()) {
        statusBar()->showMessage(tr("平面提取正在运行"));
        return;
    }
    invalidateTextureOverlay();
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
    setWorkflowState(WorkflowState::Cancelled);
    setPickingOperation(QString(), false);
    showPlaneDialog();
    statusBar()->showMessage(hasTempWorkpieceSession()
        ? tr("已放弃本轮取点，当前临时工件会话保留，可重新开始取点")
        : tr("已放弃取点"));
}

void MainWindow::undoPlanePointSelection() {
    if (pointTaskRunning()) {
        statusBar()->showMessage(tr("点云处理任务正在运行"));
        return;
    }
    if (m_selectedPointIndices.isEmpty()) return;
    invalidateTextureOverlay();
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
    setWorkflowState(WorkflowState::SelectingPrimaryPlanePoints);
    statusBar()->showMessage(tr("请选择第 %1 个平面控制点").arg(m_selectedPointIndices.size() + 1));
    setPickingOperation(tr("选择第一组三点"), true);
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
    QString textureError;
    const bool textureReady = planeReady
        && !m_threePlaneResult.planeIndices.isEmpty()
        && textureRasterMappingReady(&textureError);
    if (m_edgeApplyButton)
        m_edgeApplyButton->setEnabled(!running && planeReady && !m_textureExperimentActive);
    if (m_selectEdgeButton)
        m_selectEdgeButton->setEnabled(!running && m_planeEdgeResult.ok
                                       && !m_planeEdgeResult.edgeIndices.isEmpty());
    if (m_clearEdgeSelectionButton)
        m_clearEdgeSelectionButton->setEnabled(!running && !m_selectedEdgeIndices.isEmpty());
    if (m_extractPlaneImageButton)
        m_extractPlaneImageButton->setEnabled(!running && !imageRunning
                                               && planeReady && !m_textureExperimentActive);
    if (m_selectTextureImageButton)
        m_selectTextureImageButton->setEnabled(!pointTaskRunning()
                                                && textureReady);
    if (m_selectTextureImageButton)
        m_selectTextureImageButton->setToolTip(textureReady
            ? tr("仅支持与当前单个 PLY 同次扫描、同点序的亮度图")
            : (!planeReady ? tr("请先完成两组三点平面校验") : textureError));
    if (m_savePlaneImageButton)
        m_savePlaneImageButton->setEnabled(planeImageSaveReady());
    if (m_finalizeTempWorkpieceButton) {
        QString finalizeError;
        const bool visible = hasTempWorkpieceSession();
        m_finalizeTempWorkpieceButton->setVisible(visible);
        m_finalizeTempWorkpieceButton->setEnabled(
            visible && tempWorkpieceFinalizeReady(&finalizeError));
        m_finalizeTempWorkpieceButton->setToolTip(finalizeError);
    }
}

bool MainWindow::textureRasterMappingReady(QString *error) const {
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (!m_textureExperimentActive && m_tempWorkpieceSource != TempWorkpieceSource::Ply)
        return fail(QStringLiteral("当前点云没有原始亮度栅格对应关系，无法进行严格纹理映射"));
    if (!m_tempWorkpieceRasterCompatible)
        return fail(QStringLiteral("当前单个 PLY 在转换或处理过程中已失去原始亮度栅格对应关系"));
    if (m_tempWorkpieceDeclaredPointCount <= 0)
        return fail(QStringLiteral("原始 PLY 点数无效，无法进行严格纹理映射"));
    if (m_tempWorkpieceSourceIndices.size() != m_points.size())
        return fail(QStringLiteral("当前点云没有完整的原始 PLY source index"));
    if (m_points.size() != m_tempWorkpieceDeclaredPointCount)
        return fail(QStringLiteral("当前点云数量已改变，无法进行严格纹理映射"));
    if (m_tempWorkpieceRejectedInvalidPointCount > 0
        || m_tempWorkpieceRejectedRangePointCount > 0)
        return fail(QStringLiteral("原始 PLY 存在被过滤点，无法保证亮度图一一对应"));
    return true;
}

void MainWindow::invalidateTextureOverlay() {
    if (m_textureCancelToken) m_textureCancelToken->store(true);
    m_textureImagePath.clear();
    m_pendingTextureImagePath.clear();
    if (m_canvas) m_canvas->clearTextureGrayOverlay();
    if (m_colorMode && m_colorMode->currentIndex() == 3) {
        const QSignalBlocker blocker(m_colorMode);
        m_colorMode->setCurrentIndex(0);
        updateRenderSettings();
    }
}

void MainWindow::openMultiFrameRegistration()
{
    if (pointTaskRunning() && (!m_stitchingWindow || !m_stitchingWindow->isBusy())) {
        statusBar()->showMessage(tr("点云处理任务正在运行"));
        return;
    }
    if (!m_stitchingWindow) {
        m_stitchingWindow = new StitchingWindow(this);
        m_stitchingWindow->canStartTask = [this] {
            if (m_closing || pointTaskRunning()) {
                statusBar()->showMessage(tr("点云处理任务正在运行"));
                return false;
            }
            return confirmReplaceTempWorkpieceSession();
        };
        connect(m_stitchingWindow, &StitchingWindow::stitchingSucceeded,
                this, &MainWindow::stitchingSucceeded);
    }
    m_stitchingWindow->show();
    m_stitchingWindow->raise();
    m_stitchingWindow->activateWindow();
}

void MainWindow::stitchingSucceeded(const StitchingPublication &publication)
{
    if (m_closing || publication.points.isEmpty()) return;
#ifdef PCV_BUILD_EXPERIMENTAL_CAMERA
    if (m_cameraDialog) m_cameraDialog->close();
#endif
    m_pendingPath = publication.formalPly;
    m_rawPoints = publication.points;
    m_rawCloudSource = {};
    m_rawCloudSource.robotBaseConfirmed = true;
    m_rawCloudSource.entry = QStringLiteral("stitching_robot_base");
    m_rawCloudSource.files = {publication.formalPly};
    m_rawCloudSource.manifest = publication.reportJson;
    m_rawCloudSource.processingSteps.append(QStringLiteral("stitching_or_transform_only"));
    m_sourceFiles = {publication.formalPly};
    m_sourceDirectory = publication.outputDirectory;
    m_folderScanOnly = false;
    m_pendingTextureExperiment = false;
    {
        const QSignalBlocker blocker(m_fileList);
        m_fileList->clear();
        if (!publication.formalPly.isEmpty())
            m_fileList->addItem(QFileInfo(publication.formalPly).fileName());
        m_fileList->setCurrentRow(0);
    }
    publishCanvasCache(publication.points, false, m_rawCloudSource);
    m_stitchingArtifactPaths = {publication.formalPly, publication.previewPly,
        publication.mappingFile, publication.reportJson};
    applyDepthRenderRange(m_points);
    m_fileInfo->setText(tr("多帧点云拼接\n正式 PLY\n%1\n点数  %2")
                            .arg(publication.formalPly)
                            .arg(QLocale().toString(m_rawPoints.size())));
    statusBar()->showMessage(tr("多帧点云拼接结果已发布到当前画布"));
}

void MainWindow::openTempScanningInfo() {
    if (workflowBlocksNewSource()) {
        statusBar()->showMessage(tr("当前流程正在执行（%1），请先结束或取消后再打开扫描 JSON")
                                     .arg(workflowStateName()));
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
            .arg(pose.rx, 0, 'g', 12).arg(pose.ry, 0, 'g', 12).arg(pose.rz, 0, 'g', 12);
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
    if (inputDialog.exec() != QDialog::Accepted) return;
    if (!confirmReplaceTempWorkpieceSession()) return;
    invalidateTempWorkpieceSession();
    invalidateTextureExperiment();

    // The JSON entry never uses the in-memory scan descriptor of the plain-PLY
    // line; it keeps reading temp_scanning_info.json unchanged.
    m_pendingTempWorkpieceSource = TempWorkpieceSource::Json;
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
    setWorkflowState(WorkflowState::ReadingPly, tr("正在处理扫描 JSON..."));
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

    const bool plyLine = m_pendingTempWorkpieceSource == TempWorkpieceSource::Ply;
    m_progress->hide();
    if (!result.success) {
        const QString detail = result.errorCode.isEmpty()
            ? result.message
            : QStringLiteral("%1: %2").arg(result.errorCode, result.message);
        const QString title = plyLine ? tr("PLY 线路处理失败") : tr("扫描 JSON 处理失败");
        if (result.cancelled) {
            // A cancelled conversion must never create a session, publish a
            // canvas, start point selection or produce output.
            m_pendingTempWorkpieceSource = TempWorkpieceSource::None;
            setWorkflowState(WorkflowState::Cancelled, detail);
            appendTempWorkpieceOutput(tr("坐标转换已取消：%1").arg(detail));
            return;
        }
        const bool inputFailure = result.errorCode == QStringLiteral("PCV_INPUT_001")
            || result.errorCode == QStringLiteral("PCV_INPUT_002")
            || result.errorCode == QStringLiteral("PCV_CONTRACT_001");
        if (inputFailure)
            QMessageBox::warning(this, title, detail);
        else
            QMessageBox::critical(this, title, detail);
        if (plyLine) {
            // Keep the previous canvas and the previous (finished) output: a
            // failed conversion never replaces the last successful result.
            m_pendingTempWorkpieceSource = TempWorkpieceSource::None;
            appendTempWorkpieceOutput(
                tr("自动流程失败阶段：PLY 读取与手眼转换：%1").arg(detail));
        }
        setWorkflowState(WorkflowState::Failed, detail);
        return;
    }

    const QString layoutName = QStringLiteral("LineProfileXz");
    m_rawPoints = result.robotBasePoints;
    m_rawCloudSource = {};
    m_rawCloudSource.robotBaseConfirmed = result.coordinateFrame == QStringLiteral("robot_base");
    m_rawCloudSource.entry = QStringLiteral("handeye_conversion");
    m_rawCloudSource.files = {result.resolvedPointCloudFile, result.resolvedCalibrationFile};
    m_rawCloudSource.manifest = result.scanningInfoPath;
    m_rawCloudSource.processingSteps.append(QStringLiteral("handeye_transform_to_robot_base"));
    m_tempWorkpieceSourceIndices = std::move(result.sourceIndices);
    m_tempWorkpieceDeclaredPointCount = result.declaredPointCount;
    m_tempWorkpieceRejectedInvalidPointCount = result.rejectedInvalidPointCount;
    m_tempWorkpieceRejectedRangePointCount = result.rejectedRangePointCount;
    m_tempWorkpieceRasterCompatible =
        plyLine
        && result.declaredPointCount > 0
        && m_tempWorkpieceSourceIndices.size() == result.robotBasePoints.size()
        && result.robotBasePoints.size() == result.declaredPointCount
        && result.rejectedInvalidPointCount == 0
        && result.rejectedRangePointCount == 0;
    m_tempWorkpieceScanId = result.scanId;
    m_tempWorkpieceOutputDirectory = result.outputDirectory;
    m_tempWorkpieceCreatedAtIso8601 = result.createdAtIso8601;
    m_pendingPath = result.resolvedPointCloudFile;
    m_sourceFiles = {m_pendingPath};
    m_sourceDirectory = QFileInfo(m_pendingPath).absolutePath();
    publishCanvasCache(std::move(result.robotBasePoints), true, m_rawCloudSource);
    applyDepthRenderRange(m_points);
    m_tempWorkpieceSource = m_pendingTempWorkpieceSource;
    m_pendingTempWorkpieceSource = TempWorkpieceSource::None;
    m_tempWorkpieceSessionRevision = m_canvasRevision;
    showPlaneDialog();
    startPlanePointSelection();
    if (plyLine) {
        // Plain-PLY diagnostics are appended after startPlanePointSelection()
        // because the plane pane is rewritten by the selection UI refresh.
        reportPlyLinePreparation(result);
        return;
    }
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
    if (!hasTempWorkpieceSession())
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
    ++m_roiRevision;
    invalidateScanTrajectory(tr("WObj1 或 ROI 已变化，旧扫描路径已失效"));
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

void MainWindow::invalidateScanTrajectory(const QString &reason) {
    ++m_scanPlanningRequestRevision;
    if (m_scanPlanningCancelToken) m_scanPlanningCancelToken->store(true);
    m_scanTrajectoryResult = {};
    if (m_exportScanButton) m_exportScanButton->setEnabled(false);
    if (m_exportInoRobotButton) m_exportInoRobotButton->setEnabled(false);
    if (m_scanOutput) m_scanOutput->setPlainText(reason);
    if (m_scanCenter) m_scanCenter->setText(tr("规划后显示扫描中心"));
    if (m_scanRange) m_scanRange->setText(tr("规划后显示 WObj1 XY 范围"));
}

void MainWindow::planScanTrajectory() {
    if (m_closing || (m_scanPlanningWatcher && m_scanPlanningWatcher->isRunning())) return;
    if (pointTaskRunning()) {
        statusBar()->showMessage(tr("点云处理任务正在运行，请稍后规划扫描路径"));
        return;
    }
    invalidateScanTrajectory();
    pcv::scan::ScanPlanningOptions options;
    options.mode = static_cast<pcv::scan::ScanPlanningMode>(m_scanMode->currentData().toInt());
    options.workpieceCoordinateSystem = toScanCoordinateFrame(m_workpieceCoordinate);
    options.travelDirection = pcv::scan::ScanTravelDirection::PositiveYToNegativeY;
    options.scanHeightMm = float(m_scanHeight->value());
    options.cameraWidthMm = float(m_scanCameraWidth->value());
    options.overlapMm = float(m_scanOverlap->value());
    options.travelExtensionMm = 50.0f;
    options.points.reserve(m_points.size());
    const QVector<pointcloud::Point3D> &currentPoints = m_points;
    for (const pointcloud::Point3D &point : currentPoints) options.points.push_back(point);
    if (options.mode == pcv::scan::ScanPlanningMode::Global) {
        options.globalPlaneIndices.reserve(m_threePlaneResult.planeIndices.size());
        const QVector<int> &currentPlane = m_threePlaneResult.planeIndices;
        for (const int index : currentPlane) options.globalPlaneIndices.push_back(index);
    } else {
        options.localRoiIndices.reserve(m_planeImageResult.roiIndices.size());
        const QVector<int> &currentRoi = m_planeImageResult.roiIndices;
        for (const int index : currentRoi) options.localRoiIndices.push_back(index);
    }
    m_scanPlanningCancelToken = std::make_shared<std::atomic_bool>(false);
    const pointcloud::CancellationToken token = m_scanPlanningCancelToken;
    options.isCancelled = [token]() { return token && token->load(); };
    m_scanPlanningTaskRevision = m_scanPlanningRequestRevision;
    m_scanPlanningCanvasRevision = m_canvasRevision;
    m_scanPlanningCoordinateRevision = m_coordinateFrameRevision;
    m_scanPlanningPlaneRevision = m_threePlaneInputRevision;
    m_scanPlanningRoiRevision = m_roiRevision;
    m_planScanButton->setEnabled(false);
    m_scanOutput->setPlainText(tr("正在规划扫描路径..."));
    m_exportScanButton->setEnabled(false);
    m_exportInoRobotButton->setEnabled(false);
    if (!m_scanPlanningWatcher) {
        m_scanPlanningWatcher = new QFutureWatcher<pcv::scan::ScanTrajectoryResult>(this);
        connect(m_scanPlanningWatcher, &QFutureWatcher<pcv::scan::ScanTrajectoryResult>::finished,
                this, &MainWindow::scanPlanningFinished);
    }
    m_scanPlanningWatcher->setFuture(QtConcurrent::run([options]() {
        return pcv::scan::planScanTrajectory(options);
    }));
    statusBar()->showMessage(tr("正在后台规划扫描路径..."));
}

void MainWindow::scanPlanningFinished() {
    if (m_planScanButton) m_planScanButton->setEnabled(true);
    if (m_closing || !m_scanPlanningWatcher) return;
    const pcv::scan::ScanTrajectoryResult result = m_scanPlanningWatcher->result();
    if (m_scanPlanningTaskRevision != m_scanPlanningRequestRevision
        || m_scanPlanningCanvasRevision != m_canvasRevision
        || m_scanPlanningCoordinateRevision != m_coordinateFrameRevision
        || m_scanPlanningPlaneRevision != m_threePlaneInputRevision
        || m_scanPlanningRoiRevision != m_roiRevision) {
        statusBar()->showMessage(tr("规划输入已变化，旧扫描路径已丢弃"));
        return;
    }
    if (!result.ok) {
        m_scanTrajectoryResult = {};
        m_exportScanButton->setEnabled(false);
        m_scanOutput->setPlainText(result.error);
        statusBar()->showMessage(result.cancelled ? tr("扫描路径规划已取消")
                                                  : tr("扫描路径规划失败：%1").arg(result.error));
        return;
    }
    m_scanTrajectoryResult = result;
    const QLocale numberLocale = QLocale::c();
    QStringList lines;
    lines.reserve(result.points.size());
    for (const pcv::scan::ScanTrajectoryPoint &point : result.points) {
        const pointcloud::RobotPose &pose = point.robotPose;
        lines.push_back(QStringLiteral("P%1=[%2,%3,%4,%5,%6,%7]")
            .arg(point.pointId)
            .arg(numberLocale.toString(pose.x, 'f', 6))
            .arg(numberLocale.toString(pose.y, 'f', 6))
            .arg(numberLocale.toString(pose.z, 'f', 6))
            .arg(numberLocale.toString(pose.rx, 'f', 6))
            .arg(numberLocale.toString(pose.ry, 'f', 6))
            .arg(numberLocale.toString(pose.rz, 'f', 6)));
    }
    lines.prepend(tr("内部 P0/P1...；导出 P.json 使用 P[10]/P[11]..."));
    m_scanOutput->setPlainText(lines.join(QLatin1Char('\n')));
    m_scanCenter->setText(QStringLiteral("[%1,%2,%3]")
        .arg(numberLocale.toString(result.centerPose.x, 'f', 6))
        .arg(numberLocale.toString(result.centerPose.y, 'f', 6))
        .arg(numberLocale.toString(result.centerPose.z, 'f', 6)));
    m_scanRange->setText(QStringLiteral("X 宽度=%1 mm，原始 Y 行程=%2 mm，最终行程=%3 mm，扫描线=%4，间距=%5 mm，实际重叠=%6 mm")
        .arg(numberLocale.toString(result.requiredWidthMm, 'f', 3))
        .arg(numberLocale.toString(result.requiredTravelLengthMm, 'f', 3))
        .arg(numberLocale.toString(result.finalTravelLengthMm, 'f', 3))
        .arg(result.scanLineCount)
        .arg(numberLocale.toString(result.lineSpacingMm, 'f', 3))
        .arg(numberLocale.toString(result.actualOverlapMm, 'f', 3)));
    m_exportScanButton->setEnabled(true);
    m_exportInoRobotButton->setEnabled(true);
    statusBar()->showMessage(tr("扫描路径规划完成：%1 条扫描线，%2 个 P 点，实际重叠 %3 mm")
        .arg(result.scanLineCount).arg(result.points.size())
        .arg(result.actualOverlapMm, 0, 'f', 3));
}

void MainWindow::exportScanTrajectory() {
    if (!m_scanTrajectoryResult.ok || !m_exportScanButton->isEnabled()
        || m_scanPlanningCanvasRevision != m_canvasRevision
        || m_scanPlanningCoordinateRevision != m_coordinateFrameRevision
        || m_scanPlanningPlaneRevision != m_threePlaneInputRevision
        || m_scanPlanningRoiRevision != m_roiRevision) {
        invalidateScanTrajectory(tr("扫描输入已变化，请重新规划后导出"));
        QMessageBox::warning(this, tr("导出扫描路径"), tr("请先生成有效的扫描路径"));
        return;
    }
    const QString initialDirectory = m_sourceDirectory.isEmpty()
        ? QDir::homePath() : m_sourceDirectory;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出扫描路径 JSON"),
        QDir(initialDirectory).filePath(QStringLiteral("scan_trajectory.json")),
        tr("JSON 文件 (*.json)"), nullptr, QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty()) return;
    const QFileInfo destination(path);
    const bool exists = destination.exists();
    if (exists) {
        const auto answer = QMessageBox::question(
            this, tr("确认覆盖"), tr("文件已存在：\n%1\n\n是否覆盖？").arg(path),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }
    pcv::io::ArtifactTransaction transaction(destination.absolutePath(), exists);
    const QByteArray json = QJsonDocument(
        pcv::scan::scanTrajectoryToJson(m_scanTrajectoryResult)).toJson(QJsonDocument::Indented);
    if (!transaction.stageJson(destination.fileName(), json) || !transaction.commit()) {
        QMessageBox::warning(this, tr("导出扫描路径失败"), transaction.error());
        statusBar()->showMessage(tr("扫描路径 JSON 未发布"));
        return;
    }
    statusBar()->showMessage(tr("扫描路径已导出：%1").arg(path));
}

void MainWindow::exportInoRobotTrajectory() {
    if (!m_scanTrajectoryResult.ok || !m_exportInoRobotButton->isEnabled()
        || m_scanPlanningCanvasRevision != m_canvasRevision
        || m_scanPlanningCoordinateRevision != m_coordinateFrameRevision
        || m_scanPlanningPlaneRevision != m_threePlaneInputRevision
        || m_scanPlanningRoiRevision != m_roiRevision) {
        invalidateScanTrajectory(tr("扫描输入已变化，请重新规划后导出"));
        QMessageBox::warning(this, tr("导出 InoRobot 路径"), tr("请先生成有效的扫描路径"));
        return;
    }
    const QString initialDirectory = m_sourceDirectory.isEmpty()
        ? QDir::homePath() : m_sourceDirectory;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出 InoRobot P.json"),
        QDir(initialDirectory).filePath(QStringLiteral("P.json")),
        tr("JSON 文件 (*.json)"), nullptr, QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty()) return;
    const QFileInfo destination(path);
    const bool exists = destination.exists();
    if (exists) {
        const auto answer = QMessageBox::question(
            this, tr("确认覆盖"), tr("文件已存在：\n%1\n\n是否覆盖？").arg(path),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }
    pcv::io::ArtifactTransaction transaction(destination.absolutePath(), exists);
    const QByteArray json = QJsonDocument(
        pcv::scan::scanTrajectoryToInoRobotJson(
            m_scanTrajectoryResult, toScanCoordinateFrame(m_workpieceCoordinate))).toJson(QJsonDocument::Indented);
    if (!transaction.stageJson(destination.fileName(), json) || !transaction.commit()) {
        QMessageBox::warning(this, tr("导出 InoRobot 路径失败"), transaction.error());
        statusBar()->showMessage(tr("InoRobot P.json 未发布"));
        return;
    }
    statusBar()->showMessage(tr("InoRobot P.json 已导出：%1").arg(path));
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
        ++m_roiRevision;
        invalidateScanTrajectory(tr("ROI 已变化，旧扫描路径已失效"));
        m_planeImagePreview->setPixmap({});
        m_planeImagePreview->setText(tr("画布或工件坐标系已变化，请重新生成平面图像"));
        m_edgeOutput->setPlainText(tr("画布缓存已变化，旧平面图像已丢弃。"));
        if (hasTempWorkpieceSession())
            failAutomaticTempWorkpieceStage(tr("ROI/Mask 结果发布"),
                                             tr("画布或工件坐标系已变化"));
        else
            updatePlaneEdgeUi();
        return;
    }
    m_planeImageResult = result;
    ++m_roiRevision;
    invalidateScanTrajectory(tr("ROI 已更新，请重新规划扫描路径"));
    if (!result.ok) {
        m_planeImagePreview->setPixmap({});
        m_planeImagePreview->setText(result.error);
        m_edgeOutput->setPlainText(result.error);
        updatePlaneEdgeUi();
        failAutomaticTempWorkpieceStage(tr("ROI/Mask 生成"), result.error);
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
    if (hasTempWorkpieceSession() && result.edgeMask) {
        statusBar()->showMessage(tr("边缘 PNG 已生成，正在提交临时工件四件套"));
        finalizeTempWorkpiece();
    }
}

void MainWindow::selectTextureImage() {
    QString readinessError;
    if (pointTaskRunning() || !textureRasterMappingReady(&readinessError)
        || !m_planeCandidateConfirmed || !m_threePlaneResult.ok
        || m_threePlaneResult.planeIndices.isEmpty()) {
        statusBar()->showMessage(pointTaskRunning()
            ? tr("点云处理任务正在运行")
            : (!readinessError.isEmpty() ? readinessError : tr("请先完成两组三点平面校验")));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择同次扫描亮度图"), QString(),
        tr("亮度图 (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty()) return;
    QImage image(path);
    if (image.isNull() || image.width() < 1 || image.height() < 1) {
        QMessageBox::warning(this, tr("亮度图无效"),
                             tr("无法读取 PNG/JPG/BMP 亮度图：%1").arg(path));
        return;
    }
    if (!m_textureMappingWatcher) {
        m_textureMappingWatcher = new QFutureWatcher<pcv::texture::MappingResult>(this);
        connect(m_textureMappingWatcher,
                &QFutureWatcher<pcv::texture::MappingResult>::finished,
                this, &MainWindow::textureMappingFinished);
    }
    if (m_textureCancelToken) m_textureCancelToken->store(true);
    m_textureCancelToken = std::make_shared<std::atomic_bool>(false);
    const QVector<pointcloud::Point3D> source = m_points;
    const QVector<int> planeIndices = m_threePlaneResult.planeIndices;
    const QVector<qsizetype> sourceIndices = m_tempWorkpieceSourceIndices;
    pcv::texture::MappingOptions options;
    options.sourceIndices = sourceIndices;
    options.declaredPointCount = m_tempWorkpieceDeclaredPointCount;
    options.rejectedInvalidPointCount = m_tempWorkpieceRejectedInvalidPointCount;
    options.rejectedRangePointCount = m_tempWorkpieceRejectedRangePointCount;
    options.isCancelled = [token = m_textureCancelToken]() {
        return token && token->load();
    };
    m_textureInputRevision = m_canvasRevision;
    m_pendingTextureImagePath = path;
    m_progress->show();
    m_progress->setRange(0, 0);
    m_edgeOutput->setPlainText(tr("正在按原始 PLY source index 将同次扫描亮度图映射到拟合平面...\n图像尺寸：%1 × %2 px")
        .arg(image.width()).arg(image.height()));
    statusBar()->showMessage(tr("后台执行亮度图平面映射..."));
    updatePlaneEdgeUi();
    m_textureMappingWatcher->setFuture(QtConcurrent::run(
        [source, planeIndices, image, options]() {
            return pcv::texture::mapPlaneBrightness(source, planeIndices, image, options);
        }));
}

void MainWindow::textureMappingFinished() {
    const pcv::texture::MappingResult result = m_textureMappingWatcher->result();
    m_progress->hide();
    if (m_textureInputRevision != m_canvasRevision) {
        m_pendingTextureImagePath.clear();
        m_edgeOutput->setPlainText(tr("点云已变化，旧亮度图映射已丢弃。"));
        updatePlaneEdgeUi();
        statusBar()->showMessage(tr("旧亮度图映射已丢弃"));
        return;
    }
    if (result.cancelled) {
        m_pendingTextureImagePath.clear();
        m_edgeOutput->setPlainText(tr("亮度图映射已取消。"));
        updatePlaneEdgeUi();
        statusBar()->showMessage(tr("亮度图映射已取消"));
        return;
    }
    if (!result.ok) {
        m_pendingTextureImagePath.clear();
        m_edgeOutput->setPlainText(result.error);
        updatePlaneEdgeUi();
        statusBar()->showMessage(result.error);
        return;
    }
    m_canvas->setTextureGrayOverlay(result.grayscale, result.valid);
    m_textureImagePath = m_pendingTextureImagePath;
    m_pendingTextureImagePath.clear();
    {
        const QSignalBlocker blocker(m_colorMode);
        m_colorMode->setCurrentIndex(3);
    }
    updateRenderSettings();
    m_edgeOutput->setPlainText(
        tr("同次扫描亮度图已按原始栅格索引贴到 3D 平面点\n映射方式：%1\n图像尺寸：%2 × %3 px\n"
           "原始 PLY 点数：%4\n当前点数：%5\n平面点总数：%6\n成功映射：%7\n跳过：%8\n"
           "规则：左上角起始、row-major、仅使用 planeIndices 对应像素")
            .arg(result.mappingMode)
            .arg(result.imageWidth).arg(result.imageHeight)
            .arg(QLocale().toString(result.sourcePointCount))
            .arg(QLocale().toString(result.currentPointCount))
            .arg(QLocale().toString(result.planePointCount))
            .arg(QLocale().toString(result.mappedPointCount))
            .arg(QLocale().toString(result.skippedPointCount)));
    updatePlaneEdgeUi();
    statusBar()->showMessage(tr("纹理 %1 × %2 px · PLY %3 点 · 平面 %4 点 · 映射 %5 · 跳过 %6 · %7")
        .arg(result.imageWidth).arg(result.imageHeight)
        .arg(QLocale().toString(result.sourcePointCount))
        .arg(QLocale().toString(result.planePointCount))
        .arg(QLocale().toString(result.mappedPointCount))
        .arg(QLocale().toString(result.skippedPointCount)).arg(result.mappingMode));
}

void MainWindow::finalizeTempWorkpiece() {
    QString validationError;
    if (!tempWorkpieceFinalizeReady(&validationError)) {
        if (hasTempWorkpieceSession())
            failAutomaticTempWorkpieceStage(tr("四件套提交前置校验"), validationError);
        else
            statusBar()->showMessage(validationError);
        return;
    }
    const QStringList outputNames{
        QStringLiteral("baseline_robot_base.ply"),
        QStringLiteral("roi_template_robot_base.ply"),
        QStringLiteral("plane_mask.png"),
        QStringLiteral("temp_workpiece_info.json")};
    // The plain-PLY line blocks any existing target in the parameter dialog and
    // keeps the transaction fail-closed (allowOverwrite = false). The JSON entry
    // keeps its existing explicit overwrite confirmation.
    const bool plyLine = m_tempWorkpieceSource == TempWorkpieceSource::Ply;
    bool allowOverwrite = !plyLine;
    if (!plyLine) {
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
    } else {
        const QStringList existingOutputs = existingWorkpieceArtifacts(m_tempWorkpieceOutputDirectory);
        if (!existingOutputs.isEmpty()) {
            const QString detail = tr("输出目录已存在正式文件，请更换目录：%1")
                .arg(existingOutputs.join(QStringLiteral("、")));
            failAutomaticTempWorkpieceStage(tr("四件套提交前检查"), detail);
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
    options.allowOverwrite = allowOverwrite;
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
    setWorkflowState(WorkflowState::FinalizingArtifacts,
                     tr("正在事务性提交临时工件四件套..."));
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
        // No partial formal result was published; the converted cloud and the
        // confirmed plane stay available for inspection or a retry.
        appendTempWorkpieceOutput(tr("自动流程失败阶段：四件套事务提交：%1").arg(detail));
        setWorkflowState(WorkflowState::Failed, detail);
        QMessageBox::critical(this, tr("临时工件输出失败"), detail);
        updatePlaneEdgeUi();
        return;
    }
    invalidateTempWorkpieceSession();
    updatePlaneExtractionUi();
    updatePlaneEdgeUi();
    appendTempWorkpieceOutput(tr("临时工件四件套已提交：\n%1\n%2\n%3\n%4")
        .arg(result.tempWorkpieceInfoPath, result.baselineRobotBasePly,
             result.roiTemplateRobotBasePly, result.planeMaskPng));
    setWorkflowState(WorkflowState::Completed, tr("临时工件四件套已提交：%1\n%2\n%3\n%4")
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
        setPickingOperation(tr("选择第二组三点"), true);
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
    if (m_selectedPointIndices.size() == 2
        && (m_tempWorkpieceSource == TempWorkpieceSource::Ply || m_textureExperimentActive))
        m_planeElapsed.start();
    m_selectedPointIndices.push_back(index);
    m_canvas->setSelectedIndices(m_selectedPointIndices);
    updatePlaneExtractionUi();
    setPickingOperation(m_secondPlaneSelectionActive ? tr("选择第二组三点")
                                                       : tr("选择第一组三点"), true);
    if (m_selectedPointIndices.size() < 3) {
        statusBar()->showMessage(tr("请选择第 %1 个点").arg(m_selectedPointIndices.size() + 1));
        return;
    }

    updatePlaneExtractionUi();
    if (m_tempWorkpieceSource == TempWorkpieceSource::Ply || m_textureExperimentActive) {
        // The plain-PLY line runs the plane fit as soon as P1/P2/P3 are
        // selected; the operator never has to click the extraction button.
        // Esc/Backspace still abort or rewind the selection.
        runPlaneExtraction(false, true);
        return;
    }
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
    if (m_planeDialog) m_planeDialog->hide();
    setPickingOperation(tr("选择第二组三点"), true);
    setWorkflowState(WorkflowState::SelectingSecondaryPlanePoints);
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
    setWorkflowState(WorkflowState::SelectingSecondaryPlanePoints);
    statusBar()->showMessage(tr("已取消第二组三点校验"));
    setPickingOperation(QString(), false);
    showPlaneDialog();
    updatePlaneExtractionUi();
}

void MainWindow::validateSecondPlaneSelection() {
    if (m_secondPlanePointIndices.size() != 3 || !m_threePlaneResult.ok) return;
    setWorkflowState(WorkflowState::ValidatingSecondaryPlane);
    const float angleTolerance = float(m_defaults.planeAngleToleranceDeg);
    const float distanceTolerance = float(m_defaults.planeDistanceToleranceMm);
    bool autoConfirmPlane = false;
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
        autoConfirmPlane = hasTempWorkpieceSession() || m_textureExperimentActive;
        if (hasTempWorkpieceSession()) setWorkflowState(WorkflowState::FinalizingArtifacts);
        statusBar()->showMessage(m_textureExperimentActive
            ? tr("第二组三点同面校验通过，可以选择同次扫描亮度图")
            : hasTempWorkpieceSession()
            ? tr("第二组三点确认属于同一平面，将自动建立 WObj1 并生成四件套")
            : tr("第二组三点确认属于同一平面，系统将按机器人基坐标轴自动建立 WObj1"));
    } else if (validation.status == pointcloud::PlaneConsistencyStatus::Collinear
               || validation.status == pointcloud::PlaneConsistencyStatus::InvalidInput
               || validation.status == pointcloud::PlaneConsistencyStatus::ReusedPoint) {
        const QString error = validation.error;
        resetSecondPlaneVerification();
        m_canvas->setSelectedIndices(m_selectedPointIndices);
        setWorkflowState(WorkflowState::SelectingSecondaryPlanePoints);
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
        setWorkflowState(WorkflowState::SelectingPrimaryPlanePoints);
        statusBar()->showMessage(tr("当前候选已清除，请重新选择第一组平面控制点"));
    }
    updatePlaneExtractionUi();
    updatePlaneEdgeUi();
    if (m_threePointSelectionActive) {
        if (m_planeDialog) m_planeDialog->hide();
        setPickingOperation(tr("选择第一组三点"), true);
    } else {
        setPickingOperation(QString(), false);
        showPlaneDialog();
    }
    if (autoConfirmPlane)
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

void MainWindow::runPlaneExtraction(bool deferFinalClassification, bool fromThirdPick) {
    if (pointTaskRunning()) return;
    if (m_selectedPointIndices.size() < 3) {
        statusBar()->showMessage(tr("请先在画布中指定至少三个点"));
        return;
    }
    if (!fromThirdPick) m_planeElapsed.start();
    m_planeFramePending = false;
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
    options.useZAxisResidual = !m_textureExperimentActive;
    options.maxNormalTiltDegrees = 45.0f;
    options.deferFinalClassification = deferFinalClassification;
    options.parallelEnabled = m_points.size() >= 100000;
    options.maxParallelTasks = 4;
    m_planeCancelToken = std::make_shared<std::atomic_bool>(false);
    const pointcloud::CancellationToken token = m_planeCancelToken;
    options.isCancelled = [token]() { return token->load(); };
    if (!m_threePlaneWatcher) {
        m_threePlaneWatcher = new QFutureWatcher<pointcloud::ThreePointPlaneResult>(this);
        connect(m_threePlaneWatcher, &QFutureWatcher<pointcloud::ThreePointPlaneResult>::finished,
                this, &MainWindow::planeExtractionFinished);
    }
    const QVector<pointcloud::Point3D> source = m_points;
    const QVector<int> seeds = m_selectedPointIndices;
    m_threePlaneInputRevision = m_canvasRevision;
    qInfo() << "plane_fit_start seeds=" << seeds << "points=" << source.size()
            << "z_residual=" << options.useZAxisResidual << "parallel=" << options.parallelEnabled;
    m_threePointSelectionActive = false;
    m_canvas->setSelectionMode(false);
    m_progress->show();
    m_progress->setRange(0, 0);
    setWorkflowState(WorkflowState::ValidatingPrimaryPlane);
    statusBar()->showMessage(deferFinalClassification
        ? tr("快速拟合候选平面...") : tr("正在完成平面分类和连通域处理..."));
    updatePlaneExtractionUi();
    m_threePlaneWatcher->setFuture(QtConcurrent::run([source, seeds, options]() {
        return pointcloud::extractPlaneFromPoints(source, seeds, options);
    }));
}

void MainWindow::planeExtractionFinished() {
    if (m_closing) return;
    QElapsedTimer publishTimer;
    publishTimer.start();
    const pointcloud::ThreePointPlaneResult result = m_threePlaneWatcher->result();
    m_progress->hide();
    if (m_threePlaneInputRevision != m_canvasRevision) {
        statusBar()->showMessage(tr("画布缓存已变化，已丢弃旧三点平面结果"));
        return;
    }
    if (result.cancelled || (m_planeCancelToken && m_planeCancelToken->load())) {
        m_threePlaneResult = {};
        updatePlaneExtractionUi();
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
        setWorkflowState(WorkflowState::SelectingPrimaryPlanePoints);
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
        setWorkflowState(WorkflowState::SelectingPrimaryPlanePoints);
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
          << (m_textureExperimentActive ? tr("平面垂直距离 RMS：%1 mm")
                                        : tr("Z 方向 RMS：%1 mm")).arg(result.rmsError, 0, 'g', 7)
          << (m_textureExperimentActive
              ? tr("平面包围盒中心（实验参考中心）：X %1  Y %2  Z %3 mm")
              : tr("平面包围盒中心（WObj1 原点 O）：X %1  Y %2  Z %3 mm"))
                 .arg(m_planeCenter.x(), 0, 'f', 3)
                 .arg(m_planeCenter.y(), 0, 'f', 3)
                 .arg(m_planeCenter.z(), 0, 'f', 3)
          << QString() << (result.deferred
              ? tr("快速候选平面已生成，请确认后完成全量分类")
              : tr("候选平面已生成，请进行第二组三点验证"));
    m_threeOutput->setPlainText(lines.join(QLatin1Char('\n')));
    updatePlaneExtractionUi();
    updatePlaneEdgeUi();
    setWorkflowState(WorkflowState::SelectingSecondaryPlanePoints);
    if ((m_tempWorkpieceSource == TempWorkpieceSource::Ply || m_textureExperimentActive)
        && !result.deferred) {
        // The plain-PLY line chains the second triplet automatically: the two
        // groups of three points are its only manual steps.
        startSecondPlanePointSelection();
    } else if (!result.deferred) {
        statusBar()->showMessage(tr("平面已生成"));
    } else {
        statusBar()->showMessage(tr("候选平面已生成"));
    }
    const auto &timings = result.timings;
    qInfo() << "plane_fit_phases_ms candidate=" << timings.candidateMs
            << "ransac=" << timings.ransacMs << "pca=" << timings.pcaMs
            << "classification=" << timings.classificationMs << "connectivity=" << timings.connectivityMs
            << "assembly=" << timings.assemblyMs << "worker_total=" << timings.totalMs
            << "ui_publish=" << publishTimer.nsecsElapsed() / 1.0e6;
    m_planeFramePending = true;
}

void MainWindow::confirmPlaneCandidate() {
    if (!m_threePlaneResult.ok || pointTaskRunning()) {
        if (hasTempWorkpieceSession())
            failAutomaticTempWorkpieceStage(
                tr("WObj1 前置校验"),
                pointTaskRunning() ? tr("已有点云处理任务正在运行")
                                   : tr("当前没有有效的拟合平面"));
        return;
    }
    if (m_planeFinalizationPending) {
        runPlaneExtraction(false);
        return;
    }
    if (!m_secondPlaneValidated || !m_secondPlaneSamePlane) {
        const QString detail = tr("请先完成并通过第二组三点平面验证");
        if (hasTempWorkpieceSession()) failAutomaticTempWorkpieceStage(tr("WObj1 前置校验"), detail);
        else statusBar()->showMessage(detail);
        return;
    }
    if (m_textureExperimentActive) {
        m_planeCandidateConfirmed = true;
        m_threePointSelectionActive = false;
        m_secondPlaneSelectionActive = false;
        m_canvas->setSelectionMode(false);
        setPickingOperation(QString(), false);
        m_threeOutput->appendPlainText(tr("\n实验平面已确认（PLY 原始坐标），请选择同次扫描亮度图"));
        updatePlaneExtractionUi();
        updatePlaneEdgeUi();
        setWorkflowState(WorkflowState::Completed);
        showEdgeDialog();
        statusBar()->showMessage(tr("实验平面已确认，请选择同次扫描亮度图"));
        return;
    }
    if (!m_planeCenterValid) {
        const QString detail = tr("当前平面包围盒中心无效，无法建立 WObj1");
        if (hasTempWorkpieceSession()) failAutomaticTempWorkpieceStage(tr("WObj1 前置校验"), detail);
        else statusBar()->showMessage(detail);
        return;
    }
    if (m_selectedPointIndices.size() < 3) {
        const QString detail = tr("建立 WObj1 需要有效的第一组平面控制点");
        if (hasTempWorkpieceSession()) failAutomaticTempWorkpieceStage(tr("WObj1 前置校验"), detail);
        else statusBar()->showMessage(detail);
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
        failAutomaticTempWorkpieceStage(tr("WObj1 自动辅助点选择"), error);
        return;
    }
    frame = pointcloud::buildWorkpieceCoordinateSystemFromThreePoints(
        m_points, m_planeCenter, m_xAxisPointIndex, m_yAxisPointIndex);
    if (!frame.valid) {
        statusBar()->showMessage(frame.error);
        m_threeOutput->appendPlainText(QStringLiteral("\n") + frame.error);
        failAutomaticTempWorkpieceStage(tr("WObj1 坐标系构造"), frame.error);
        return;
    }
    m_workpieceCoordinate = frame;
    ++m_coordinateFrameRevision;
    clearPlaneEdgeUi();
    m_planeCandidateConfirmed = true;
    m_threePointSelectionActive = false;
    m_secondPlaneSelectionActive = false;
    m_canvas->setSelectionMode(false);
    setPickingOperation(QString(), false);
    showPlaneDialog();
    m_canvas->setWorkpieceCoordinateSystem(toRenderCoordinateFrame(m_workpieceCoordinate));
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
    if (hasTempWorkpieceSession()) {
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
        const QString detail = tr("已有点云处理任务正在运行");
        if (hasTempWorkpieceSession()) failAutomaticTempWorkpieceStage(tr("边缘分割前置校验"), detail);
        else statusBar()->showMessage(detail);
        return;
    }
    if (!m_planeCandidateConfirmed || !m_threePlaneResult.ok) {
        const QString detail = tr("请先在平面提取页确定候选平面");
        if (hasTempWorkpieceSession()) failAutomaticTempWorkpieceStage(tr("边缘分割前置校验"), detail);
        else statusBar()->showMessage(detail);
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
        if (hasTempWorkpieceSession())
            failAutomaticTempWorkpieceStage(tr("边缘分割结果发布"),
                                             tr("画布缓存已变化，已丢弃旧边缘结果"));
        else {
            updatePlaneEdgeUi();
            statusBar()->showMessage(tr("画布缓存已变化，已丢弃旧边缘结果"));
        }
        return;
    }
    if (!result.ok) {
        m_planeEdgeResult = {};
        m_edgeOutput->setPlainText(result.error);
        updatePlaneEdgeUi();
        // The automatic chain stops here; the converted cloud and the confirmed
        // plane stay available, and no partial formal result is produced.
        failAutomaticTempWorkpieceStage(tr("边缘分割"), result.error);
        return;
    }
    m_planeEdgeResult = result;
    m_canvas->setPlaneResult(m_threePlaneResult.planeIndices,
                             result.edgeIndices, toRenderContours(result.contours));
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
    if (m_edgeDialog) m_edgeDialog->hide();
    setPickingOperation(tr("选择边缘真实点"), true);
    m_selectedEdgeIndices.clear();
    m_canvas->setEdgeSelectionMode(true, m_planeEdgeResult.edgeIndices);
    m_canvas->setSelectedEdgeIndices({});
    updatePlaneEdgeUi();
    statusBar()->showMessage(tr("请在画布中选择边缘真实点，Esc 取消"));
}

void MainWindow::finishEdgePointSelection() {
    if (!m_edgeSelectionActive) return;
    m_edgeSelectionActive = false;
    if (m_canvas) m_canvas->setEdgeSelectionMode(false, {});
    updatePlaneEdgeUi();
    setPickingOperation(QString(), false);
    showEdgeDialog();
    statusBar()->showMessage(tr("边缘点选择完成：%1 个点").arg(m_selectedEdgeIndices.size()));
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
    setPickingOperation(QString(), false);
    showEdgeDialog();
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
    setPickingOperation(tr("选择边缘真实点"), true);
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
        metadata.sourcePlyEncoding = m_pendingSourcePlyEncoding;
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
    m_noiseCloudSource = m_currentCloudSource;
    m_noiseCloudSource.processingSteps.append(QJsonObject{
        {QStringLiteral("operation"), QStringLiteral("remove_noise")},
        {QStringLiteral("voxel_enabled"), options.voxelEnabled},
        {QStringLiteral("voxel_size_mm"), options.voxelSize},
        {QStringLiteral("statistical_enabled"), options.statisticalEnabled},
        {QStringLiteral("mean_k"), options.meanK},
        {QStringLiteral("stddev_multiplier"), options.stddevMultiplier}});
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
    publishCanvasCache(result.points, false, m_noiseCloudSource);
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
    cancelAndWaitWorkpieceTemplate();
    if (m_memoryTimer) m_memoryTimer->stop();
    if (m_stitchingWindow) {
        disconnect(m_stitchingWindow, nullptr, this, nullptr);
        m_stitchingWindow->cancelAndWait();
        m_stitchingWindow->hide();
    }
    if (m_planeCancelToken) m_planeCancelToken->store(true);
    m_planeFramePending = false;
    qWarning() << "MainWindow closeEvent, spontaneous=" << event->spontaneous()
               << "visible=" << isVisible()
               << "loading=" << m_loading
               << "loadRunning="
               << (m_loadWatcher && m_loadWatcher->isRunning());
    setEnabled(false);
    // Stop the long-running plain-PLY preparation worker through the shared
    // cancellation token before waiting for it below.
    if (m_prepareCancelToken) m_prepareCancelToken->store(true);
    if (m_scanPlanningCancelToken) m_scanPlanningCancelToken->store(true);
    if (m_textureCancelToken) m_textureCancelToken->store(true);
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
    if (m_scanPlanningWatcher) disconnect(m_scanPlanningWatcher, nullptr, this, nullptr);
    if (m_textureMappingWatcher) disconnect(m_textureMappingWatcher, nullptr, this, nullptr);
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
    if (m_scanPlanningWatcher && m_scanPlanningWatcher->isRunning()) {
        statusBar()->showMessage(tr("正在结束后台扫描路径规划，请稍候..."));
        m_scanPlanningWatcher->waitForFinished();
    }
    if (m_textureMappingWatcher && m_textureMappingWatcher->isRunning()) {
        statusBar()->showMessage(tr("正在结束后台亮度图映射，请稍候..."));
        m_textureMappingWatcher->waitForFinished();
    }
    event->accept();
}


