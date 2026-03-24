#include "ronq/core/pipe.hpp"

#include <unistd.h>

[[nodiscard]] std::expected<std::pair<FileDescriptor, FileDescriptor>,
                            CommandError>
create_pipe() noexcept {
    int fds[2];
    if (::pipe(fds) == -1) {
        return std::unexpected{CommandError::PipeCreationFailed};
    }
    return std::pair{FileDescriptor{fds[0]}, FileDescriptor{fds[1]}};
}
