// Cross-platform L2NPC process console. This portable composition adapter
// replaces the shipping Win32 status-window input surface. It is not a
// recovered Shared or PDB class.
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace l2npc::portable {

class Console {
public:
    using Handler = std::function<void(const std::vector<std::string>& args)>;

    // Register a command: name without the leading dot, help text, handler.
    void Register(const std::string& name, const std::string& help, Handler h);

    // Run until `.quit`/`.exit` or until keepRunning returns false. EOF on
    // stdin disables the interactive shell but does not stop a headless
    // service.
    int Run(const std::function<bool()>& keepRunning = {});

    // Execute a single command line (used by Run and could be scripted).
    void Execute(const std::string& line);

    bool ShouldQuit() const { return quit_.load(); }
    void RequestQuit() { quit_.store(true); }

private:
    struct Cmd {
        std::string help;
        Handler fn;
    };
    std::map<std::string, Cmd> cmds_;
    std::atomic<bool> quit_{false};

    void printHelp() const;
};

} // namespace l2npc::portable
