#include "c6_file.h"
#include <limits.h>
bool c6_file_read(FILE *file, uint32_t base, uint32_t length,
                  size_t offset, void *out, size_t size) {
    if (!file || !out || offset > length || size > length-offset ||
        (uint64_t)base+offset > LONG_MAX) return false;
    if (fseek(file, (long)((uint64_t)base+offset), SEEK_SET) != 0) return false;
    return fread(out, 1, size, file) == size;
}
