#pragma once

#include "ronq/core/fd.hpp"

#include <sys/types.h>

struct BgProcess {
    pid_t pid = -1;
    pid_t pgid = -1;
    FileDescriptor read_fd;
};

struct SpawnedProcess {
    pid_t pid = -1;
    pid_t pgid = -1;
};
