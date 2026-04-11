#include "../src/kernel/path.h"

extern int puts(const char* s);

typedef struct PathCase {
    const char* cwd;
    const char* input;
    const char* expected;
} PathCase;

static int check_path_case(const PathCase* test_case)
{
    char buffer[PATH_CAPACITY];
    uint32_t i = 0;

    if (!path_normalize(test_case->cwd, test_case->input, buffer, sizeof(buffer))) {
        puts("path_test: normalization failed");
        return 1;
    }

    for (;;) {
        if (buffer[i] != test_case->expected[i]) {
            puts("path_test: unexpected normalized path");
            return 1;
        }
        if (buffer[i] == '\0')
            break;
        ++i;
    }

    return 0;
}

int main(void)
{
    static const PathCase path_cases[] = {
        {"/", "/", "/"},
        {"/MYDIR", "..", "/"},
        {"/MYDIR", "./TEST.TXT", "/MYDIR/TEST.TXT"},
        {"/MYDIR", "../BIGDIR/./ITEM00.TXT", "/BIGDIR/ITEM00.TXT"},
        {"/", "////MYDIR//TEST.TXT", "/MYDIR/TEST.TXT"},
        {"/", "longfilenamecomponent.txt", "/LONGFILENAMECOMPONENT.TXT"},
    };
    char too_small[4];
    uint32_t i;

    for (i = 0; i < sizeof(path_cases) / sizeof(path_cases[0]); ++i) {
        if (check_path_case(&path_cases[i]) != 0)
            return 1;
    }

    if (path_normalize("/", "MYDIR/TEST.TXT", too_small, sizeof(too_small))) {
        puts("path_test: expected small buffer failure");
        return 1;
    }

    return 0;
}
