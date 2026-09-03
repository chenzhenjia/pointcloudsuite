#include <iostream>
#include <string>

#ifndef PCV_PROJECT_VERSION
#define PCV_PROJECT_VERSION "unknown"
#endif

namespace {

void printUsage(std::ostream& stream) {
    stream << "Usage: pointcloudcli [--help] [--version]\n"
           << "Options:\n"
           << "  --help     Show this help message.\n"
           << "  --version  Show the PointCloudSuite version.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc == 1) {
        std::cerr << "error: missing command-line option\n";
        printUsage(std::cerr);
        return 2;
    }

    const std::string argument = argv[1];
    if (argc == 2 && argument == "--help") {
        printUsage(std::cout);
        return 0;
    }
    if (argc == 2 && argument == "--version") {
        std::cout << "pointcloudcli " << PCV_PROJECT_VERSION << '\n';
        return 0;
    }

    std::cerr << "error: unknown option: " << argument << '\n';
    printUsage(std::cerr);
    return 2;
}
