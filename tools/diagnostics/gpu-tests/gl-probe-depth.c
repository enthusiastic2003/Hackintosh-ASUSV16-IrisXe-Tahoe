// WebRender's opaque pass in miniature: hundreds of opaque quads, drawn front to back with depth test + depth WRITE, many sharing
// the same depth, into a texture-array layer with a depth-array attachment. Compared per pixel with a CPU reference.
// Variants: depth function, depth format, depth write on/off (alpha-pass style), the scene drawn in several passes with a depth
// clear in between, and the depth buffer cleared with a scissor.
//   clang -w gl-probe-depth.c -framework OpenGL -o gl-probe-depth && ./gl-probe-depth
#define GL_SILENCE_DEPRECATION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#define W 1024
#define H 1024
#define N 600
static GLuint cs(GLenum t, const char *s) { GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, 0); glCompileShader(o);
    GLint ok; glGetShaderiv(o, GL_COMPILE_STATUS, &ok); if (!ok) { char l[512]; glGetShaderInfoLog(o, 512, 0, l); printf("shader error: %s\n", l); exit(3); } return o; }
typedef struct { float x0, y0, x1, y1, z; unsigned char c[4]; } Q;
static Q q[N]; static int total_fail;
static unsigned rng = 12345; static unsigned rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static GLuint prog2, prog, fbo, rt, depthTex, vao, vbc, vbi;
static unsigned char *px, *want; static float *zbuf;

static void scene(int tiesOnly) {
    for (int i = 0; i < N; i++) {
        float w = 8 + rnd() % 900, h = 8 + rnd() % 700; q[i].x0 = (float)((int)(rnd() % (W - 4)) - 100); q[i].y0 = (float)((int)(rnd() % (H - 4)) - 100);   // integer edges: no pixel centre lies on an edge
        q[i].x1 = q[i].x0 + w; q[i].y1 = q[i].y0 + h;
        int zi = tiesOnly ? rnd() % 6 : rnd() % 40; q[i].z = 0.05f + zi * 0.02f;
        if (tiesOnly == 2) { static const float ex[] = { 0.0f, 1.0f, 0.5f, 1e-6f, 1.0f - 1e-6f, 1e-4f, 1.0f - 1e-4f }; q[i].z = (rnd() % 3 == 0) ? ex[rnd() % 7] : (rnd() % 1000) / 1000.0f; }
        if (tiesOnly == 3) q[i].z = i * 1e-6f;                       // unique depths 1e-6 apart starting at the near plane
        if (tiesOnly == 4) q[i].z = 1.0f - (N - 1 - i) * 1e-6f;      // unique depths 1e-6 apart ending at the far plane
        if (tiesOnly == 5) q[i].z = 0.5f + i * 2e-7f;                // unique depths 2e-7 apart mid range             // few distinct depths: many ties between overlapping quads
        q[i].c[0] = rnd() % 256; q[i].c[1] = rnd() % 256; q[i].c[2] = rnd() % 256; q[i].c[3] = 255; }
}
static int cmpz(const void *a, const void *b) { const Q *x = a, *y = b; return x->z < y->z ? -1 : x->z > y->z ? 1 : 0; }
static void upload(void) {
    glBindBuffer(GL_ARRAY_BUFFER, vbi); glBufferData(GL_ARRAY_BUFFER, sizeof(Q) * N, q, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, 0, sizeof(Q), (void *)0); glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 1, GL_FLOAT, 0, sizeof(Q), (void *)16); glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Q), (void *)20); glVertexAttribDivisor(3, 1); }
