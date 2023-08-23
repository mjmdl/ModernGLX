/* gcc x11_glx.c -o x11_glx -lX11 -lGL */
#include <assert.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <GL/gl.h>
#include <GL/glext.h>

#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#define MINIMUM_GLX_MAJOR 1
#define MINIMUM_GLX_MINOR 3
#define CONTEXT_OPENGL_MAJOR 3
#define CONTEXT_OPENGL_MINOR 3

#define GL_LIST								\
    GLE(void, AttachShader, GLuint, GLuint);				\
    GLE(void, BindBuffer, GLenum, GLuint);				\
    GLE(void, BindFramebuffer, GLenum, GLuint);				\
    GLE(void, BindVertexArray, GLuint);					\
    GLE(void, BufferData, GLenum, GLsizeiptr, const GLvoid *, GLenum);	\
    GLE(void, BufferSubData, GLenum, GLintptr, GLsizeiptr, const GLvoid *); \
    GLE(GLenum, CheckFramebufferStatus, GLenum);			\
    GLE(void, ClearBufferfv, GLenum, GLint, const GLfloat *);		\
    GLE(void, CompileShader, GLuint);					\
    GLE(GLuint, CreateProgram, void);					\
    GLE(GLuint, CreateShader, GLenum);					\
    GLE(void, DeleteBuffers, GLsizei, const GLuint *);			\
    GLE(void, DeleteFramebuffers, GLsizei, const GLuint *);		\
    GLE(void, DeleteProgram, GLuint);					\
    GLE(void, DeleteShader, GLuint);					\
    GLE(void, DetachShader, GLuint, GLuint);				\
    GLE(void, DrawBuffers, GLsizei, const GLenum *);			\
    GLE(void, EnableVertexAttribArray, GLuint);				\
    GLE(void, FramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint); \
    GLE(void, GenBuffers, GLsizei, GLuint *);				\
    GLE(void, GenFramebuffers, GLsizei, GLuint *);			\
    GLE(void, GenVertexArrays, GLsizei, GLuint *);			\
    GLE(GLint, GetAttribLocation, GLuint, const GLchar *);		\
    GLE(void, GetProgramInfoLog, GLuint, GLsizei, GLsizei *, GLchar *);	\
    GLE(void, GetProgramiv, GLuint, GLenum, GLint *);			\
    GLE(void, GetShaderInfoLog, GLuint, GLsizei, GLsizei *, GLchar *);	\
    GLE(void, GetShaderiv, GLuint, GLenum, GLint *);			\
    GLE(GLint, GetUniformLocation, GLuint, const GLchar *);		\
    GLE(void, LinkProgram, GLuint);					\
    GLE(void, ShaderSource, GLuint, GLsizei count, const GLchar *const *, \
	const GLint *);							\
    GLE(void, Uniform1i, GLint, GLint);					\
    GLE(void, Uniform1f, GLint, GLfloat);				\
    GLE(void, Uniform2f, GLint, GLfloat, GLfloat);			\
    GLE(void, Uniform4f, GLint, GLfloat, GLfloat, GLfloat, GLfloat);	\
    GLE(void, UniformMatrix4fv, GLint, GLsizei, GLboolean, const GLfloat *); \
    GLE(void, UseProgram, GLuint);					\
    GLE(void, VertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, \
	const GLvoid *);

#define GLE(ret, name, ...)			\
    typedef ret name##proc(__VA_ARGS__);	\
    name##proc * gl##name;
GL_LIST
#undef GLE

#define WINDOW_TITLE "Hello, Triangle!"
#define WINDOW_WIDTH 960
#define WINDOW_HEIGHT 540
#define WINDOW_CLEAR_COLOR 0.17f, 0.17f, 0.17f, 1.0f

