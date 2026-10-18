/* Baseline JPEG decoder tests against Pillow-made vectors, plus truncation
 * and corruption fuzzing (run under ASan/UBSan). Created by Jonas Lee. */
#include "image/jpeg.h"
#include "test_support.h"
#include "jpeg_vectors.h"

static uint32_t out[64 * 64];
static uint8_t work[8192];

static int absd(int a, int b) { return a > b ? a - b : b - a; }

int main(void)
{
    for (unsigned v = 0; v < ARRAY_SIZE(jpeg_vectors); v++) {
        const JpegVector *t = &jpeg_vectors[v];
        int w = 0, h = 0;
        CHECK(jpeg_info(t->jpg, t->len, &w, &h) && w == t->w && h == t->h, "info reads size");
        bool ok = jpeg_decode(t->jpg, t->len, out, ARRAY_SIZE(out));
        CHECK(ok, "decodes");
        if (!ok) { emit(t->name); continue; }
        int maxd = 0;
        long sum = 0;
        for (int i = 0; i < w * h; i++)
            for (int sh = 0; sh < 24; sh += 8) {
                int d = absd((int)((out[i] >> sh) & 255), (int)((t->ref[i] >> sh) & 255));
                if (d > maxd) maxd = d;
                sum += d;
            }
        long mean100 = sum * 100 / (w * h * 3);
        bool close = t->subsampled ? (mean100 <= 250 && maxd <= 32) : (mean100 <= 150 && maxd <= 8);
        CHECK(close, "pixels match Pillow");
        if (!close) {
            char num[24];
            emit(t->name);
            utoa((uint64_t)mean100, num, 10); emit(num);
            utoa((uint64_t)maxd, num, 10); emit(num);
        }
    }
    CHECK(!jpeg_decode(jpeg_vectors[0].jpg, jpeg_vectors[0].len, out, 10), "refuses a too-small output buffer");

    /* Progressive (SOF2) is rejected cleanly. */
    const JpegVector *b = &jpeg_vectors[0];
    memcpy(work, b->jpg, b->len);
    for (unsigned i = 0; i + 1 < b->len; i++) if (work[i] == 0xFF && work[i + 1] == 0xC0) { work[i + 1] = 0xC2; break; }
    CHECK(!jpeg_decode(work, b->len, out, ARRAY_SIZE(out)), "progressive JPEG rejected");

    /* Every truncation and thousands of random corruptions: no crash. */
    for (unsigned n = 0; n < b->len; n++) jpeg_decode(b->jpg, n, out, ARRAY_SIZE(out));
    uint32_t seed = 12345;
    for (int iter = 0; iter < 4000; iter++) {
        const JpegVector *t = &jpeg_vectors[iter % ARRAY_SIZE(jpeg_vectors)];
        memcpy(work, t->jpg, t->len);
        for (int k = 0; k < 1 + iter % 6; k++) {
            seed = seed * 1103515245u + 12345u;
            work[(seed >> 8) % t->len] = (uint8_t)(seed >> 16);
        }
        jpeg_decode(work, t->len, out, ARRAY_SIZE(out));
    }
    CHECK(true, "survived truncation + corruption fuzzing");
    return finish();
}
