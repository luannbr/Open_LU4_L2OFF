#include "Shared/src/Runtime/SerialKey.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void PrintUsage(const char* executable) {
    std::cout
        << "Usage: " << executable
        << " [--output FILE] [--all]\n"
        << "Generate a SerialKey compatible with the recovered CacheD and "
           "L2Server validators for this host.\n";
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path outputPath;
    bool printAll = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            PrintUsage(argv[0]);
            return 0;
        }
        if (argument == "--all") {
            printAll = true;
            continue;
        }
        if (argument == "--output" || argument == "-o") {
            if (++index >= argc) {
                std::cerr << argument << " requires a file path\n";
                return 2;
            }
            outputPath = argv[index];
            continue;
        }
        std::cerr << "unknown argument: " << argument << '\n';
        PrintUsage(argv[0]);
        return 2;
    }

    const runtime::SerialKeyHostIdentity identity =
        runtime::ReadSerialKeyHostIdentity();
    const std::vector<std::string> serialKeys =
        runtime::GenerateSerialKeys(identity);
    if (serialKeys.empty()) {
        std::cerr
            << "no target-visible network adapter was found; "
               "a host-bound key cannot be generated\n";
        return 1;
    }
    if (!runtime::ValidateSerialKey(serialKeys.front(), identity)) {
        std::cerr << "generated key failed its own validation\n";
        return 1;
    }

    if (!outputPath.empty()) {
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            std::cerr << "cannot open output file: " << outputPath << '\n';
            return 1;
        }
        output.write(
            serialKeys.front().data(),
            static_cast<std::streamsize>(serialKeys.front().size()));
        if (!output) {
            std::cerr << "cannot write output file: " << outputPath << '\n';
            return 1;
        }
    }

    const std::size_t count = printAll ? serialKeys.size() : 1U;
    for (std::size_t index = 0; index < count; ++index) {
        std::cout << serialKeys[index] << '\n';
    }
    return 0;
}