// reference: for every pixel, the winning quad given the depth function and write flag
static void reference(int func, int write, int order_back_to_front) {
    for (int i = 0; i < W * H; i++) { zbuf[i] = 1.0f; want[i * 4] = 10; want[i * 4 + 1] = 20; want[i * 4 + 2] = 30; want[i * 4 + 3] = 255; }
    for (int k = 0; k < N; k++) { const Q *s = &q[k];
        int xa = (int)(s->x0 + 0.5f) > 0 ? (int)(s->x0 + 0.5f) : 0, xb = (int)(s->x1 + 0.5f) < W ? (int)(s->x1 + 0.5f) : W, ya = (int)(s->y0 + 0.5f) > 0 ? (int)(s->y0 + 0.5f) : 0, yb = (int)(s->y1 + 0.5f) < H ? (int)(s->y1 + 0.5f) : H;
        for (int y = ya; y < yb; y++) for (int x = xa; x < xb; x++) { int i = y * W + x; int pass = func == 0 ? s->z < zbuf[i] : s->z <= zbuf[i]; if (!pass) continue;
            memcpy(want + i * 4, s->c, 4); if (write) zbuf[i] = s->z; } }
    (void)order_back_to_front; }
static int compare(const char *name, int tolerate_edges) {
    glFinish(); glBindFramebuffer(GL_FRAMEBUFFER, fbo); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    int bad = 0, edge = 0, fx = -1, fy = -1; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { int i = y * W + x; if (abs(px[i * 4] - want[i * 4]) > 2 || abs(px[i * 4 + 1] - want[i * 4 + 1]) > 2 || abs(px[i * 4 + 2] - want[i * 4 + 2]) > 2) { if (!bad) { fx = x; fy = y; } bad++; } }
    (void)edge; (void)tolerate_edges; printf("%-70s %s (%d wrong of %d)", name, bad ? "FAIL" : "ok  ", bad, W * H); if (bad) { printf("  first at (%d,%d)", fx, fy); total_fail++; } printf("\n"); return bad; }
static int g_mode;   // 0 one instanced draw; 1 one draw per quad; 2 one draw per quad alternating two programs; 3 chunks of 8 alternating programs
static void draw_all(void) {
    if (g_mode == 0) { upload(); glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, N); return; }
    glBindBuffer(GL_ARRAY_BUFFER, vbi); glBufferData(GL_ARRAY_BUFFER, sizeof(Q) * N, q, GL_DYNAMIC_DRAW); int chunk = g_mode == 3 ? 8 : 1, k = 0;
    for (int i = 0; i < N; i += chunk, k++) { int n = i + chunk <= N ? chunk : N - i; GLuint pr = (g_mode >= 2 && (k & 1)) ? prog2 : prog; glUseProgram(pr);
        glBindBuffer(GL_ARRAY_BUFFER, vbi); size_t o = (size_t)i * sizeof(Q); glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, 0, sizeof(Q), (void *)(o + 0)); glVertexAttribDivisor(1, 1);
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 1, GL_FLOAT, 0, sizeof(Q), (void *)(o + 16)); glVertexAttribDivisor(2, 1); glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Q), (void *)(o + 20)); glVertexAttribDivisor(3, 1);
        glBindBuffer(GL_ARRAY_BUFFER, vbc); glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0); glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, n); } }
static void run(const char *name, GLenum func, int write, GLenum depthFmt, int passes, int scissorClear) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, depthTex); glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, depthFmt, W, H, 3, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, rt, 0, 1); glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthTex, 0, 1);
    glViewport(0, 0, W, H); glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE); glClearColor(10 / 255.f, 20 / 255.f, 30 / 255.f, 1); glClearDepth(1.0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (scissorClear) { glEnable(GL_SCISSOR_TEST); glScissor(0, 0, W, H); glClear(GL_DEPTH_BUFFER_BIT); glDisable(GL_SCISSOR_TEST); }
    glEnable(GL_DEPTH_TEST); glDepthFunc(func); glDepthMask(write ? GL_TRUE : GL_FALSE); glUseProgram(prog); glBindBuffer(GL_ARRAY_BUFFER, vbc);
    draw_all();
    for (int p = 1; p < passes; p++) draw_all();                       // same scene again: must change nothing with LEQUAL / LESS+write
    reference(func == GL_LESS ? 0 : 1, write, 0); compare(name, 0); glDisable(GL_DEPTH_TEST); }

