// Isolates WHY rewriting an index buffer between draws gives stale data: synchronisation / buffer reuse variants.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
static GLuint compile(GLenum t, const char *s) { GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, NULL); glCompileShader(o); return o; }
#define N 256
static unsigned char big[8 * N * 4];
static GLuint vbo, ibo[N], fbo;
// sync: 0 none, 1 glFlush per draw, 2 glFinish per draw; reuse: 0 same buffer, 1 two alternating, 2 one buffer per draw
// how: 0 SubData, 1 BufferData (full replace), 2 map+unmap, 3 orphan+SubData;  idx32: 1 = 32-bit indices
static int run(int sync, int reuse, int how, int idx32, int pad) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, 8, N);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glEnable(GL_SCISSOR_TEST);
    size_t isz = idx32 ? 4 : 2, bytes = 3 * isz + pad;
    for (int i = 0; i < N; i++) {
        GLuint b = ibo[reuse == 0 ? 0 : reuse == 1 ? (i & 1) : i];
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b);
        int k = i % 3; uint32_t a32[3] = { k * 3, k * 3 + 1, k * 3 + 2 }; uint16_t a16[3] = { (uint16_t)(k * 3), (uint16_t)(k * 3 + 1), (uint16_t)(k * 3 + 2) };
        const void *src = idx32 ? (const void *)a32 : (const void *)a16; size_t n = 3 * isz;
        if (how == 0) glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, n, src);
        else if (how == 1) glBufferData(GL_ELEMENT_ARRAY_BUFFER, n, src, GL_DYNAMIC_DRAW);
        else if (how == 2) { void *q = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, n, GL_MAP_WRITE_BIT); memcpy(q, src, n); glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER); }
        else { glBufferData(GL_ELEMENT_ARRAY_BUFFER, bytes, NULL, GL_DYNAMIC_DRAW); glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, n, src); }
        glScissor(0, i, 8, 1);
        glDrawElements(GL_TRIANGLES, 3, idx32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, 0);
        if (sync == 1) glFlush(); else if (sync == 2) glFinish();
    }
    glFinish(); glReadPixels(0, 0, 8, N, GL_RGBA, GL_UNSIGNED_BYTE, big); int bad = 0;
    for (int i = 0; i < N; i++) { unsigned char *c = &big[(i * 8 + 3) * 4]; int k = i % 3; if (c[0] != (k == 0) * 255 || c[1] != (k == 1) * 255 || c[2] != (k == 2) * 255) bad++; }
    glDisable(GL_SCISSOR_TEST); return bad;
}
int main(void) {
    CGLPixelFormatAttribute at[] = { kCGLPFAAccelerated, kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, 0 };
    CGLPixelFormatObj pf; GLint np; CGLContextObj ctx; CGLChoosePixelFormat(at, &pf, &np); CGLCreateContext(pf, NULL, &ctx); CGLSetCurrentContext(ctx);
    printf("%s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    GLuint vao, tex; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo); glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    float v[3 * 3 * 5]; for (int k = 0; k < 3; k++) for (int i = 0; i < 3; i++) { float *q = &v[(k * 3 + i) * 5]; q[0] = i == 1 ? 3 : -1; q[1] = i == 2 ? 3 : -1; q[2] = k == 0; q[3] = k == 1; q[4] = k == 2; }
    GLuint p = glCreateProgram(); glAttachShader(p, compile(GL_VERTEX_SHADER, "#version 150\nin vec2 p; in vec3 c; out vec3 vc; void main(){ gl_Position=vec4(p,0,1); vc=c; }\n"));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, "#version 150\nin vec3 vc; out vec4 o; void main(){ o=vec4(vc,1); }\n")); glBindAttribLocation(p, 0, "p"); glBindAttribLocation(p, 1, "c"); glLinkProgram(p); glUseProgram(p);
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 20, (void *)0); glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 20, (void *)8);
    glGenBuffers(N, ibo);
    for (int i = 0; i < N; i++) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo[i]); glBufferData(GL_ELEMENT_ARRAY_BUFFER, 64, NULL, GL_DYNAMIC_DRAW); }   // storage first: SubData on an empty buffer is an error
    const char *sy[3] = { "no sync", "glFlush each draw", "glFinish each draw" }, *re[3] = { "same buffer", "2 alternating", "new buffer each" };
    const char *ho[4] = { "SubData", "BufferData(data)", "map+unmap", "orphan+SubData" };
    printf("rows wrong out of %d (0 = correct):\n", N);
    for (int idx32 = 1; idx32 >= 0; idx32--) {
        printf("\n== %d-bit indices\n", idx32 ? 32 : 16);
        printf("%-20s %-16s", "sync", "buffers"); for (int h = 0; h < 4; h++) printf(" %-17s", ho[h]); printf("\n");
        for (int s = 0; s < 3; s++) for (int r = 0; r < 3; r++) { printf("%-20s %-16s", sy[s], re[r]);
            for (int h = 0; h < 4; h++) printf(" %-17d", run(s, r, h, idx32, 0)); printf("\n"); }
    }
    return 0;
}
