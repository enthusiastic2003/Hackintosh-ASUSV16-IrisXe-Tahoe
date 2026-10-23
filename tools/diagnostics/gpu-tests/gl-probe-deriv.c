// Screen-space derivatives (dFdx/dFdy/fwidth) on large two-triangle quads, as WebRender's antialiasing does it:
// fwidth(vLocalPos) inside a branch on a flat integer flag. vLocalPos is the pixel position, so fwidth must be exactly (1,1) in every
// pixel, including pixels on the diagonal seam between the two triangles. Every pixel is checked.
//   clang -w gl-probe-deriv.c -framework OpenGL -o gl-probe-deriv && ./gl-probe-deriv
#define GL_SILENCE_DEPRECATION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#define W 1280
#define H 720
static GLuint cs(GLenum t, const char *s) { GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, 0); glCompileShader(o);
    GLint ok; glGetShaderiv(o, GL_COMPILE_STATUS, &ok); if (!ok) { char l[512]; glGetShaderInfoLog(o, 512, 0, l); printf("shader error: %s\n", l); exit(3); } return o; }
static int total_fail;
static GLuint vao, vbc, ibo, fbo, rt;
static unsigned char *px;

static const char *VS =
    "#version 150\nin vec2 corner; uniform vec4 rect; uniform int flagv; out vec2 vpos; flat out int vflag; flat out vec4 vrect;\n"
    "void main(){ vec2 p = mix(rect.xy, rect.zw, corner); gl_Position = vec4(p / vec2(1280.0, 720.0) * 2.0 - 1.0, 0.0, 1.0); vpos = p; vflag = flagv; vrect = rect; }\n";
// each variant writes r = fwidth.x / 4, g = fwidth.y / 4 (so exact value 1 -> 64), b = 255 as a 'shaded' marker
static const char *FS_PLAIN = "#version 150\nin vec2 vpos; flat in int vflag; flat in vec4 vrect; out vec4 o; void main(){ vec2 w = fwidth(vpos); o = vec4(w * 0.25, 1.0, 1.0); }\n";
static const char *FS_BRANCH = "#version 150\nin vec2 vpos; flat in int vflag; flat in vec4 vrect; out vec4 o; void main(){ vec2 w = vec2(0.0); if (vflag != 0) { w = fwidth(vpos); } o = vec4(w * 0.25, 1.0, 1.0); }\n";
static const char *FS_DFDX = "#version 150\nin vec2 vpos; flat in int vflag; flat in vec4 vrect; out vec4 o; void main(){ o = vec4(abs(dFdx(vpos.x)) * 0.25, abs(dFdy(vpos.y)) * 0.25, 1.0, 1.0); }\n";
// WebRender's formula: aa_range = fwidth(pos); dist = signed_distance / aa_range; alpha = clamp(0.5 - max(dist.x, dist.y), 0, 1). For a pixel well inside the rect the distance is large and
// negative, so alpha must be exactly 1 everywhere inside; a bad fwidth turns that into NaN/0.
static const char *FS_WR = "#version 150\nin vec2 vpos; flat in int vflag; flat in vec4 vrect; out vec4 o;\n"
    "void main(){ float alpha = 1.0; if (vflag != 0) { vec2 aa_range = fwidth(vpos); vec2 d = max(vrect.xy - vpos, vpos - vrect.zw) / aa_range; alpha = clamp(0.5 - max(d.x, d.y), 0.0, 1.0); } o = vec4(alpha * 0.25, alpha * 0.25, 1.0, 1.0); }\n";

