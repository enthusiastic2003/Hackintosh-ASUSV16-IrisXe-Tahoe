// Vertex formats, index-buffer updates, culling, depth, blending and flat varyings, offscreen, with checkable results.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#define W 64
#define H 64
static int total_fail;
static void report(const char *n, int bad, int of) { printf("%-60s %s (%d bad of %d)\n", n, bad ? "FAIL" : "ok  ", bad, of); if (bad) total_fail++; }
static GLuint compile(GLenum t, const char *s) { GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, NULL); glCompileShader(o);
    GLint ok; glGetShaderiv(o, GL_COMPILE_STATUS, &ok); if (!ok) { char l[512]; glGetShaderInfoLog(o, 512, NULL, l); printf("shader error: %s\n", l); exit(2); } return o; }
static GLuint program(const char *vs, const char *fs) { GLuint p = glCreateProgram(); glAttachShader(p, compile(GL_VERTEX_SHADER, vs)); glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "p"); glBindAttribLocation(p, 1, "c"); glLinkProgram(p); return p; }
static unsigned char px[W * H * 4];
static void clear(void) { glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE); glClearColor(0, 0, 0, 1); glClearDepth(1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }
static void grab(void) { glFinish(); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); }
static int lit(int x, int y) { return px[(y * W + x) * 4] > 127; }
// ideal triangle A(-.8,-.8) B(.8,-.8) C(-.8,.8) in NDC: count pixels (away from the edges) that disagree
static int tri_bad(void) {
    int bad = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        double nx = (x + 0.5) / W * 2 - 1, ny = (y + 0.5) / H * 2 - 1;
        double d = (nx + ny) / 1.41421356;            // distance to the hypotenuse x+y=0
        int inside = nx > -0.8 && ny > -0.8 && (nx + ny) < 0;
        double margin = fmin(fmin(fabs(nx + 0.8), fabs(ny + 0.8)), fabs(d)) * (W / 2.0);
        if (margin < 1.5) continue;                    // skip pixels on an edge
        if (lit(x, y) != inside) bad++;
    }
    return bad;
}
static uint16_t half(float f) { union { float f; uint32_t u; } v = { f }; uint32_t s = (v.u >> 16) & 0x8000, e = ((v.u >> 23) & 0xff) - 112, m = (v.u >> 13) & 0x3ff; return (uint16_t)(s | (e << 10) | m); }

static GLuint progF, progI, fbo, vao, vbo;
static void fmt_test(const char *name, GLenum type, int n, GLboolean norm, int integer, int stride, int offset, const void *data, size_t bytes, float s, float ox, float oy) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, bytes, data, GL_STATIC_DRAW);
    glUseProgram(integer ? progI : progF);
    glUniform1f(glGetUniformLocation(integer ? progI : progF, "s"), s);
    glUniform2f(glGetUniformLocation(integer ? progI : progF, "o"), ox, oy);
    glEnableVertexAttribArray(0);
    if (integer) glVertexAttribIPointer(0, n, type, stride, (void *)(intptr_t)offset);
    else glVertexAttribPointer(0, n, type, norm, stride, (void *)(intptr_t)offset);
    clear(); glDrawArrays(GL_TRIANGLES, 0, 3); grab(); report(name, tri_bad(), W * H);
}

