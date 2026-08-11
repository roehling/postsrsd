#include <milter.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    char* output;
    output = milter_parse_macros("i", data, size);
    if (output != NULL)
        free(output);
    list_t* L = list_create();
    milter_parse_str_list(L, data, size);
    list_destroy(L, free);
    return 0;
}
