#pragma once

// Display-less commands. Returns -1 when argv is not a CLI command, so the
// caller opens the window; otherwise the process exit code.
int runCli(int argc, char *argv[]);
