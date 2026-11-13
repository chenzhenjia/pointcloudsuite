#pragma once

#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTabBar>
#include <QVBoxLayout>
#include <QVariant>
#include <functional>

// Application-only shell. Widgets remain owned and scheduled by their existing
// controllers; navigating never calls close(), reject() or cancelAndWait().
class WorkbenchShellV1 final : public QWidget {
public:
    explicit WorkbenchShellV1(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("workbench_shell_v1");
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0); root->setSpacing(0);
        header = new QWidget(this); header->setObjectName("workbench_header");
        header->setFixedHeight(36);
        headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(12, 4, 12, 4); headerLayout->setSpacing(8);
        auto *brand = new QLabel("PointCloudView", header);
        brand->setObjectName("workbench_brand"); headerLayout->addWidget(brand);
        pagePicker = new QComboBox(header); pagePicker->setObjectName("workbench_page_picker");
        pagePicker->setAccessibleName(tr("工作页"));
        const QStringList ids{"main", "camera", "depth_raw", "stitch", "selection", "cad", "template"};
        const QStringList titles{tr("点云工作台"), tr("相机采集"), tr("深度图取流"), tr("多帧拼接"),
                                 tr("六分屏选面"), tr("STEP 模板"), tr("工件模板")};
        for (int i = 0; i < ids.size(); ++i) pagePicker->addItem(titles.at(i), ids.at(i));
        headerLayout->addWidget(pagePicker); headerLayout->addStretch();
        root->addWidget(header);
        toolbar = new QWidget(this); toolbar->setObjectName("preview_toolbar"); toolbar->setFixedHeight(26);
        toolbarLayout = new QHBoxLayout(toolbar);
        toolbarLayout->setContentsMargins(8, 1, 8, 1); toolbarLayout->setSpacing(4);
        root->addWidget(toolbar);
        body = new QWidget(this); auto *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(0, 0, 0, 0);
        pages = new QStackedWidget(body); pages->setObjectName("workbench_pages");
        bodyLayout->addWidget(pages); root->addWidget(body, 1);
        for (int i = 0; i < ids.size(); ++i) {
            auto *page = new QWidget(pages); auto *layout = new QVBoxLayout(page);
            layout->setContentsMargins(12, 8, 12, 8); layout->setSpacing(8);
            if (i != 0) {
                auto *row = new QHBoxLayout; row->addWidget(new QLabel(titles.at(i), page)); row->addStretch();
                auto *back = new QPushButton(tr("返回工作台"), page); row->addWidget(back);
                connect(back, &QPushButton::clicked, this, [this] { showPage("main"); });
                layout->addLayout(row);
            } else layout->setContentsMargins(0, 0, 0, 0);
            auto *host = new QScrollArea(page); host->setWidgetResizable(true); host->setFrameShape(QFrame::NoFrame);
            if (i == 0) { delete host; host = nullptr; }
            if (host) layout->addWidget(host, 1);
            pages->addWidget(page); pageIndices.insert(ids.at(i), i); pageHosts.insert(ids.at(i), host);
            pageLayouts.insert(ids.at(i), layout);
        }
        drawer = new QWidget(body); drawer->setObjectName("workbench_drawer");
        drawer->setAttribute(Qt::WA_StyledBackground);
        auto *dl = new QVBoxLayout(drawer); dl->setContentsMargins(10, 8, 10, 8); dl->setSpacing(8);
        auto *row = new QHBoxLayout;
        drawerTitle = new QLabel(tr("工具"), drawer); row->addWidget(drawerTitle); row->addStretch();
        auto *close = new QPushButton(tr("关闭"), drawer); close->setObjectName("drawer_close"); row->addWidget(close);
        connect(close, &QPushButton::clicked, this, [this] { hideDrawer(); }); dl->addLayout(row);
        groups = new QTabBar(drawer); groups->setExpanding(true);
        for (const QString &group : {tr("数据"), tr("处理"), tr("输出"), tr("高级")}) groups->addTab(group);
        dl->addWidget(groups);
        drawerPages = new QStackedWidget(drawer); dl->addWidget(drawerPages, 1);
        auto *navigation = new QWidget(drawerPages); auto *nl = new QVBoxLayout(navigation);
        nl->setContentsMargins(0, 0, 0, 0); lists = new QStackedWidget(navigation); nl->addWidget(lists);
        for (int i = 0; i < 4; ++i) {
            auto *list = new QWidget(lists); auto *ll = new QVBoxLayout(list);
            ll->setContentsMargins(0, 0, 0, 0); ll->setSpacing(8); ll->addStretch(); lists->addWidget(list);
        }
        drawerPages->addWidget(navigation);
        connect(groups, &QTabBar::currentChanged, this, [this](int index) {
            lists->setCurrentIndex(index); drawerPages->setCurrentIndex(0); drawerTitle->setText(tr("工具"));
        });
        drawer->hide(); body->installEventFilter(this);
        connect(pagePicker, qOverload<int>(&QComboBox::activated), this, [this](int index) {
            const QString id = pagePicker->itemData(index).toString();
            showPage(id); if (activatePage) activatePage(id);
        });
    }

    void addTool(int group, const QString &title, std::function<void()> action) {
        auto *list = lists->widget(group); auto *button = new QPushButton(title, list);
        button->setMinimumHeight(28);
        auto *layout = qobject_cast<QVBoxLayout *>(list->layout());
        layout->insertWidget(layout->count() - 1, button);
        connect(button, &QPushButton::clicked, this, [action] { action(); });
    }
    void setMainWidget(QWidget *widget) {
        pageLayouts.value("main")->addWidget(widget); widget->show();
    }
    void embedPage(const QString &id, QWidget *widget) {
        if (hosted.value(id) == widget) return;
        auto *host = pageHosts.value(id); if (!host) return;
        if (host->widget()) {
            auto *previous = host->takeWidget(); previous->setParent(this); previous->hide();
        }
        widget->setWindowFlags(Qt::Widget); widget->setMinimumSize(0, 0);
        widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        if (auto *dialog = qobject_cast<QDialog *>(widget)) dialog->setModal(false);
        host->setWidget(widget); hosted.insert(id, widget); widget->show();
    }
    bool hasPage(const QString &id) const { return !hosted.value(id).isNull(); }
    void showPage(const QString &id) {
        if (!pageIndices.contains(id)) return;
        const int index = pageIndices.value(id); pages->setCurrentIndex(index);
        const QSignalBlocker blocker(pagePicker); pagePicker->setCurrentIndex(index);
        toolbar->setVisible(id == "main"); hideDrawer();
        if (hosted.value(id)) hosted.value(id)->show();
    }
    void showTool(QWidget *widget, const QString &title) {
        if (!toolHosts.contains(widget)) {
            auto *host = new QScrollArea(drawerPages); host->setWidgetResizable(true); host->setFrameShape(QFrame::NoFrame);
            widget->setWindowFlags(Qt::Widget); widget->setMinimumSize(0, 0);
            if (auto *dialog = qobject_cast<QDialog *>(widget)) dialog->setModal(false);
            host->setWidget(widget); drawerPages->addWidget(host); toolHosts.insert(widget, host);
            widget->installEventFilter(this);
        }
        drawerPages->setCurrentWidget(toolHosts.value(widget)); drawerTitle->setText(title);
        drawer->show(); drawer->raise(); widget->show(); positionDrawer();
    }
    void toggleDrawer() {
        if (drawer->isVisible()) hideDrawer();
        else { drawerPages->setCurrentIndex(0); drawerTitle->setText(tr("工具")); drawer->show(); drawer->raise(); positionDrawer(); }
    }
    void hideDrawer() { drawer->hide(); }
    bool drawerVisible() const { return drawer->isVisible(); }
    void setPageEnabled(const QString &id, bool enabled) {
        const int index = pageIndices.value(id, -1);
        if (auto *model = qobject_cast<QStandardItemModel *>(pagePicker->model()))
            if (index >= 0) model->item(index)->setEnabled(enabled);
    }
    QWidget *header = nullptr, *toolbar = nullptr;
    QHBoxLayout *headerLayout = nullptr, *toolbarLayout = nullptr;
    QComboBox *pagePicker = nullptr;
    QStackedWidget *pages = nullptr;
    std::function<void(const QString &)> activatePage;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == body && event->type() == QEvent::Resize) positionDrawer();
        if (toolHosts.contains(qobject_cast<QWidget *>(watched))) {
            if (event->type() == QEvent::Close || (event->type() == QEvent::KeyPress
                && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)) {
                hideDrawer(); event->ignore(); return true;
            }
        }
        return QWidget::eventFilter(watched, event);
    }
private:
    void positionDrawer() { drawer->setGeometry(qMax(0, body->width() - 360), 0, qMin(360, body->width()), body->height()); }
    QWidget *body = nullptr, *drawer = nullptr;
    QLabel *drawerTitle = nullptr;
    QTabBar *groups = nullptr;
    QStackedWidget *drawerPages = nullptr, *lists = nullptr;
    QHash<QString, int> pageIndices;
    QHash<QString, QScrollArea *> pageHosts;
    QHash<QString, QVBoxLayout *> pageLayouts;
    QHash<QString, QPointer<QWidget>> hosted;
    QHash<QWidget *, QScrollArea *> toolHosts;
};
