#include "fuzz.h"

#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0)
        abort();
    pid_t pid = fork();
    if (pid < 0)
        abort();
    if (pid == 0)
    {
        close(fds[0]);
        postsrsd_t* state = fuzz_state_create();
        if (state == NULL)
            _exit(1);
        handle_socketmap_client(state, fds[1]);
        fuzz_state_destroy(state);
        close(fds[1]);
        _exit(0);
    }
    close(fds[1]);
    if (size > 4096)
        size = 4096;
    if (write(fds[0], data, size) != (ssize_t)size)
        abort();
    shutdown(fds[0], SHUT_WR);
    char buffer[4096];
    while (read(fds[0], buffer, sizeof(buffer)) > 0) /* do nothing */
        ;
    close(fds[0]);
    int status;
    if (waitpid(pid, &status, 0) < 0)
        abort();
    if (WIFSIGNALED(status))
        abort();
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
        abort();
    return 0;
}