static GLuint prog(const char *fs) { GLuint p = glCreateProgram(); glAttachShader(p, cs(GL_VERTEX_SHADER, VS)); glAttachShader(p, cs(GL_FRAGMENT_SHADER, fs)); glBindAttribLocation(p, 0, "corner"); glLinkProgram(p); return p; }
static void run(const char *name, GLuint p, int indexed, int flag, float x0, float y0, float x1, float y1, int expect_r, int expect_g) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, W, H); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(p); glUniform4f(glGetUniformLocation(p, "rect"), x0, y0, x1, y1); glUniform1i(glGetUniformLocation(p, "flagv"), flag); glBindBuffer(GL_ARRAY_BUFFER, vbc);
    if (indexed) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo); glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0, 1); } else glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, 1);
    glFinish(); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    int bad = 0, diagBad = 0, inside = 0, fx = -1, fy = -1, gr = 0, gg = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { const unsigned char *g = px + ((size_t)y * W + x) * 4; if (g[2] < 128) continue;       // not shaded
        if (x + 0.5f > x0 + 1 && x + 0.5f < x1 - 1 && y + 0.5f > y0 + 1 && y + 0.5f < y1 - 1) { inside++; if (abs(g[0] - expect_r) > 2 || abs(g[1] - expect_g) > 2) { if (!bad) { fx = x; fy = y; gr = g[0]; gg = g[1]; } bad++;
            float u = (x - x0) / (x1 - x0), v = (y - y0) / (y1 - y0); if (abs(u - v) < 0.01f || abs(u + v - 1) < 0.01f) diagBad++; } } }
    printf("%-72s %s (%d of %d interior pixels wrong, %d of those within 1%% of a diagonal", name, bad ? "FAIL" : "ok  ", bad, inside, diagBad); if (bad) { printf("; first at (%d,%d) got r=%d g=%d want %d,%d", fx, fy, gr, gg, expect_r, expect_g); total_fail++; } printf(")\n"); }

int main(void) {
    CGLPixelFormatAttribute at[] = { kCGLPFAAccelerated, kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, 0 };
    CGLPixelFormatObj pf; GLint np; CGLContextObj ctx; if (CGLChoosePixelFormat(at, &pf, &np) || CGLCreateContext(pf, NULL, &ctx)) { printf("no context\n"); return 2; }
    CGLSetCurrentContext(ctx); printf("%s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    px = malloc((size_t)W * H * 4); glGenVertexArrays(1, &vao); glBindVertexArray(vao); glGenFramebuffers(1, &fbo); glGenTextures(1, &rt); glGenBuffers(1, &vbc); glGenBuffers(1, &ibo);
    glBindTexture(GL_TEXTURE_2D, rt); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0); glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
    float c[8] = { 0, 0, 1, 0, 0, 1, 1, 1 }; glBindBuffer(GL_ARRAY_BUFFER, vbc); glBufferData(GL_ARRAY_BUFFER, sizeof c, c, GL_STATIC_DRAW); glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0);
    unsigned short idx[6] = { 0, 1, 2, 2, 1, 3 }; glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
    GLuint pP = prog(FS_PLAIN), pB = prog(FS_BRANCH), pD = prog(FS_DFDX), pW = prog(FS_WR);
    for (int ix = 0; ix < 2; ix++) { const char *how = ix ? "indexed TRIANGLES" : "TRIANGLE_STRIP"; char n[120];
        snprintf(n, sizeof n, "%s: fwidth(vpos), plain (expect 1,1)", how); run(n, pP, ix, 1, 40.3f, 30.7f, 1240.2f, 690.4f, 64, 64);
        snprintf(n, sizeof n, "%s: fwidth inside 'if (flat int != 0)'", how); run(n, pB, ix, 1, 40.3f, 30.7f, 1240.2f, 690.4f, 64, 64);
        snprintf(n, sizeof n, "%s: dFdx / dFdy", how); run(n, pD, ix, 1, 40.3f, 30.7f, 1240.2f, 690.4f, 64, 64);
        snprintf(n, sizeof n, "%s: WebRender AA formula, alpha must be 1 inside", how); run(n, pW, ix, 1, 40.3f, 30.7f, 1240.2f, 690.4f, 64, 64);
        snprintf(n, sizeof n, "%s: same, small quad 90x60", how); run(n, pW, ix, 1, 100.2f, 100.7f, 190.2f, 160.7f, 64, 64);
        snprintf(n, sizeof n, "%s: same, skinny quad 900x6", how); run(n, pW, ix, 1, 100.2f, 100.7f, 1000.2f, 106.7f, 64, 64); }
    printf("--- %d failing group(s)\n", total_fail); return total_fail != 0; }
