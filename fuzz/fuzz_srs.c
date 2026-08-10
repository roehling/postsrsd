#include <srs2.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    srs_t* srs = srs_new();
    if (srs == NULL)
        abort();
    srs_add_secret(srs, "tops3cr3t");
    srs_set_hashlength(srs, 4);
    srs_set_hashmin(srs, 4);
    srs->faketime = 1577836860;

    char* input = malloc(size + 1);
    if (input == NULL)
        abort();
    memcpy(input, data, size);
    input[size] = 0;

    char output[4096];
    srs_reverse(srs, output, sizeof(output), input);

    free(input);
    srs_free(srs);
    return 0;
}
