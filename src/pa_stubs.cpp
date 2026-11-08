// Skia собрана с PartitionAlloc. Его allocator shim ссылается на символы __real_*
// (цели линкерного --wrap). Мы --wrap не используем, поэтому shim не активен,
// а эти заглушки просто вызывают обычные функции libc (рекурсии нет: подмены нет).
#include <cstddef>
#include <cstdlib>
#include <unistd.h>

extern "C" {
void __real_free(void *p) { free(p); }
char *__real_realpath(const char *path, char *resolved) { return realpath(path, resolved); }
char *__real_getcwd(char *buf, size_t size) { return getcwd(buf, size); }
}