int main(void) {
    CGLPixelFormatAttribute at[] = { kCGLPFAAccelerated, kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, kCGLPFADepthSize, 24, 0 };
    CGLPixelFormatObj pf; GLint np; CGLContextObj ctx;
    if (CGLChoosePixelFormat(at, &pf, &np) || CGLCreateContext(pf, NULL, &ctx)) { printf("no context\n"); return 2; }
    CGLSetCurrentContext(ctx);
    printf("%s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    GLuint tex, rb; glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glGenRenderbuffers(1, &rb); glBindRenderbuffer(GL_RENDERBUFFER, rb); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb);
    glViewport(0, 0, W, H); glGenVertexArrays(1, &vao); glBindVertexArray(vao); glGenBuffers(1, &vbo);
    progF = program("#version 150\nin vec4 p; uniform float s; uniform vec2 o; void main(){ gl_Position=vec4(p.xy*s+o,0,1); }\n", "#version 150\nout vec4 c; void main(){ c=vec4(1); }\n");
    progI = program("#version 150\nin ivec4 p; uniform float s; uniform vec2 o; void main(){ gl_Position=vec4(vec2(p.xy)*s+o,0,1); }\n", "#version 150\nout vec4 c; void main(){ c=vec4(1); }\n");
    const float X[3] = { -0.8f, 0.8f, -0.8f }, Y[3] = { -0.8f, -0.8f, 0.8f };    // A, B, C in NDC
    printf("--- vertex formats (one triangle each, positions decoded in the shader)\n");
    { float v[9]; for (int i = 0; i < 3; i++) { v[i*3] = X[i]; v[i*3+1] = Y[i]; v[i*3+2] = 0; } fmt_test("P1 FLOAT x3, stride 12", GL_FLOAT, 3, GL_FALSE, 0, 12, 0, v, sizeof v, 1, 0, 0); }
    { int16_t v[9]; for (int i = 0; i < 3; i++) { v[i*3] = (int16_t)(X[i]*1000); v[i*3+1] = (int16_t)(Y[i]*1000); v[i*3+2] = 0; } fmt_test("P2 SHORT x3 (6 byte stride, not normalised)", GL_SHORT, 3, GL_FALSE, 0, 6, 0, v, sizeof v, 0.001f, 0, 0); }
    { int16_t v[12]; for (int i = 0; i < 3; i++) { v[i*4] = (int16_t)(X[i]*1000); v[i*4+1] = (int16_t)(Y[i]*1000); v[i*4+2] = 0; v[i*4+3] = 1; } fmt_test("P3 SHORT x4 (8 byte stride, not normalised)", GL_SHORT, 4, GL_FALSE, 0, 8, 0, v, sizeof v, 0.001f, 0, 0); }
    { int16_t v[6]; for (int i = 0; i < 3; i++) { v[i*2] = (int16_t)(X[i]*32767); v[i*2+1] = (int16_t)(Y[i]*32767); } fmt_test("P4 SHORT x2 normalised", GL_SHORT, 2, GL_TRUE, 0, 4, 0, v, sizeof v, 1, 0, 0); }
    { uint16_t v[6]; for (int i = 0; i < 3; i++) { v[i*2] = (uint16_t)((X[i]+1)/2*65535); v[i*2+1] = (uint16_t)((Y[i]+1)/2*65535); } fmt_test("P5 UNSIGNED_SHORT x2 normalised", GL_UNSIGNED_SHORT, 2, GL_TRUE, 0, 4, 0, v, sizeof v, 2, -1, -1); }
    { int8_t v[12]; for (int i = 0; i < 3; i++) { v[i*4] = (int8_t)(X[i]*127); v[i*4+1] = (int8_t)(Y[i]*127); v[i*4+2] = 0; v[i*4+3] = 0; } fmt_test("P6 BYTE x3 normalised (4 byte stride)", GL_BYTE, 3, GL_TRUE, 0, 4, 0, v, sizeof v, 1, 0, 0); }
    { int8_t v[9]; for (int i = 0; i < 3; i++) { v[i*3] = (int8_t)(X[i]*127); v[i*3+1] = (int8_t)(Y[i]*127); v[i*3+2] = 0; } fmt_test("P7 BYTE x3 normalised (3 byte stride)", GL_BYTE, 3, GL_TRUE, 0, 3, 0, v, sizeof v, 1, 0, 0); }
    { uint8_t v[12]; for (int i = 0; i < 3; i++) { v[i*4] = (uint8_t)((X[i]+1)/2*255); v[i*4+1] = (uint8_t)((Y[i]+1)/2*255); v[i*4+2] = 0; v[i*4+3] = 0; } fmt_test("P8 UNSIGNED_BYTE x3 normalised", GL_UNSIGNED_BYTE, 3, GL_TRUE, 0, 4, 0, v, sizeof v, 2, -1, -1); }
    { uint32_t v[3]; for (int i = 0; i < 3; i++) { int xi = (int)(X[i]*511) & 0x3ff, yi = (int)(Y[i]*511) & 0x3ff; v[i] = xi | (yi << 10); } fmt_test("P9 INT_2_10_10_10_REV normalised", GL_INT_2_10_10_10_REV, 4, GL_TRUE, 0, 4, 0, v, sizeof v, 1, 0, 0); }
    { uint16_t v[6]; for (int i = 0; i < 3; i++) { v[i*2] = half(X[i]); v[i*2+1] = half(Y[i]); } fmt_test("P10 HALF_FLOAT x2", GL_HALF_FLOAT, 2, GL_FALSE, 0, 4, 0, v, sizeof v, 1, 0, 0); }
    { int16_t v[9]; for (int i = 0; i < 3; i++) { v[i*3] = 77; v[i*3+1] = (int16_t)(X[i]*1000); v[i*3+2] = (int16_t)(Y[i]*1000); } fmt_test("P11 SHORT x2 at offset 2 of a 6 byte stride", GL_SHORT, 2, GL_FALSE, 0, 6, 2, v, sizeof v, 0.001f, 0, 0); }
    { int16_t v[6]; for (int i = 0; i < 3; i++) { v[i*2] = (int16_t)(X[i]*1000); v[i*2+1] = (int16_t)(Y[i]*1000); } fmt_test("P12 integer attribute SHORT x2 (ivec, glVertexAttribIPointer)", GL_SHORT, 2, GL_FALSE, 1, 4, 0, v, sizeof v, 0.001f, 0, 0); }
    { uint8_t v[12]; for (int i = 0; i < 3; i++) { v[i*4] = (uint8_t)((X[i]+1)/2*255); v[i*4+1] = (uint8_t)((Y[i]+1)/2*255); v[i*4+2] = 0; v[i*4+3] = 0; } fmt_test("P13 integer attribute UNSIGNED_BYTE x4", GL_UNSIGNED_BYTE, 4, GL_FALSE, 1, 4, 0, v, sizeof v, 2.0f/255, -1, -1); }
    { int32_t v[6]; for (int i = 0; i < 3; i++) { v[i*2] = (int32_t)(X[i]*1000); v[i*2+1] = (int32_t)(Y[i]*1000); } fmt_test("P14 integer attribute INT x2", GL_INT, 2, GL_FALSE, 1, 8, 0, v, sizeof v, 0.001f, 0, 0); }
    glDisableVertexAttribArray(0);

    printf("--- index buffer rewritten between draws (what sorting translucent water does)\n");
    {   // three full-screen triangles in one VBO: red, green, blue; the index buffer picks one per row
        float v[3 * 3 * 5]; for (int k = 0; k < 3; k++) for (int i = 0; i < 3; i++) { float *q = &v[(k * 3 + i) * 5]; q[0] = i == 1 ? 3 : -1; q[1] = i == 2 ? 3 : -1; q[2] = k == 0; q[3] = k == 1; q[4] = k == 2; }
        GLuint pc = program("#version 150\nin vec2 p; in vec3 c; out vec3 vc; void main(){ gl_Position=vec4(p,0,1); vc=c; }\n", "#version 150\nin vec3 vc; out vec4 o; void main(){ o=vec4(vc,1); }\n");
        glUseProgram(pc); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 20, (void *)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 20, (void *)8);
        const int N = 256; GLuint t2, f2; glGenTextures(1, &t2); glBindTexture(GL_TEXTURE_2D, t2); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glGenFramebuffers(1, &f2); glBindFramebuffer(GL_FRAMEBUFFER, f2); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t2, 0); glViewport(0, 0, 8, N);
        GLuint ib; glGenBuffers(1, &ib); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib);
        const char *nm[4] = { "G1 index buffer glBufferSubData, 32-bit", "G2 index buffer glBufferSubData, 16-bit", "G3 index buffer glMapBufferRange, 32-bit", "G4 index buffer orphan + glBufferSubData, 16-bit" };
        for (int m = 0; m < 4; m++) {
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, 12, NULL, GL_DYNAMIC_DRAW); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glEnable(GL_SCISSOR_TEST);
            for (int i = 0; i < N; i++) { int k = i % 3; uint32_t i32[3] = { k * 3, k * 3 + 1, k * 3 + 2 }; uint16_t i16[3] = { (uint16_t)(k * 3), (uint16_t)(k * 3 + 1), (uint16_t)(k * 3 + 2) };
                if (m == 0) glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, 12, i32);
                else if (m == 1) glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, 6, i16);
                else if (m == 2) { void *q = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, 12, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT); memcpy(q, i32, 12); glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER); }
                else { glBufferData(GL_ELEMENT_ARRAY_BUFFER, 12, NULL, GL_DYNAMIC_DRAW); glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, 6, i16); }
                glScissor(0, i, 8, 1); glDrawElements(GL_TRIANGLES, 3, (m == 1 || m == 3) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, 0); }
            glFinish(); static unsigned char big[8 * 256 * 4]; glReadPixels(0, 0, 8, N, GL_RGBA, GL_UNSIGNED_BYTE, big); int bad = 0;
            for (int i = 0; i < N; i++) { unsigned char *c = &big[(i * 8 + 3) * 4]; int k = i % 3; if (c[0] != (k == 0) * 255 || c[1] != (k == 1) * 255 || c[2] != (k == 2) * 255) bad++; }
            report(nm[m], bad, N);
        }
        glDisable(GL_SCISSOR_TEST); glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, W, H);
    }

    printf("--- culling, depth and blending (the translucent water pass)\n");
    glEnableVertexAttribArray(0); glDisableVertexAttribArray(1);
    GLuint pz = program("#version 150\nin vec3 p; uniform vec4 col; out vec4 o; void main(){ gl_Position=vec4(p,1); }\n", "#version 150\nuniform vec4 col; out vec4 o; void main(){ o=col; }\n");
    glUseProgram(pz); GLint ucol = glGetUniformLocation(pz, "col");
    #define TRI(z, ccw) { float t[9] = { -1,-1,z,  ccw ? 3 : -1, ccw ? -1 : 3, z,  ccw ? -1 : 3, ccw ? 3 : -1, z }; glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, sizeof t, t, GL_STREAM_DRAW); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, 0); glDrawArrays(GL_TRIANGLES, 0, 3); }
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
    glUniform4f(ucol, 1, 1, 1, 1);
    clear(); TRI(0, 1); grab(); report("K1 counter-clockwise triangle is drawn (cull back, front = CCW)", lit(32, 32) ? 0 : 1, 1);
    clear(); TRI(0, 0); grab(); report("K2 clockwise triangle is culled (cull back, front = CCW)", lit(32, 32) ? 1 : 0, 1);
    glFrontFace(GL_CW);
    clear(); TRI(0, 1); grab(); report("K3 counter-clockwise triangle is culled (front = CW)", lit(32, 32) ? 1 : 0, 1);
    clear(); TRI(0, 0); grab(); report("K4 clockwise triangle is drawn (front = CW)", lit(32, 32) ? 0 : 1, 1);
    glFrontFace(GL_CCW); glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
    { int bad = 0;
      clear(); glUniform4f(ucol, 1, 0, 0, 1); TRI(-0.5f, 1); glUniform4f(ucol, 0, 1, 0, 1); TRI(0.5f, 1); grab(); bad += !(px[(32*W+32)*4] == 255 && px[(32*W+32)*4+1] == 0);       // near red first: far green rejected
      clear(); glUniform4f(ucol, 0, 1, 0, 1); TRI(0.5f, 1); glUniform4f(ucol, 1, 0, 0, 1); TRI(-0.5f, 1); grab(); bad += !(px[(32*W+32)*4] == 255 && px[(32*W+32)*4+1] == 0);       // far green first, near red wins
      clear(); glDepthMask(GL_FALSE); glUniform4f(ucol, 1, 0, 0, 1); TRI(-0.5f, 1); glUniform4f(ucol, 0, 1, 0, 1); TRI(0.5f, 1); grab(); bad += !(px[(32*W+32)*4] == 0 && px[(32*W+32)*4+1] == 255);   // no depth write: far green drawn over
      glDepthMask(GL_TRUE); report("D1 depth test LEQUAL, with and without depth writes", bad, 3); }
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    clear(); glUniform4f(ucol, 0, 0, 1, 0.5f); TRI(0, 1); TRI(0, 1); grab();
    { int b = px[(32*W+32)*4+2]; report("B1 alpha blending twice (0.5 blue over black: expect 191)", abs(b - 191) > 3, 1); printf("     got %d\n", b); }
    glDisable(GL_BLEND);

    printf("--- flat varying (GL: the LAST vertex of a triangle provides the value)\n");
    {
        GLuint pf2 = program("#version 150\nin float p; flat out float f; void main(){ f = p; vec2 q[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3)); gl_Position = vec4(q[gl_VertexID], 0, 1); }\n",
                             "#version 150\nflat in float f; out vec4 o; void main(){ o = vec4(f / 3.0, 0, 0, 1); }\n");
        glUseProgram(pf2); float a[3] = { 1, 2, 3 }; glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, sizeof a, a, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, 4, 0); clear(); glDrawArrays(GL_TRIANGLES, 0, 3); grab();
        int r = px[(32*W+32)*4]; printf("flat value used: %s (red=%d: 85 means the FIRST vertex, 255 the LAST)\n", r > 200 ? "last, as OpenGL specifies" : r > 60 && r < 110 ? "FIRST (differs from OpenGL)" : "other", r);
        report("S1 flat varying provoking vertex is the last vertex", r > 200 ? 0 : 1, 1);
    }
    printf("%s\n", total_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return total_fail ? 1 : 0;
}
