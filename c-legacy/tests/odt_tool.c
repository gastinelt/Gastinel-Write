/* Outil de test natif (avec CRT) : odt_tool cat f.odt | odt_tool make in.txt out.odt */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/odt.h"

void *gw_alloc(u32 n) { return malloc(n); }
void *gw_realloc(void *p, u32 n) { return realloc(p, n); }
void gw_free(void *p) { free(p); }

static int slurp(const char *path, Buf *b)
{
    FILE *f = fopen(path, "rb");
    char tmp[4096];
    size_t n;
    if (!f) return 0;
    buf_init(b);
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) buf_append(b, tmp, (u32)n);
    fclose(f);
    return !b->oom;
}

int main(int argc, char **argv)
{
    Buf in, out;
    const char *err;
    if (argc < 3 || !slurp(argv[2], &in)) return 2;
    buf_init(&out);
    if (!strcmp(argv[1], "cat")) {
        int formatted = 0;
        err = odt_read(in.data, in.len, &out, &formatted);
        if (err) { fprintf(stderr, "ERREUR: %s\n", err); return 1; }
        if (formatted) fprintf(stderr, "FORMATTED\n");
        fwrite(out.data, 1, out.len, stdout);
    } else if (!strcmp(argv[1], "make") && argc == 4) {
        FILE *f;
        if (!odt_build(in.data, in.len, &out)) return 1;
        f = fopen(argv[3], "wb");
        fwrite(out.data, 1, out.len, f);
        fclose(f);
    } else return 2;
    buf_free(&in);
    buf_free(&out);
    return 0;
}
