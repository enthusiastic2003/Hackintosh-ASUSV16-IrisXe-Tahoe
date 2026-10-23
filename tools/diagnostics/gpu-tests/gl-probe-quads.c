// Large quads made of two triangles, the way WebRender draws everything (instanced TRIANGLE_STRIP / indexed triangles), with
// depth testing, blending, flat varyings and clipping. Checks every pixel, so a missing or double-drawn triangle edge (a diagonal
// seam) shows up. Prints how many pixels are wrong and where the first ones are.
//   clang -w gl-probe-quads.c -framework OpenGL -o gl-probe-quads && ./gl-probe-quads
#define GL_SILENCE_DEPRECATION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#define W 1920
#define H 1081
static GLuint cs(GLenum t, const char *s) { GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, 0); glCompileShader(o);
    GLint ok; glGetShaderiv(o, GL_COMPILE_STATUS, &ok); if (!ok) { char l[512]; glGetShaderInfoLog(o, 512, 0, l); printf("shader error: %s\n", l); exit(3); } return o; }
static GLuint prog(const char *vs, const char *fs) { GLuint p = glCreateProgram(); glAttachShader(p, cs(GL_VERTEX_SHADER, vs)); glAttachShader(p, cs(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "corner"); glBindAttribLocation(p, 1, "rect"); glBindAttribLocation(p, 2, "col"); glBindAttribLocation(p, 3, "z"); glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok); if (!ok) { printf("link failed\n"); exit(4); } return p; }
static unsigned char *px; static GLuint fbo, rt, depth, vao, vbCorner, vbInst, ibo;
static int total_fail;

typedef struct { float rect[4]; float col[4]; float z; } Inst;     // pixel rect x0,y0,x1,y1 ; colour ; depth
static const char *VS =
    "#version 150\nin vec2 corner; in vec4 rect; in vec4 col; in float z; out vec4 vcol; flat out vec4 fcol; flat out ivec4 icol; out vec2 vpos;\n"
    "void main(){ vec2 px = mix(rect.xy, rect.zw, corner); gl_Position = vec4(px / vec2(1920.0, 1081.0) * 2.0 - 1.0, z, 1.0); vcol = col; fcol = col; icol = ivec4(col * 255.0 + 0.5); vpos = px; }\n";
static const char *FS_SMOOTH = "#version 150\nin vec4 vcol; flat in vec4 fcol; flat in ivec4 icol; in vec2 vpos; out vec4 o; void main(){ o = vcol; }\n";
static const char *FS_FLAT   = "#version 150\nin vec4 vcol; flat in vec4 fcol; flat in ivec4 icol; in vec2 vpos; out vec4 o; void main(){ o = fcol; }\n";
static const char *FS_INT    = "#version 150\nin vec4 vcol; flat in vec4 fcol; flat in ivec4 icol; in vec2 vpos; out vec4 o; void main(){ o = vec4(icol) / 255.0; }\n";

static void upload(const Inst *in, int n) { glBindBuffer(GL_ARRAY_BUFFER, vbInst); glBufferData(GL_ARRAY_BUFFER, sizeof(Inst) * n, in, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, 0, sizeof(Inst), (void *)0); glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, 0, sizeof(Inst), (void *)16); glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 1, GL_FLOAT, 0, sizeof(Inst), (void *)32); glVertexAttribDivisor(3, 1); }
static void clear(float r, float g, float b, float a) { glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE); glClearColor(r, g, b, a); glClearDepth(1.0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }
static void readback(void) { glFinish(); glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); }
static int inside(const Inst *q, int x, int y) { return x + 0.5f > q->rect[0] && x + 0.5f < q->rect[2] && y + 0.5f > q->rect[1] && y + 0.5f < q->rect[3]; }
// expected(x,y,out[4]) decides the colour; tolerance 3/255
typedef void (*expect_fn)(const Inst *in, int n, int x, int y, unsigned char *out);
static void report(const char *name, expect_fn ex, const Inst *in, int n) {
    int bad = 0, fx = -1, fy = -1; unsigned char want[4] = {0}, got[4] = {0}; int onDiag = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { unsigned char e[4]; ex(in, n, x, y, e); const unsigned char *g = px + ((size_t)y * W + x) * 4;
        if (abs(g[0] - e[0]) > 3 || abs(g[1] - e[1]) > 3 || abs(g[2] - e[2]) > 3) { if (!bad) { fx = x; fy = y; memcpy(want, e, 4); memcpy(got, g, 4); } bad++; } }
    (void)onDiag; printf("%-62s %s (%d wrong of %d)", name, bad ? "FAIL" : "ok  ", bad, W * H);
    if (bad) { printf("  first at (%d,%d) got %d,%d,%d want %d,%d,%d", fx, fy, got[0], got[1], got[2], want[0], want[1], want[2]); total_fail++; } printf("\n"); }
