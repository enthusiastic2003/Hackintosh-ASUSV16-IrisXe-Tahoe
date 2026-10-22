// Which varying shapes make Apple's GL-on-Metal layer fail to link ("type of fragment input user(slotN) does not match
// the type of the corresponding vertex shader output") and fall back to software vertex processing?
// Each case draws a fullscreen triangle with the varyings and checks the pixel; the link failures are counted from
// libxedrv's log (/tmp/xedrv-<program>.log, lines "PIPE ... FALHOU"), so run it with the xedrv driver loaded as usual.
//   clang gl-probe-varyings.c -framework OpenGL -o gl-probe-varyings && ./gl-probe-varyings
#define GL_SILENCE_DEPRECATION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

static int count_fail(void) {
    char p[256]; snprintf(p, sizeof p, "/tmp/xedrv-%s.log", getprogname()); FILE *f = fopen(p, "r"); if (!f) return -1;
    char l[1024]; int n = 0; while (fgets(l, sizeof l, f)) if (strstr(l, "FALHOU")) n++; fclose(f); return n;
}
static GLuint compile(GLenum t, const char *s) { GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, NULL); glCompileShader(o);
    GLint ok; glGetShaderiv(o, GL_COMPILE_STATUS, &ok); if (!ok) { char l[512]; glGetShaderInfoLog(o, 512, NULL, l); printf("shader error: %s\n", l); return 0; } return o; }
