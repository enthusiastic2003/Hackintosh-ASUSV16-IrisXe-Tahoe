// Draws a coloured triangle into an offscreen framebuffer with a GLSL shader and checks the pixels.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

static int fails;
#define CHECK(c, msg) do { if (!(c)) { fails++; printf("FAIL: %s\n", msg); } else printf("ok:   %s\n", msg); } while (0)

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof(log), NULL, log); printf("shader log: %s\n", log); }
    return ok ? s : 0;
}

int main(int argc, char **argv)
{
    int want = argc > 1 ? atoi(argv[1]) : 41;            // 32 or 41
    CGLPixelFormatAttribute attrs[] = { kCGLPFAAccelerated, kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(want == 32 ? kCGLOGLPVersion_3_2_Core : kCGLOGLPVersion_GL4_Core), 0 };
    CGLPixelFormatObj pf; GLint np; CGLContextObj ctx;
    CHECK(CGLChoosePixelFormat(attrs, &pf, &np) == 0 && pf, "core profile pixel format");
    CHECK(CGLCreateContext(pf, NULL, &ctx) == 0, "context");
    CGLSetCurrentContext(ctx);
    printf("RENDERER=%s\nVERSION=%s\nGLSL=%s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION),
           glGetString(GL_SHADING_LANGUAGE_VERSION));

    const int W = 64, H = 64;
    GLuint fbo, tex, vao, vbo;
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "framebuffer complete");

    const char *vs = "#version 150\nin vec2 p;\nvoid main(){ gl_Position = vec4(p, 0.0, 1.0); }\n";
    const char *fs = "#version 150\nout vec4 c;\nvoid main(){ c = vec4(0.0, 1.0, 0.0, 1.0); }\n";   // green
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    CHECK(v && f, "vertex and fragment shaders compile");
    GLuint prog = glCreateProgram();
    glAttachShader(prog, v); glAttachShader(prog, f);
    glBindAttribLocation(prog, 0, "p");
    glLinkProgram(prog);
    GLint linked = 0; glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    CHECK(linked, "program links");

    float tri[] = { -1, -1,  3, -1,  -1, 3 };            // covers the whole viewport
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glViewport(0, 0, W, H);
    glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);          // red
    glUseProgram(prog);
    glDrawArrays(GL_TRIANGLES, 0, 3);                                // green over it
    glFinish();
    unsigned char px[4] = {0};
    glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("centre pixel RGBA = %d %d %d %d\n", px[0], px[1], px[2], px[3]);
    CHECK(px[0] == 0 && px[1] == 255 && px[2] == 0, "the shader drew green over the red clear");
    CHECK(glGetError() == GL_NO_ERROR, "no GL error");
    printf("%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails ? 1 : 0;
}