static void ex_single(const Inst *in, int n, int x, int y, unsigned char *o) { o[3] = 255; if (inside(&in[0], x, y)) { for (int c = 0; c < 3; c++) o[c] = (unsigned char)(in[0].col[c] * 255.0f + 0.5f); } else { o[0] = 10; o[1] = 20; o[2] = 30; } }
static void ex_grid(const Inst *in, int n, int x, int y, unsigned char *o) { o[3] = 255; o[0] = 10; o[1] = 20; o[2] = 30; for (int i = 0; i < n; i++) if (inside(&in[i], x, y)) { for (int c = 0; c < 3; c++) o[c] = (unsigned char)(in[i].col[c] * 255.0f + 0.5f); } }
static void ex_depth(const Inst *in, int n, int x, int y, unsigned char *o) {   // nearest (smallest z) covering quad wins
    o[3] = 255; o[0] = 10; o[1] = 20; o[2] = 30; float best = 2; for (int i = 0; i < n; i++) if (inside(&in[i], x, y) && in[i].z < best) { best = in[i].z; for (int c = 0; c < 3; c++) o[c] = (unsigned char)(in[i].col[c] * 255.0f + 0.5f); } }
static void ex_blend(const Inst *in, int n, int x, int y, unsigned char *o) {   // background 10,20,30 under a 50% white quad
    o[3] = 255; if (inside(&in[0], x, y)) { o[0] = (10 + 255) / 2; o[1] = (20 + 255) / 2; o[2] = (30 + 255) / 2; } else { o[0] = 10; o[1] = 20; o[2] = 30; } }

static void draw_strip(int n) { glBindBuffer(GL_ARRAY_BUFFER, vbCorner); glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, n); }
static void draw_indexed(int n) { glBindBuffer(GL_ARRAY_BUFFER, vbCorner); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo); glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0, n); }

