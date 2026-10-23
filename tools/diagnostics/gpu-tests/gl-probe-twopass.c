// WebRender's frame structure in miniature, in ONE render pass: an opaque pass (blend off, depth test + depth WRITE, front to back)
// followed by an alpha pass (blend on, depth test, depth write OFF, back to front). Translucent quads are placed in front of and
// behind opaque ones. Every pixel is compared with a CPU reference. A failing case shows how many pixels differ and where.
//   clang -w gl-probe-twopass.c -framework OpenGL -o gl-probe-twopass && ./gl-probe-twopass
#define GL_SILENCE_DEPRECATION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#define W 1024
#define H 1024
typedef struct { float x0, y0, x1, y1, z; unsigned char c[4]; } Q;      // c = rgba, a used by the alpha pass
static GLuint cs(GLenum t, const char *s) { GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, 0); glCompileShader(o);
    GLint ok; glGetShaderiv(o, GL_COMPILE_STATUS, &ok); if (!ok) { char l[512]; glGetShaderInfoLog(o, 512, 0, l); printf("shader error: %s\n", l); exit(3); } return o; }
static unsigned rng = 777; static unsigned rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static GLuint progO, progA, fbo, rt, depthTex, vao, vbc, vbi; static int total_fail;
static unsigned char *px; static float *acc[3], *zb;

static void upload_and_draw(const Q *q, int n, GLuint prog) {
    glBindBuffer(GL_ARRAY_BUFFER, vbi); glBufferData(GL_ARRAY_BUFFER, sizeof(Q) * n, q, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, 0, sizeof(Q), (void *)0); glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 1, GL_FLOAT, 0, sizeof(Q), (void *)16); glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Q), (void *)20); glVertexAttribDivisor(3, 1);
    glBindBuffer(GL_ARRAY_BUFFER, vbc); glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0); glUseProgram(prog); glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, n); }
static void ref_opaque(const Q *q, int n) { for (int k = 0; k < n; k++) { const Q *s = &q[k]; int xa = s->x0 < 0 ? 0 : (int)s->x0, xb = s->x1 > W ? W : (int)s->x1, ya = s->y0 < 0 ? 0 : (int)s->y0, yb = s->y1 > H ? H : (int)s->y1;
    for (int y = ya; y < yb; y++) for (int x = xa; x < xb; x++) { int i = y * W + x; if (s->z <= zb[i]) { zb[i] = s->z; for (int c = 0; c < 3; c++) acc[c][i] = s->c[c] / 255.0f; } } } }
static void ref_alpha(const Q *q, int n) { for (int k = 0; k < n; k++) { const Q *s = &q[k]; float a = s->c[3] / 255.0f; int xa = s->x0 < 0 ? 0 : (int)s->x0, xb = s->x1 > W ? W : (int)s->x1, ya = s->y0 < 0 ? 0 : (int)s->y0, yb = s->y1 > H ? H : (int)s->y1;
    for (int y = ya; y < yb; y++) for (int x = xa; x < xb; x++) { int i = y * W + x; if (s->z <= zb[i]) for (int c = 0; c < 3; c++) acc[c][i] = s->c[c] / 255.0f * a + acc[c][i] * (1 - a); } } }
static int check(const char *name) {
    glFinish(); glBindFramebuffer(GL_FRAMEBUFFER, fbo); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    int bad = 0, fx = -1, fy = -1, gone = 0; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { int i = y * W + x; int d = 0, dark = 1;
        for (int c = 0; c < 3; c++) { int e = (int)(acc[c][i] * 255.0f + 0.5f); d += abs(px[i * 4 + c] - e) > 4; if (px[i * 4 + c] > 30) dark = 0; }
        if (d) { if (!bad) { fx = x; fy = y; } bad++; if (dark) gone++; } }
    printf("%-72s %s (%d wrong of %d", name, bad ? "FAIL" : "ok  ", bad, W * H); if (bad) { printf(", %d of them nearly black; first at (%d,%d)", gone, fx, fy); total_fail++; } printf(")\n"); return bad; }
static void clear_all(void) { glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, rt, 0, 1); glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthTex, 0, 1);
    glViewport(0, 0, W, H); glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE); glClearColor(0, 0, 0, 1); glClearDepth(1.0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    for (int i = 0; i < W * H; i++) { zb[i] = 1.0f; acc[0][i] = acc[1][i] = acc[2][i] = 0; } }
static int cmpz_asc(const void *a, const void *b) { float x = ((const Q *)a)->z, y = ((const Q *)b)->z; return x < y ? -1 : x > y; }
static int cmpz_desc(const void *a, const void *b) { return -cmpz_asc(a, b); }
static void rect(Q *q, int x, int y, int w, int h, float z, int r, int g, int b, int a) { q->x0 = x; q->y0 = y; q->x1 = x + w; q->y1 = y + h; q->z = z; q->c[0] = r; q->c[1] = g; q->c[2] = b; q->c[3] = a; }

