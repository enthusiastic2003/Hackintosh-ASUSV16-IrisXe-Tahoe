// Texture uploads through the paths WebRender uses, checked by sampling the texture in a shader.
// Formats x methods x widths (odd widths give unaligned row pitches). Prints only the failures.
//   clang gl-probe-textures.c -framework OpenGL -o gl-probe-textures && ./gl-probe-textures
#define GL_SILENCE_DEPRECATION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#define MAXW 1100
#define H 6

typedef struct { const char *name; GLenum internal, format, type; int nch, bpt, kind, bgra; } Fmt;
static const Fmt FM[] = {
    { "R8",       GL_R8,       GL_RED,  GL_UNSIGNED_BYTE,  1, 1, 0, 0 },
    { "RG8",      GL_RG8,      GL_RG,   GL_UNSIGNED_BYTE,  2, 2, 0, 0 },
    { "RGBA8",    GL_RGBA8,    GL_RGBA, GL_UNSIGNED_BYTE,  4, 4, 0, 0 },
    { "BGRA8",    GL_RGBA8,    GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, 4, 4, 0, 1 },
    { "R16",      GL_R16,      GL_RED,  GL_UNSIGNED_SHORT, 1, 2, 1, 0 },
    { "RGBA16F",  GL_RGBA16F,  GL_RGBA, GL_HALF_FLOAT,     4, 8, 2, 0 },
    { "RGBA32F",  GL_RGBA32F,  GL_RGBA, GL_FLOAT,          4, 16, 3, 0 },
};
enum { M_IMG, M_SUB, M_PBO, M_PBO_OFF, M_ROWLEN, M_SUBRECT, M_ARRAY, M_NM };
static const char *MN[] = { "TexImage2D(client)", "TexImage2D(NULL)+SubImage", "PBO", "PBO offset 52", "ROW_LENGTH=w+13", "two sub-rects", "2D array layer 1" };
static const int WS[] = { 1, 2, 3, 5, 7, 13, 16, 17, 31, 33, 63, 64, 65, 100, 127, 129, 255, 256, 257, 500, 1000 };

static int bval(int x, int y, int c) { return (x * 7 + y * 13 + c * 31 + 5) & 0xff; }
static uint16_t half8(int b) {            // b/256 in half precision (exact for b in 0..255)
    if (b == 0) return 0;
    int e = -1; int m = b; while (m < 128) { m <<= 1; e--; }       // b/256 = (m/128) * 2^e
    return (uint16_t)(((e + 15) << 10) | ((m & 127) << 3));
}
// texel (x,y) of the logical image -> bytes at p
static void put(const Fmt *f, uint8_t *p, int x, int y) {
    for (int c = 0; c < f->nch; c++) {
        int b = bval(x, y, c);
        int dc = f->bgra ? (c == 0 ? 2 : c == 2 ? 0 : c) : c;
        switch (f->kind) {
        case 0: p[dc] = (uint8_t)b; break;
        case 1: ((uint16_t *)p)[c] = (uint16_t)(b * 257); break;
        case 2: ((uint16_t *)p)[c] = half8(b); break;
        case 3: ((float *)p)[c] = b / 255.0f; break;
        }
    }
}
static float expect(const Fmt *f, int x, int y, int c) {
    if (c >= f->nch) return c == 3 ? 1.0f : 0.0f;
    int b = bval(x, y, c);
    return f->kind == 2 ? b / 256.0f : b / 255.0f;
}
static GLuint compile(GLenum t, const char *s) { GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, NULL); glCompileShader(o);
    GLint ok; glGetShaderiv(o, GL_COMPILE_STATUS, &ok); if (!ok) { char l[512]; glGetShaderInfoLog(o, 512, NULL, l); printf("shader error: %s\n", l); exit(3); } return o; }
static GLuint program(const char *fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, "#version 150\nvoid main(){ vec2 q[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3)); gl_Position = vec4(q[gl_VertexID],0,1); }\n"));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs)); glLinkProgram(p); return p;
}
static uint8_t *stage, *stage2;
static void fill(const Fmt *f, int w, int h, int rowlen, uint8_t *dst) {
    memset(dst, 0xAB, (size_t)rowlen * h * f->bpt + 4096);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) put(f, dst + ((size_t)y * rowlen + x) * f->bpt, x, y);
}

