#include "task_runtime.h"
#include <QCoreApplication>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QRegularExpression separators(QStringLiteral("[\\s,，;]+"));
    pointcloud::RobotPose pose;
    if (!pcv::ui::parseRobotPose("1,2，3;4 5 6", &pose, separators)
        || pose.x != 1 || pose.rz != 6) return 1;
    for (const auto *invalid : {"1 2 3", "1 2 3 4 5 6 7", "1 2 3 4 5 nan", "1 2 3 4 5 inf"})
        if (pcv::ui::parseRobotPose(QString::fromUtf8(invalid), &pose, separators)) return 1;
    auto current = std::make_shared<std::atomic_bool>(false);
    auto replacement = std::make_shared<std::atomic_bool>(false);
    if (!pcv::ui::acceptsTask(2, 2, current, current)
        || pcv::ui::acceptsTask(1, 2, current, current)
        || pcv::ui::acceptsTask(2, 2, current, replacement)
        || pcv::ui::acceptsTask(2, 2, {}, current)) return 1;
    current->store(true);
    if (pcv::ui::acceptsTask(2, 2, current, current)) return 1;
    return 0;
}