int main(void) {
    CGLPixelFormatAttribute at[] = { kCGLPFAAccelerated, kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, kCGLPFADepthSize, 24, 0 };
    CGLPixelFormatObj pf; GLint np; CGLContextObj ctx; if (CGLChoosePixelFormat(at, &pf, &np) || CGLCreateContext(pf, NULL, &ctx)) { printf("no context\n"); return 2; }
    CGLSetCurrentContext(ctx); printf("%s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    px = malloc(W * H * 4); want = malloc(W * H * 4); zbuf = malloc(W * H * sizeof(float));
    glGenVertexArrays(1, &vao); glBindVertexArray(vao); glGenFramebuffers(1, &fbo); glGenTextures(1, &rt); glGenTextures(1, &depthTex); glGenBuffers(1, &vbc); glGenBuffers(1, &vbi);
    glBindTexture(GL_TEXTURE_2D_ARRAY, rt); glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, W, H, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    float corners[8] = { 0, 0, 1, 0, 0, 1, 1, 1 }; glBindBuffer(GL_ARRAY_BUFFER, vbc); glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW); glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0);
    const char *vs = "#version 150\nin vec2 corner; in vec4 rect; in float z; in vec4 col; flat out vec4 fc; void main(){ vec2 p = mix(rect.xy, rect.zw, corner); gl_Position = vec4(p / vec2(1024.0, 1024.0) * 2.0 - 1.0, z * 2.0 - 1.0, 1.0); fc = col; }\n";
    const char *fs = "#version 150\nflat in vec4 fc; out vec4 o; void main(){ o = fc; }\n";
    const char *fs2 = "#version 150\nflat in vec4 fc; out vec4 o; void main(){ o = fc + vec4(0.0); }\n";
    prog2 = glCreateProgram(); glAttachShader(prog2, cs(GL_VERTEX_SHADER, vs)); glAttachShader(prog2, cs(GL_FRAGMENT_SHADER, fs2)); glBindAttribLocation(prog2, 0, "corner"); glBindAttribLocation(prog2, 1, "rect"); glBindAttribLocation(prog2, 2, "z"); glBindAttribLocation(prog2, 3, "col"); glLinkProgram(prog2);
    prog = glCreateProgram(); glAttachShader(prog, cs(GL_VERTEX_SHADER, vs)); glAttachShader(prog, cs(GL_FRAGMENT_SHADER, fs)); glBindAttribLocation(prog, 0, "corner"); glBindAttribLocation(prog, 1, "rect"); glBindAttribLocation(prog, 2, "z"); glBindAttribLocation(prog, 3, "col"); glLinkProgram(prog);
    const char *kinds[6] = { "40 distinct mid-range depths", "only 6 distinct depths (heavy ties)", "depths including exactly 0 and 1 (the clip planes), 1e-6 and 1-1e-6, and random", "unique depths 1e-6 apart starting AT the near plane (z=0)", "unique depths 1e-6 apart ending AT the far plane (z=1)", "unique depths only 2e-7 apart, mid range" };
    for (int scene_kind = 0; scene_kind < 6; scene_kind++) {
        scene(scene_kind); qsort(q, N, sizeof(Q), cmpz);               // front to back, like WebRender's opaque pass
        printf("--- %d opaque quads, %s\n", N, kinds[scene_kind]);
        g_mode = 0; run("LEQUAL, depth write on, D24", GL_LEQUAL, 1, GL_DEPTH_COMPONENT24, 1, 0); run("LEQUAL, depth write on, D32F", GL_LEQUAL, 1, GL_DEPTH_COMPONENT32F, 1, 0);
        run("LESS,   depth write on, D24", GL_LESS, 1, GL_DEPTH_COMPONENT24, 1, 0); }
    printf("--- %d failing group(s)\n", total_fail); return total_fail != 0; }
