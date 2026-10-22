// Tests the OpenGL operations Minecraft's chunk renderer relies on, offscreen, with checkable results.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#define W 64
#define H 64
static int total_fail;
static void report(const char *name, int bad, int checked) {
    printf("%-58s %s  (%d bad of %d)\n", name, bad ? "FAIL" : "ok  ", bad, checked);
    if (bad) total_fail++;
}
static GLuint compile(GLenum t, const char *s) { GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, NULL); glCompileShader(o);
    GLint ok; glGetShaderiv(o, GL_COMPILE_STATUS, &ok); if (!ok) { char l[512]; glGetShaderInfoLog(o, 512, NULL, l); printf("shader error: %s\n", l); exit(2); } return o; }
static GLuint program(const char *vs, const char *fs) { GLuint p = glCreateProgram(); glAttachShader(p, compile(GL_VERTEX_SHADER, vs)); glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "p"); glBindAttribLocation(p, 1, "c"); glLinkProgram(p); return p; }
static void clear(void) { glDisable(GL_SCISSOR_TEST); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); }
static unsigned char px[W * H * 4];
static void grab(void) { glFinish(); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); }
static unsigned char *at(int x, int y) { return &px[(y * W + x) * 4]; }

// two triangles, left red and right green; 5 floats per vertex: x y r g b
static const float tris[] = {
    -0.9f,-0.9f,1,0,0,  -0.1f,-0.9f,1,0,0,  -0.5f,0.8f,1,0,0,
     0.1f,-0.9f,0,1,0,   0.9f,-0.9f,0,1,0,   0.5f,0.8f,0,1,0 };

// every non-black pixel must be the expected colour for its half; returns the number of wrong pixels
static int check_halves(int want_left, int want_right, int *colored) {
    int bad = 0; *colored = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        unsigned char *c = at(x, y); int black = !c[0] && !c[1] && !c[2];
        int red = c[0] == 255 && !c[1] && !c[2], green = !c[0] && c[1] == 255 && !c[2];
        if (!black) (*colored)++;
        if (x < W / 2) { if (!(black || (want_left && red))) bad++; } else { if (!(black || (want_right && green))) bad++; }
    }
    return bad;
}