static void scenario(const char *title, int nOpaque, int nAlpha, int bigAlpha, int sizeMax) {
    static Q op[4000], al[8000]; int no = 0, na = 0;
    rect(&op[no++], 0, 0, W, H, 0.9f, 40, 30, 70, 255);                                              // page background
    for (int i = 1; i < nOpaque; i++) rect(&op[no++], rnd() % (W - 40), rnd() % (H - 40), 20 + rnd() % sizeMax, 20 + rnd() % sizeMax, 0.2f + (rnd() % 60) / 100.0f, rnd() % 256, rnd() % 256, rnd() % 256, 255);
    for (int i = 0; i < nAlpha; i++) rect(&al[na++], rnd() % (W - 30), rnd() % (H - 30), 6 + rnd() % (i < bigAlpha ? 700 : 60), 6 + rnd() % (i < bigAlpha ? 500 : 20), 0.1f + (rnd() % 80) / 100.0f, rnd() % 256, rnd() % 256, rnd() % 256, 20 + rnd() % 230);
    qsort(op, no, sizeof(Q), cmpz_asc); qsort(al, na, sizeof(Q), cmpz_desc);                         // opaque front to back, alpha back to front
    clear_all(); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND); glDepthMask(GL_TRUE); upload_and_draw(op, no, progO);                        // opaque pass
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); upload_and_draw(al, na, progA);   // alpha pass
    glDisable(GL_BLEND); glDepthMask(GL_TRUE); glDisable(GL_DEPTH_TEST);
    ref_opaque(op, no); ref_alpha(al, na); check(title); }

int main(void) {
    CGLPixelFormatAttribute at[] = { kCGLPFAAccelerated, kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, kCGLPFADepthSize, 24, 0 };
    CGLPixelFormatObj pf; GLint np; CGLContextObj ctx; if (CGLChoosePixelFormat(at, &pf, &np) || CGLCreateContext(pf, NULL, &ctx)) { printf("no context\n"); return 2; }
    CGLSetCurrentContext(ctx); printf("%s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    px = malloc(W * H * 4); zb = malloc(W * H * sizeof(float)); for (int c = 0; c < 3; c++) acc[c] = malloc(W * H * sizeof(float));
    glGenVertexArrays(1, &vao); glBindVertexArray(vao); glGenFramebuffers(1, &fbo); glGenTextures(1, &rt); glGenTextures(1, &depthTex); glGenBuffers(1, &vbc); glGenBuffers(1, &vbi);
    glBindTexture(GL_TEXTURE_2D_ARRAY, rt); glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, W, H, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D_ARRAY, depthTex); glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, W, H, 3, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    float corners[8] = { 0, 0, 1, 0, 0, 1, 1, 1 }; glBindBuffer(GL_ARRAY_BUFFER, vbc); glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW); glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0);
    const char *vs = "#version 150\nin vec2 corner; in vec4 rect; in float z; in vec4 col; flat out vec4 fc; void main(){ vec2 p = mix(rect.xy, rect.zw, corner); gl_Position = vec4(p / vec2(1024.0, 1024.0) * 2.0 - 1.0, z * 2.0 - 1.0, 1.0); fc = col; }\n";
    const char *fsO = "#version 150\nflat in vec4 fc; out vec4 o; void main(){ o = vec4(fc.rgb, 1.0); }\n";                 // opaque variant: no alpha handling
    const char *fsA = "#version 150\nflat in vec4 fc; out vec4 o; void main(){ o = fc; }\n";                                  // alpha variant: straight alpha
    progO = glCreateProgram(); glAttachShader(progO, cs(GL_VERTEX_SHADER, vs)); glAttachShader(progO, cs(GL_FRAGMENT_SHADER, fsO)); progA = glCreateProgram(); glAttachShader(progA, cs(GL_VERTEX_SHADER, vs)); glAttachShader(progA, cs(GL_FRAGMENT_SHADER, fsA));
    for (GLuint p = progO, k = 0; k < 2; k++, p = progA) { glBindAttribLocation(p, 0, "corner"); glBindAttribLocation(p, 1, "rect"); glBindAttribLocation(p, 2, "z"); glBindAttribLocation(p, 3, "col"); glLinkProgram(p); }
    printf("--- opaque pass (depth write) then alpha pass (depth test only, blend), one render pass\n");
    scenario("background only, then 400 small translucent quads", 1, 400, 0, 100);
    scenario("background only, then large translucent quads (up to 700x500)", 1, 40, 40, 100);
    scenario("40 opaque boxes + background, then 600 small translucent quads", 40, 600, 0, 200);
    scenario("40 opaque boxes + background, then 80 large translucent quads", 40, 80, 80, 200);
    scenario("300 opaque boxes + background, then 2000 mixed translucent quads", 300, 2000, 40, 150);
    printf("--- %d failing group(s)\n", total_fail); return total_fail != 0; }