int main(void) {
    CGLPixelFormatAttribute at[] = { kCGLPFAAccelerated, kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, kCGLPFADepthSize, 24, 0 };
    CGLPixelFormatObj pf; GLint np; CGLContextObj ctx; if (CGLChoosePixelFormat(at, &pf, &np) || CGLCreateContext(pf, NULL, &ctx)) { printf("no context\n"); return 2; }
    CGLSetCurrentContext(ctx); printf("%s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    px = malloc((size_t)W * H * 4);
    glGenVertexArrays(1, &vao); glBindVertexArray(vao); glGenFramebuffers(1, &fbo); glGenTextures(1, &rt); glGenTextures(1, &depth); glGenBuffers(1, &vbCorner); glGenBuffers(1, &vbInst); glGenBuffers(1, &ibo);
    glBindTexture(GL_TEXTURE_2D, rt); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glBindTexture(GL_TEXTURE_2D, depth); glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, W, H, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    float corners[8] = { 0, 0, 1, 0, 0, 1, 1, 1 }; glBindBuffer(GL_ARRAY_BUFFER, vbCorner); glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW); glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0);
    unsigned short idx[6] = { 0, 1, 2, 2, 1, 3 }; glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
    GLuint pS = prog(VS, FS_SMOOTH), pF = prog(VS, FS_FLAT), pI = prog(VS, FS_INT);
    glViewport(0, 0, W, H);
    Inst big = { { 100.3f, 80.7f, 1820.2f, 990.6f }, { 0.9f, 0.4f, 0.2f, 1 }, 0.5f };

    printf("--- one large quad (1720x910), every pixel checked\n");
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); upload(&big, 1);
    clear(10 / 255.f, 20 / 255.f, 30 / 255.f, 1); glUseProgram(pS); draw_strip(1); readback(); report("TRIANGLE_STRIP, smooth varying", ex_single, &big, 1);
    clear(10 / 255.f, 20 / 255.f, 30 / 255.f, 1); glUseProgram(pS); draw_indexed(1); readback(); report("indexed TRIANGLES, smooth varying", ex_single, &big, 1);
    clear(10 / 255.f, 20 / 255.f, 30 / 255.f, 1); glUseProgram(pF); draw_strip(1); readback(); report("TRIANGLE_STRIP, flat vec4 varying", ex_single, &big, 1);
    clear(10 / 255.f, 20 / 255.f, 30 / 255.f, 1); glUseProgram(pI); draw_strip(1); readback(); report("TRIANGLE_STRIP, flat ivec4 varying", ex_single, &big, 1);
    clear(10 / 255.f, 20 / 255.f, 30 / 255.f, 1); glUseProgram(pI); draw_indexed(1); readback(); report("indexed TRIANGLES, flat ivec4 varying", ex_single, &big, 1);

    printf("--- with depth test and depth write (opaque batch, as WebRender)\n");
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
    clear(10 / 255.f, 20 / 255.f, 30 / 255.f, 1); glUseProgram(pS); draw_strip(1); readback(); report("TRIANGLE_STRIP + depth test", ex_single, &big, 1);
    Inst two[2] = { { { 100, 80, 1500, 800 }, { 1, 0, 0, 1 }, 0.6f }, { { 500, 300, 1800, 1000 }, { 0, 1, 0, 1 }, 0.3f } };
    upload(two, 2); clear(10 / 255.f, 20 / 255.f, 30 / 255.f, 1); glUseProgram(pS); draw_strip(2); readback(); report("two overlapping large quads, nearer one wins (depth)", ex_depth, two, 2);
    Inst two2[2] = { { { 500, 300, 1800, 1000 }, { 0, 1, 0, 1 }, 0.3f }, { { 100, 80, 1500, 800 }, { 1, 0, 0, 1 }, 0.6f } };
    upload(two2, 2); clear(10 / 255.f, 20 / 255.f, 30 / 255.f, 1); glUseProgram(pS); draw_strip(2); readback(); report("same, nearer quad drawn first (must stay on top)", ex_depth, two2, 2);

    printf("--- translucent quad: 50%% white over the background (a doubly drawn seam would show as a brighter diagonal)\n");
    glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); Inst tr = { { 100.3f, 80.7f, 1820.2f, 990.6f }, { 1, 1, 1, 0.5f }, 0.5f }; upload(&tr, 1);
    clear(10 / 255.f, 20 / 255.f, 30 / 255.f, 1); glUseProgram(pS); draw_strip(1); readback(); report("TRIANGLE_STRIP, 50% blend", ex_blend, &tr, 1);
    clear(10 / 255.f, 20 / 255.f, 30 / 255.f, 1); glUseProgram(pS); draw_indexed(1); readback(); report("indexed TRIANGLES, 50% blend", ex_blend, &tr, 1);

    printf("--- clipping: quad extends far beyond the viewport on every side\n");
    glDisable(GL_BLEND); Inst huge = { { -3000, -2000, 5000, 4000 }, { 0.2f, 0.7f, 0.9f, 1 }, 0.5f }; upload(&huge, 1);
    clear(10 / 255.f, 20 / 255.f, 30 / 255.f, 1); glUseProgram(pS); draw_strip(1); readback(); report("quad much larger than the viewport", ex_single, &huge, 1);
    Inst edge = { { -500, 300, 700, 900 }, { 0.8f, 0.8f, 0.1f, 1 }, 0.5f }; upload(&edge, 1);
    clear(10 / 255.f, 20 / 255.f, 30 / 255.f, 1); glUseProgram(pS); draw_strip(1); readback(); report("quad partly off the left edge", ex_single, &edge, 1);
    glEnable(GL_SCISSOR_TEST); glScissor(333, 211, 1001, 607); clear(10 / 255.f, 20 / 255.f, 30 / 255.f, 1); glEnable(GL_SCISSOR_TEST); glScissor(333, 211, 1001, 607);
    upload(&big, 1); glUseProgram(pS); draw_strip(1); readback(); glDisable(GL_SCISSOR_TEST);
    { int bad = 0; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { int in_s = x >= 333 && x < 333 + 1001 && y >= 211 && y < 211 + 607; unsigned char e[4]; ex_single(&big, 1, x, y, e);
          if (!in_s) { e[0] = 10; e[1] = 20; e[2] = 30; } const unsigned char *g = px + ((size_t)y * W + x) * 4; if (abs(g[0] - e[0]) > 3 || abs(g[1] - e[1]) > 3 || abs(g[2] - e[2]) > 3) bad++; }
      printf("%-62s %s (%d wrong of %d)\n", "scissored large quad (clear also scissored)", bad ? "FAIL" : "ok  ", bad, W * H); if (bad) total_fail++; }

    printf("--- many small quads (a 60x33 grid of 30x30 px quads, one instanced draw)\n");
    { static Inst grid[60 * 33]; int n = 0; for (int gy = 0; gy < 33; gy++) for (int gx = 0; gx < 60; gx++) { Inst *q = &grid[n++]; q->rect[0] = gx * 32 + 1.f; q->rect[1] = gy * 32 + 1.f; q->rect[2] = gx * 32 + 31.f; q->rect[3] = gy * 32 + 31.f;
          q->col[0] = (gx * 4) / 255.f; q->col[1] = (gy * 7) / 255.f; q->col[2] = ((gx + gy) & 1) ? 0.9f : 0.1f; q->col[3] = 1; q->z = 0.5f; }
      upload(grid, n); glEnable(GL_DEPTH_TEST); clear(10 / 255.f, 20 / 255.f, 30 / 255.f, 1); glUseProgram(pI); draw_strip(n); readback(); report("grid of 1980 instanced quads, flat ivec4", ex_grid, grid, n); }
    printf("--- %d failing group(s)\n", total_fail); return total_fail != 0; }