static int visual_attribs[] = {
    GLX_DOUBLEBUFFER, True,
    GLX_X_RENDERABLE, True,
    GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
    GLX_RENDER_TYPE, GLX_RGBA_BIT,
    GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
    GLX_SAMPLE_BUFFERS, 1,
    GLX_SAMPLES, 4,
    GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8,
    GLX_BLUE_SIZE, 8,
    GLX_ALPHA_SIZE, 8,
    GLX_DEPTH_SIZE, 24,
    GLX_STENCIL_SIZE, 8,
    None
};
static int context_attribs[] = {
    GLX_CONTEXT_MAJOR_VERSION_ARB, CONTEXT_OPENGL_MAJOR,
    GLX_CONTEXT_MINOR_VERSION_ARB, CONTEXT_OPENGL_MINOR,
    GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
    None
};

static const char *vertex_src =
    "#version 330 core\n"
    "layout (location = 0) in vec3 position;\n"
    "layout (location = 1) in vec3 color;\n"
    "out vec3 frag_color;\n"
    "void main() {\n"
    "   gl_Position = vec4(position.xyz, 1.0f);\n"
    "   frag_color = color;\n"
    "}\n";
static const char *fragment_src =
    "#version 330 core\n"
    "in vec3 frag_color;\n"
    "out vec4 final_color;\n"
    "void main() {\n"
    "   final_color = vec4(frag_color.xyz, 1.0f);\n"
    "}\n";
static const float triangle_model[] = {
    -0.7f, -0.7f, 0.0f, 1.0f, 0.0f, 0.0f,
     0.0f,  0.7f, 0.0f, 0.0f, 1.0f, 0.0f,
     0.7f, -0.7f, 0.0f, 0.0f, 0.0f, 1.0f
};

static bool app_should_quit;
static Display *xlib_display;
static XVisualInfo *xlib_visual_info;
static Colormap xlib_colormap;
static Window xlib_window;
static GLXFBConfig glx_fb_config;
static GLXContext glx_context;
static Atom wm_delete_window;

