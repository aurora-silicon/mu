#include <Base.h>
#define AsciiSPrint(buf, n, fmt, ...) snprintf ((buf), (n), "/cpus/cpu%d", __VA_ARGS__)
