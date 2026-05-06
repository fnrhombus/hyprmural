#pragma once

#include <string>

namespace hm {

// Install SIGCHLD = SIG_IGN so child processes are auto-reaped without
// accumulating zombies. Call once at startup, before any fire_hook calls.
void install_hook_reaper();

// Fire-and-forget: fork+exec `/bin/sh -c <cmd>` with HYPRMURAL_* env vars
// set. Returns immediately; child runs detached. Caller responsibility:
// have install_hook_reaper() called.
//
// Empty `cmd` is a no-op.
void fire_hook(const std::string& cmd,
               const std::string& monitor,
               const std::string& workspace,
               const std::string& image);

}  // namespace hm