static GLuint fbo; static GLuint rt;
// vsDecl/fsDecl: declarations; vsBody: assignments; fsBody: expression for vec4 o
static void run(const char *name, const char *vsDecl, const char *vsBody, const char *fsDecl, const char *fsBody, float e0, float e1, float e2) {
    char vs[2048], fs[2048];
    snprintf(vs, sizeof vs, "#version 150\n%s\nvoid main(){ vec2 q[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3)); gl_Position = vec4(q[gl_VertexID],0,1); %s }\n", vsDecl, vsBody);
    snprintf(fs, sizeof fs, "#version 150\n%s\nout vec4 o; void main(){ %s }\n", fsDecl, fsBody);
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs); if (!v || !f) { printf("%-58s compile error\n", name); return; }
    GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok); if (!ok) { printf("%-58s GL link error\n", name); return; }
    int before = count_fail();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, 4, 4); glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(p); glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
    float px[4 * 4 * 4]; glReadPixels(0, 0, 4, 4, GL_RGBA, GL_FLOAT, px);
    int after = count_fail(); int right = px[0] > e0 - 0.02f && px[0] < e0 + 0.02f && px[1] > e1 - 0.02f && px[1] < e1 + 0.02f && px[2] > e2 - 0.02f && px[2] < e2 + 0.02f;
    printf("%-58s link-failures=%d  pixel %s (%.2f %.2f %.2f)\n", name, after - before, right ? "ok" : "WRONG", px[0], px[1], px[2]);
    glDeleteProgram(p);
}
int main(void) {
    CGLPixelFormatAttribute at[] = { kCGLPFAAccelerated, kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, 0 };
    CGLPixelFormatObj pf; GLint np; CGLContextObj ctx;
    if (CGLChoosePixelFormat(at, &pf, &np) || CGLCreateContext(pf, NULL, &ctx)) { printf("no context\n"); return 2; }
    CGLSetCurrentContext(ctx);
    printf("%s | %s | xedrv log fail count now %d\n", glGetString(GL_RENDERER), glGetString(GL_VERSION), count_fail());
    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenFramebuffers(1, &fbo); glGenTextures(1, &rt); glBindTexture(GL_TEXTURE_2D, rt); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 4, 0, GL_RGBA, GL_FLOAT, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);

    // single varyings
    run("smooth float",         "out float v;",          "v = 0.25;",                  "in float v;",          "o = vec4(v, 0, 0, 1);", 0.25f, 0, 0);
    run("smooth vec4",          "out vec4 v;",           "v = vec4(.1,.2,.3,1);",      "in vec4 v;",           "o = v;", .1f, .2f, .3f);
    run("flat float",           "flat out float v;",     "v = 0.5;",                   "flat in float v;",     "o = vec4(v, 0, 0, 1);", 0.5f, 0, 0);
    run("flat vec4",            "flat out vec4 v;",      "v = vec4(.1,.2,.3,1);",      "flat in vec4 v;",      "o = v;", .1f, .2f, .3f);
    run("flat int",             "flat out int v;",       "v = 3;",                     "flat in int v;",       "o = vec4(float(v)/10., 0, 0, 1);", .3f, 0, 0);
    run("flat ivec2",           "flat out ivec2 v;",     "v = ivec2(3, 4);",           "flat in ivec2 v;",     "o = vec4(vec2(v)/10., 0, 1);", .3f, .4f, 0);
    run("flat ivec4",           "flat out ivec4 v;",     "v = ivec4(3,4,5,6);",        "flat in ivec4 v;",     "o = vec4(vec3(v.xyz)/10., 1);", .3f, .4f, .5f);
    run("flat uint",            "flat out uint v;",      "v = 3u;",                    "flat in uint v;",      "o = vec4(float(v)/10., 0, 0, 1);", .3f, 0, 0);
    run("flat uvec4",           "flat out uvec4 v;",     "v = uvec4(3u,4u,5u,6u);",    "flat in uvec4 v;",     "o = vec4(vec3(v.xyz)/10., 1);", .3f, .4f, .5f);
    run("noperspective vec2",   "noperspective out vec2 v;", "v = vec2(.2,.4);",       "noperspective in vec2 v;", "o = vec4(v, 0, 1);", .2f, .4f, 0);
    // mixes: WebRender-style (flat ints and smooth floats together)
    run("smooth vec2 + flat ivec4",
        "out vec2 a; flat out ivec4 b;", "a = vec2(.2,.4); b = ivec4(1,2,3,4);",
        "in vec2 a; flat in ivec4 b;", "o = vec4(a, float(b.z)/10., 1);", .2f, .4f, .3f);
    run("flat ivec4 first, then smooth vec2 (order swapped in FS)",
        "flat out ivec4 b; out vec2 a;", "a = vec2(.2,.4); b = ivec4(1,2,3,4);",
        "in vec2 a; flat in ivec4 b;", "o = vec4(a, float(b.z)/10., 1);", .2f, .4f, .3f);
    // outputs the fragment shader does not read (very common: dead varyings are stripped on one side)
    run("VS has 2 extra unused smooth outputs",
        "out vec4 x; out vec4 y; out vec2 a;", "x = vec4(1); y = vec4(2); a = vec2(.2,.4);",
        "in vec2 a;", "o = vec4(a, 0, 1);", .2f, .4f, 0);
    run("VS has unused flat ivec4 before a used smooth vec2",
        "flat out ivec4 u; out vec2 a;", "u = ivec4(1,2,3,4); a = vec2(.2,.4);",
        "in vec2 a;", "o = vec4(a, 0, 1);", .2f, .4f, 0);
    run("VS has unused smooth vec4 before a used flat ivec4",
        "out vec4 u; flat out ivec4 b;", "u = vec4(1); b = ivec4(3,4,5,6);",
        "flat in ivec4 b;", "o = vec4(vec3(b.xyz)/10., 1);", .3f, .4f, .5f);
    run("flat int unused, flat ivec2 used",
        "flat out int u; flat out ivec2 b;", "u = 7; b = ivec2(3,4);",
        "flat in ivec2 b;", "o = vec4(vec2(b)/10., 0, 1);", .3f, .4f, 0);
    // many varyings, WebRender-like count with a mix of kinds
    run("10 varyings (4 flat ivec4, 3 smooth vec4, 3 smooth vec2)",
        "flat out ivec4 f0; flat out ivec4 f1; flat out ivec4 f2; flat out ivec4 f3; out vec4 s0; out vec4 s1; out vec4 s2; out vec2 t0; out vec2 t1; out vec2 t2;",
        "f0=ivec4(1); f1=ivec4(2); f2=ivec4(3); f3=ivec4(4); s0=vec4(.1); s1=vec4(.2); s2=vec4(.3); t0=vec2(.4); t1=vec2(.5); t2=vec2(.6);",
        "flat in ivec4 f0; flat in ivec4 f1; flat in ivec4 f2; flat in ivec4 f3; in vec4 s0; in vec4 s1; in vec4 s2; in vec2 t0; in vec2 t1; in vec2 t2;",
        "o = vec4(float(f0.x+f1.x+f2.x+f3.x)/10., s0.x+s1.x+s2.x, t0.x+t1.x+t2.x, 1);", 1.0f, .6f, 1.5f);
    run("9 varyings where FS reads only the last 3",
        "out vec4 a0; out vec4 a1; out vec4 a2; out vec4 a3; out vec4 a4; out vec4 a5; flat out ivec4 b0; flat out ivec4 b1; out vec2 c;",
        "a0=a1=a2=a3=a4=a5=vec4(.1); b0=ivec4(1); b1=ivec4(2); c=vec2(.2,.4);",
        "flat in ivec4 b0; flat in ivec4 b1; in vec2 c;", "o = vec4(c, float(b0.x+b1.x)/10., 1);", .2f, .4f, .3f);
    printf("--- link failures in the xedrv log: %d (counted from the start of the log)\n", count_fail());
    return 0;
}
