#include <netstring.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    char output[1024];
    size_t decoded_length = 0;
    netstring_decode(data, size, output, sizeof(output), &decoded_length);

    int fds[2];
    if (pipe(fds) < 0)
        abort();
    if (size > 4096)
        size = 4096;
    if (write(fds[1], data, size) != (ssize_t)size)
        abort();
    close(fds[1]);
    netstring_read(fds[0], output, sizeof(output), &decoded_length);
    close(fds[0]);
    return 0;
}
