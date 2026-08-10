#include "fuzz.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    fuzz_client(handle_milter_client, data, size);
    return 0;
}