int main(void) {
    CGLPixelFormatAttribute at_[] = { kCGLPFAAccelerated, kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, 0 };
    CGLPixelFormatObj pf; GLint np; CGLContextObj ctx;
    if (CGLChoosePixelFormat(at_, &pf, &np) || CGLCreateContext(pf, NULL, &ctx)) { printf("no context\n"); return 2; }
    CGLSetCurrentContext(ctx);
    printf("%s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    GLuint fbo, tex, vao, vbo, ibo;
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glViewport(0, 0, W, H);
    GLuint prog = program("#version 150\nin vec2 p; in vec3 c; out vec3 vc; void main(){ gl_Position=vec4(p,0,1); vc=c; }\n",
                          "#version 150\nin vec3 vc; out vec4 o; void main(){ o=vec4(vc,1); }\n");
    glUseProgram(prog);
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, sizeof(tris), tris, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 20, (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 20, (void *)8);
    unsigned idx[] = { 0, 1, 2 };
    glGenBuffers(1, &ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    int bad, col;

    // A: plain draw, vertices 0..2 only: left red triangle
    clear(); glDrawArrays(GL_TRIANGLES, 0, 3); grab(); bad = check_halves(1, 0, &col);
    report("A  glDrawArrays (left triangle only)", bad || col < 300, W * H);

    // B: glDrawElementsBaseVertex twice: indices 0,1,2 with base 0 then base 3
    clear();
    glDrawElementsBaseVertex(GL_TRIANGLES, 3, GL_UNSIGNED_INT, 0, 0);
    glDrawElementsBaseVertex(GL_TRIANGLES, 3, GL_UNSIGNED_INT, 0, 3);
    grab(); bad = check_halves(1, 1, &col);
    report("B  glDrawElementsBaseVertex (base 0, then base 3)", bad || col < 600, W * H);

    // C: instanced + base vertex
    clear(); glDrawElementsInstancedBaseVertex(GL_TRIANGLES, 3, GL_UNSIGNED_INT, 0, 1, 3);
    grab(); bad = check_halves(0, 1, &col);
    report("C  glDrawElementsInstancedBaseVertex (base 3 only)", bad || col < 300, W * H);

    // D: indirect draw with a base vertex (Minecraft lists GL_ARB_draw_indirect)
    GLuint ind; struct { GLuint count, inst, first; GLint base; GLuint binst; } cmd = { 3, 1, 0, 3, 0 };
    glGenBuffers(1, &ind); glBindBuffer(GL_DRAW_INDIRECT_BUFFER, ind); glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(cmd), &cmd, GL_STATIC_DRAW);
    clear(); glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, 0);
    grab(); bad = check_halves(0, 1, &col);
    report("D  glDrawElementsIndirect (base vertex 3)", bad || col < 300, W * H);

    // E: multi draw with an offset into the index buffer and different base vertices
    unsigned idx2[] = { 0, 1, 2, 0, 1, 2 }; glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx2), idx2, GL_STATIC_DRAW);
    GLsizei counts[2] = { 3, 3 }; const void *offs[2] = { (void *)0, (void *)(3 * sizeof(unsigned)) }; GLint bases[2] = { 0, 3 };
    clear(); glMultiDrawElementsBaseVertex(GL_TRIANGLES, counts, GL_UNSIGNED_INT, offs, 2, bases);
    grab(); bad = check_halves(1, 1, &col);
    report("E  glMultiDrawElementsBaseVertex (offset + 2 bases)", bad || col < 600, W * H);

    // F: buffer updates between draws, results compared at the end (does an update land before the draw that follows it?)
    const int N = 256; GLuint t2, f2; glGenTextures(1, &t2); glBindTexture(GL_TEXTURE_2D, t2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &f2); glBindFramebuffer(GL_FRAMEBUFFER, f2); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t2, 0);
    glViewport(0, 0, 8, N);
    GLuint vb2; glGenBuffers(1, &vb2); glBindBuffer(GL_ARRAY_BUFFER, vb2); glBufferData(GL_ARRAY_BUFFER, 60, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 20, (void *)0); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 20, (void *)8);
    const char *mname[3] = { "F1 glBufferSubData between draws", "F2 orphan (glBufferData NULL) + glBufferSubData", "F3 glMapBufferRange write + unmap" };
    for (int m = 0; m < 3; m++) {
        glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glEnable(GL_SCISSOR_TEST);
        for (int i = 0; i < N; i++) {
            float r = (i % 3 == 0), g = (i % 3 == 1), b = (i % 3 == 2);
            float v[15] = { -1,-1,r,g,b,  3,-1,r,g,b,  -1,3,r,g,b };
            if (m == 0) glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
            else if (m == 1) { glBufferData(GL_ARRAY_BUFFER, 60, NULL, GL_DYNAMIC_DRAW); glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v); }
            else { void *p = glMapBufferRange(GL_ARRAY_BUFFER, 0, 60, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT); memcpy(p, v, sizeof(v)); glUnmapBuffer(GL_ARRAY_BUFFER); }
            glScissor(0, i, 8, 1); glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glFinish(); static unsigned char big[8 * 256 * 4]; glReadPixels(0, 0, 8, N, GL_RGBA, GL_UNSIGNED_BYTE, big);
        int badrows = 0;
        for (int i = 0; i < N; i++) { unsigned char *c = &big[(i * 8 + 3) * 4];
            int er = (i % 3 == 0) * 255, eg = (i % 3 == 1) * 255, eb = (i % 3 == 2) * 255; if (c[0] != er || c[1] != eg || c[2] != eb) badrows++; }
        report(mname[m], badrows, N);
    }
    glDisable(GL_SCISSOR_TEST);

    printf("%s\n", total_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return total_fail ? 1 : 0;
}
