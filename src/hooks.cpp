#include "hooks.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

namespace hm {

void install_hook_reaper() {
    struct sigaction sa {};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDWAIT;  // children auto-reap on exit
    sigaction(SIGCHLD, &sa, nullptr);
}

void fire_hook(const std::string& cmd,
               const std::string& monitor,
               const std::string& workspace,
               const std::string& image) {
    if (cmd.empty()) return;

    const pid_t pid = fork();
    if (pid < 0) {
        std::fprintf(stderr, "hook: fork failed: %s\n", std::strerror(errno));
        return;
    }
    if (pid > 0) return;  // parent

    // child: set env, exec
    setenv("HYPRMURAL_MONITOR", monitor.c_str(), 1);
    setenv("HYPRMURAL_WORKSPACE", workspace.c_str(), 1);
    setenv("HYPRMURAL_IMAGE", image.c_str(), 1);

    execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
    // exec failed
    std::fprintf(stderr, "hook: exec failed: %s\n", std::strerror(errno));
    _exit(127);
}

}  // namespace hm
