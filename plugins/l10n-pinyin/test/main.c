#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define handle_error(fmt, ...)                                                                                 \
    do {                                                                                                       \
        if (errno) {                                                                                           \
            fprintf(stderr, fmt ": %s\n", ##__VA_ARGS__, strerror(errno));                                     \
        } else {                                                                                               \
            fprintf(stderr, fmt, ##__VA_ARGS__);                                                               \
        }                                                                                                      \
        exit(EXIT_FAILURE);                                                                                    \
    }                                                                                                          \
    while (0)

int main(int argc, char *argv[]) {
    char path[1024] = {'\0'};
    if (getcwd(path, 1024) == NULL) {
        fprintf(stderr, "ERROR: failed to get current working directory.\n");
        return -1;
    }

    strcat(path, "/libfuzzel-plugin-pinyin.so");
    void *handle = dlopen(path, RTLD_LAZY);
    if (!handle) {
        handle_error("failed to open so: %s\n", path);
    }

    dlerror();

    char *(*func)(char *);
    func = dlsym(handle, "fuzzel_plugin_translate");

    char *error = dlerror();
    if (error != NULL) {
        fprintf(stderr, "%s\n", error);
        exit(EXIT_FAILURE);
    }

    char *ret = (*func)("测试测试：床前明月光");
    printf("OUT: %s\n", ret);

    ret = (*func)(" 疑是地上霜，举头望明月，低头思故乡");
    printf("OUT: %s\n", ret);

    return 0;
}
