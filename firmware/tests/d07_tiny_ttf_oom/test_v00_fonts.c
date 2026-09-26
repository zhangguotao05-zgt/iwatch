#include "test_v00_fonts.h"
#include "iw_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct {
    const char *name;
    uint32_t bytes;
} specs[4] = {
    {"NotoSansSC-v00-only-300.ttf", 1540},
    {"NotoSansSC-v00-only-400.ttf", 13096},
    {"NotoSansSC-v00-only-500.ttf", 10312},
    {"NotoSansSC-v00-only-600.ttf", 9688}
};
static iw_font_blob_t blobs[4];

bool test_v00_fonts_load(void)
{
    if (blobs[0].data) return iw_font_v00_init(blobs);
    const char *root = getenv("IW_V00_FONT_ROOT");
    if (!root || !root[0]) root = "docs/ui/assets/v00/font-specimens/subsets";
    for (unsigned i = 0; i < 4u; ++i) {
        char path[1024];
        int length = snprintf(path, sizeof(path), "%s/%s", root, specs[i].name);
        if (length < 0 || (size_t)length >= sizeof(path)) goto failed;
        FILE *file = NULL;
#ifdef _MSC_VER
        if (fopen_s(&file, path, "rb") != 0) goto failed;
#else
        file = fopen(path, "rb");
        if (!file) goto failed;
#endif
        void *data = malloc(specs[i].bytes);
        if (!data) { fclose(file); goto failed; }
        bool valid = fread(data, 1u, specs[i].bytes, file) == specs[i].bytes &&
                     fgetc(file) == EOF;
        fclose(file);
        if (!valid) { free(data); goto failed; }
        blobs[i] = (iw_font_blob_t){data, specs[i].bytes};
    }
    if (iw_font_v00_init(blobs)) return true;
failed:
    for (unsigned i = 0; i < 4u; ++i) {
        free((void *)blobs[i].data);
        blobs[i] = (iw_font_blob_t){0};
    }
    return false;
}

void test_v00_fonts_release(void)
{
    if (!blobs[0].data) return;
    if (!iw_font_v00_deinit()) abort();
    for (unsigned i = 0; i < 4u; ++i) {
        free((void *)blobs[i].data);
        blobs[i] = (iw_font_blob_t){0};
    }
}