int main(void) {
    CGLPixelFormatAttribute at[] = { kCGLPFAAccelerated, kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, 0 };
    CGLPixelFormatObj pf; GLint np; CGLContextObj ctx;
    if (CGLChoosePixelFormat(at, &pf, &np) || CGLCreateContext(pf, NULL, &ctx)) { printf("no context\n"); return 2; }
    CGLSetCurrentContext(ctx);
    printf("%s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    stage = malloc((size_t)(MAXW + 64) * H * 16 + 8192); stage2 = malloc((size_t)(MAXW + 64) * H * 16 + 8192);
    GLuint prog2 = program("#version 150\nuniform sampler2D t; out vec4 o; void main(){ o = texelFetch(t, ivec2(gl_FragCoord.xy), 0); }\n");
    GLuint prog3 = program("#version 150\nuniform sampler2DArray t; out vec4 o; void main(){ o = texelFetch(t, ivec3(gl_FragCoord.xy, 1), 0); }\n");
    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    GLuint fbo, rt, pbo; glGenFramebuffers(1, &fbo); glGenTextures(1, &rt); glGenBuffers(1, &pbo);
    float *rb = malloc(sizeof(float) * 4 * MAXW * H);
    int failcombos = 0, combos = 0;
    for (unsigned fi = 0; fi < sizeof(FM) / sizeof(FM[0]); fi++) {
        const Fmt *f = &FM[fi];
        for (int m = 0; m < M_NM; m++) {
            int nbad = 0; char firstbad[160] = ""; char list[160] = ""; int shown = 0;
            for (unsigned wi = 0; wi < sizeof(WS) / sizeof(WS[0]); wi++) {
                int w = WS[wi], h = H;
                GLuint tex; glGenTextures(1, &tex);
                GLenum tgt = m == M_ARRAY ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
                glBindTexture(tgt, tex);
                glTexParameteri(tgt, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(tgt, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(tgt, GL_TEXTURE_BASE_LEVEL, 0); glTexParameteri(tgt, GL_TEXTURE_MAX_LEVEL, 0);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0); glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
                int rowlen = w;
                switch (m) {
                case M_IMG:
                    fill(f, w, h, w, stage);
                    glTexImage2D(GL_TEXTURE_2D, 0, f->internal, w, h, 0, f->format, f->type, stage); break;
                case M_SUB:
                    fill(f, w, h, w, stage);
                    glTexImage2D(GL_TEXTURE_2D, 0, f->internal, w, h, 0, f->format, f->type, NULL);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, f->format, f->type, stage); break;
                case M_PBO: case M_PBO_OFF: {
                    size_t off = m == M_PBO_OFF ? 52 : 0, bytes = (size_t)w * h * f->bpt;
                    fill(f, w, h, w, stage);
                    memset(stage2, 0xCD, off + bytes); memcpy(stage2 + off, stage, bytes);
                    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo); glBufferData(GL_PIXEL_UNPACK_BUFFER, off + bytes, stage2, GL_STREAM_DRAW);
                    glTexImage2D(GL_TEXTURE_2D, 0, f->internal, w, h, 0, f->format, f->type, NULL);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, f->format, f->type, (const void *)off);
                    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0); break; }
                case M_ROWLEN:
                    rowlen = w + 13; fill(f, w, h, rowlen, stage);
                    glTexImage2D(GL_TEXTURE_2D, 0, f->internal, w, h, 0, f->format, f->type, NULL);
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, rowlen);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, f->format, f->type, stage);
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); break;
                case M_SUBRECT: {
                    fill(f, w, h, w, stage);
                    glTexImage2D(GL_TEXTURE_2D, 0, f->internal, w, h, 0, f->format, f->type, NULL);
                    int half = w / 2;
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, w);
                    if (half > 0) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, half, h, f->format, f->type, stage);
                    glPixelStorei(GL_UNPACK_SKIP_PIXELS, half);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, half, 0, w - half, h, f->format, f->type, stage);
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0); break; }
                case M_ARRAY:
                    fill(f, w, h, w, stage);
                    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, f->internal, w, h, 3, 0, f->format, f->type, NULL);
                    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 1, w, h, 1, f->format, f->type, stage); break;
                }
                // sample into an RGBA32F target
                glBindTexture(GL_TEXTURE_2D, rt);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
                glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
                glViewport(0, 0, w, h); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
                GLuint pr = m == M_ARRAY ? prog3 : prog2; glUseProgram(pr); glUniform1i(glGetUniformLocation(pr, "t"), 0);
                glActiveTexture(GL_TEXTURE0); glBindTexture(tgt, tex);
                glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
                glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, rb);
                int bad = 0, bx = -1, by = -1, bc = -1; float got = 0, want = 0;
                for (int y = 0; y < h && !bad; y++) for (int x = 0; x < w && !bad; x++) for (int c = 0; c < 4; c++) {
                    float e = expect(f, x, y, c), g = rb[((size_t)y * w + x) * 4 + c];
                    if (g < e - 0.004f || g > e + 0.004f) { bad = 1; bx = x; by = y; bc = c; got = g; want = e; break; }
                }
                if (bad) {
                    nbad++;
                    if (!firstbad[0]) snprintf(firstbad, sizeof firstbad, "w=%d first bad (%d,%d) ch%d got %.4f want %.4f", w, bx, by, bc, got, want);
                    if (shown < 8) { char t[16]; snprintf(t, sizeof t, "%d ", w); strncat(list, t, sizeof list - strlen(list) - 1); shown++; }
                }
                glDeleteTextures(1, &tex);
                (void)rowlen;
            }
            combos++;
            if (nbad) { failcombos++; printf("FAIL %-8s %-26s %2d of %d widths bad: %s| %s\n", f->name, MN[m], nbad, (int)(sizeof(WS) / sizeof(WS[0])), list, firstbad); }
        }
    }
    printf("--- %d of %d format/method combinations have failures\n", failcombos, combos);
    return failcombos != 0;
}