static bool open_window() {
    xlib_display = XOpenDisplay(NULL);
    if (xlib_display == NULL) {
	fprintf(stderr, "Failed to open the X display.\n");
	return false;
    }

    int glx_major, glx_minor;
    glXQueryVersion(xlib_display, &glx_major, &glx_minor);
    if ((glx_major == MINIMUM_GLX_MAJOR && glx_minor < MINIMUM_GLX_MINOR) ||
	glx_major < MINIMUM_GLX_MAJOR)
    {	
	fprintf(stderr, "GLX version %d.%d or higher is required.\n",
		MINIMUM_GLX_MAJOR, MINIMUM_GLX_MINOR);
	fprintf(stderr, "Current GLX: %d.%d.\n", glx_major, glx_minor);
	XCloseDisplay(xlib_display);
	return false;
    }

    int screen_id = DefaultScreen(xlib_display);
    Screen *screen = ScreenOfDisplay(xlib_display, screen_id);
    Window root_window = RootWindow(xlib_display, screen_id);

    int fbc_count;
    GLXFBConfig *fbc_list = glXChooseFBConfig(xlib_display, screen_id,
					      visual_attribs, &fbc_count);
    if (fbc_list == NULL || fbc_count < 1) {
	fprintf(stderr, "Failed to retrieve a framebuffer config.\n");
	XFree(fbc_list);
	XCloseDisplay(xlib_display);
	return false;
    }
    int best_fbc_index = -1, best_fbc_samples = -1;
    for (int i = 0; i < fbc_count; i++) {
	XVisualInfo *vi = glXGetVisualFromFBConfig(xlib_display, fbc_list[i]);
	if (vi == NULL) {
	    continue;
	}
	
	int buffers, samples;
	glXGetFBConfigAttrib(xlib_display, fbc_list[i], GLX_SAMPLE_BUFFERS,
			     &buffers);
	glXGetFBConfigAttrib(xlib_display, fbc_list[i], GLX_SAMPLES, &samples);

	if (best_fbc_index < 0 || buffers && samples > best_fbc_samples) {
	    best_fbc_index = i;
	    best_fbc_samples = samples;
	}

	XFree(vi);
    }
    if (best_fbc_index < 0) {
	fprintf(stderr, "Failed to retrieve a valid X framebuffer config.\n");
	XFree(fbc_list);
	XCloseDisplay(xlib_display);
	return false;
    }
    glx_fb_config = fbc_list[best_fbc_index];
    XFree(fbc_list);

    xlib_visual_info = glXGetVisualFromFBConfig(xlib_display, glx_fb_config);
    assert(xlib_visual_info != NULL && "Invalid visual info.");

    int screen_width = XWidthOfScreen(screen);
    int screen_height = XHeightOfScreen(screen);
    xlib_colormap = XCreateColormap(xlib_display, root_window,
				    xlib_visual_info->visual, AllocNone);
    XSetWindowAttributes window_attribs = {0};
    window_attribs.colormap = xlib_colormap;
    window_attribs.border_pixel = 0;
    window_attribs.event_mask = StructureNotifyMask;
    xlib_window = XCreateWindow(
	xlib_display, root_window, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
	xlib_visual_info->depth, InputOutput, xlib_visual_info->visual,
	CWBorderPixel | CWColormap | CWEventMask, &window_attribs);
    if (xlib_window == 0) {
	fprintf(stderr, "Failed to create the X window.\n");
	XFree(xlib_visual_info);
	XFreeColormap(xlib_display, xlib_colormap);
	XCloseDisplay(xlib_display);
	return false;
    }

    wm_delete_window = XInternAtom(xlib_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(xlib_display, xlib_window, &wm_delete_window, 1);

    XStoreName(xlib_display, xlib_window, WINDOW_TITLE);
    XMapWindow(xlib_display, xlib_window);
    XMoveWindow(xlib_display, xlib_window, screen_width / 4, screen_height / 4);
    XFlush(xlib_display);
    
    typedef GLXContext(*glXCreateContextAttribsARBProc)(
	Display *, GLXFBConfig, GLXContext, Bool, const int *);
    glXCreateContextAttribsARBProc glXCreateContextAttribsARB =
	(glXCreateContextAttribsARBProc)glXGetProcAddressARB(
	    (const GLubyte *) "glXCreateContextAttribsARB");
    if (glXCreateContextAttribsARB == NULL) {
	fprintf(stderr, "Failed to load glXCreateContextAttribsARB.\n");
	XFree(xlib_visual_info);
	XFreeColormap(xlib_display, xlib_colormap);
	XCloseDisplay(xlib_display);
	return false;
    }
    glx_context = glXCreateContextAttribsARB(
	xlib_display, glx_fb_config, 0, True, context_attribs);
    if (glx_context == NULL) {
	fprintf(stderr, "Failed to create a valid GLX context.\n");
	XFree(xlib_visual_info);
	XFreeColormap(xlib_display, xlib_colormap);
	XCloseDisplay(xlib_display);
	return false;
    }
    glXMakeCurrent(xlib_display, xlib_window, glx_context);
    XFlush(xlib_display);

    return true;
}

static void close_window() {
    glXMakeCurrent(xlib_display, 0, 0);
    glXDestroyContext(xlib_display, glx_context);
    XFree(xlib_visual_info);
    XUnmapWindow(xlib_display, xlib_window);
    XDestroyWindow(xlib_display, xlib_window);
    XFreeColormap(xlib_display, xlib_colormap);
    XCloseDisplay(xlib_display);
}

static bool load_modern_opengl() {
#define GLE(RET, NAME, ...)						\
    gl##NAME = (NAME##proc *)glXGetProcAddressARB("gl" #NAME);		\
    if (gl##NAME == NULL) {						\
	fprintf(stderr, "Failed to load " #NAME " from OpenGL.\n");	\
	return false;							\
    }
    GL_LIST
#undef GLE
    return true;
}

static void handle_events() {
    XEvent event;
    XNextEvent(xlib_display, &event);

    switch (event.type) {
    case ClientMessage: {
	app_should_quit = (event.xclient.data.l[0] == wm_delete_window);
    } break;

    case ConfigureNotify: {
	XConfigureEvent *config = (XConfigureEvent *)&event;
	glViewport(0, 0, config->width, config->height);
    } break;
    }
}

static unsigned compile_shader_type(const char *src, unsigned type) {
    unsigned shad = glCreateShader(type);
    glShaderSource(shad, sizeof(char), (char const *const *)&src, NULL);
    glCompileShader(shad);

    int result;
    glGetShaderiv(shad, GL_COMPILE_STATUS, &result);
    if (result == GL_FALSE) {
	int len;
	glGetShaderiv(shad, GL_INFO_LOG_LENGTH, &len);
	char *info_log = (char *)malloc(sizeof(char) * len);
	glGetShaderInfoLog(shad, len, &len, info_log);
	fprintf(stderr, "\tGLSL: %s\n", info_log);
	free(info_log);
	return 0;
    }
    
    return shad;
}

static unsigned create_shader_program(const char *vert_src,
				      const char *frag_src)
{
    unsigned vert = compile_shader_type(vert_src, GL_VERTEX_SHADER);
    if (vert == 0) {
	fprintf(stderr, "Failed to compile vertex shader.\n");
	return 0;
    }
    
    unsigned frag = compile_shader_type(frag_src, GL_FRAGMENT_SHADER);
    if (vert == 0) {
	fprintf(stderr, "Failed to compile fragment shader.\n");
	glDeleteShader(vert);
	return 0;
    }

    unsigned prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    glDetachShader(prog, vert);
    glDetachShader(prog, frag);

    glDeleteShader(vert);
    glDeleteShader(frag);
    
    int result;
    glGetProgramiv(prog, GL_LINK_STATUS, &result);
    if (result == GL_FALSE) {
	int len;
	glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
	char *info_log = (char *)malloc(sizeof(char) * len);
	glGetProgramInfoLog(prog, len, &len, info_log);
	fprintf(stderr, "GLSL: %s\n", info_log);
	free(info_log);
	glDeleteProgram(prog);
	return 0;
    }
    
    glClearColor(WINDOW_CLEAR_COLOR);
    
    return prog;
}

int main(void) {    
    if (open_window() == false) {
	fprintf(stderr, "Failed to open the window.\n");
	return EXIT_FAILURE;;
    }

    if (load_modern_opengl() == false) {
	fprintf(stderr, "Failed to load modern OpenGL.\n");
	return EXIT_FAILURE;
    }
    
    unsigned shader = create_shader_program(vertex_src, fragment_src);
    if (shader == 0) {
	fprintf(stderr, "Failed to create default shader.\n");
	close_window();
	return EXIT_FAILURE;
    }

    unsigned vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    unsigned vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle_model), triangle_model,
		 GL_STATIC_DRAW);

    unsigned position_loc = glGetAttribLocation(shader, "position");
    glEnableVertexAttribArray(position_loc);
    glVertexAttribPointer(position_loc, 3, GL_FLOAT, GL_FALSE,
			  6 * sizeof(float), (const void *)0);

    unsigned color_loc = glGetAttribLocation(shader, "color");
    glEnableVertexAttribArray(color_loc);
    glVertexAttribPointer(color_loc, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
			  (const void *)(3 * sizeof(float)));
    
    app_should_quit = false;
    while (app_should_quit == false) {
	if (XPending(xlib_display) > 0) {
	    handle_events();
	}

	glClearColor(WINDOW_CLEAR_COLOR);
	glClear(GL_COLOR_BUFFER_BIT);
	
	glUseProgram(shader);
	glBindVertexArray(vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glXSwapBuffers(xlib_display, xlib_window);
    }

    close_window();
    return EXIT_SUCCESS;
}
