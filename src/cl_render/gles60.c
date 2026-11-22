/*
 * gles60.c — Standalone 60 fps OpenGL ES 2.0 GPU cluster animation engine for VW MH2p (Tegra K1).
 *
 * Included by cluster.c (when -DUSE_DASH is defined) and dispatched via:
 *   cl_render capture=gles60 xres=1920 yres=639
 *
 * Architecture:
 *   - Uses the exact same cluster window ("58", layered via [58, 33] over stock map 33).
 *   - Loads all GLES2 entry points dynamically via eglGetProcAddress() (never links -lGLESv2 stub).
 *   - Runs a VSync-paced 60 fps render loop (eglSwapInterval=1) completely decoupled from CarPlay's
 *     20 fps H.264 stream, applying spring-damped interpolation to 10 Hz /tmp/dash.state telemetry.
 *   - Custom AI-developed cluster skins only need to modify gles60_fs_src / gles60_render_scene()
 *     for butter-smooth 60 fps GPU animations (~1.5 ms/frame on Tegra K1 Kepler 192-core GPU).
 *
 * Copyright (c) 2026 a78c7 (https://github.com/a78c7/touareg-cn-carplay-mods)
 * Licensed under PolyForm Noncommercial License 1.0.0 + LICENSE.ADDITIONAL-TERMS.md
 * FREE & OPEN SOURCE — STRICTLY PROHIBITED FOR COMMERCIAL SALE.
 */

#ifdef USE_DASH

typedef void (*PFN_Uniform1f)(GLint, GLfloat);
typedef void (*PFN_Uniform2f)(GLint, GLfloat, GLfloat);
typedef void (*PFN_Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFN_Enable)(GLenum);
typedef void (*PFN_Disable)(GLenum);
typedef void (*PFN_BlendFunc)(GLenum, GLenum);

static volatile sig_atomic_t g_gles60_stop = 0;
static void gles60_on_sig(int sig) { (void)sig; g_gles60_stop = 1; }

static const char *gles60_vs_src =
    "attribute vec2 pos;\n"
    "attribute vec2 uv;\n"
    "varying vec2 v_uv;\n"
    "void main() { gl_Position = vec4(pos, 0.0, 1.0); v_uv = uv; }\n";

/* Pure GPU 60 fps procedural sport-cockpit background shader:
 * Draws smooth anti-aliased horizon grid, dual symmetric RPM / power glow arcs,
 * redline pulse and live G-force vector ring in ~1 ms on Tegra K1. */
static const char *gles60_bg_fs_src =
    "precision highp float;\n"
    "varying vec2 v_uv;\n"
    "uniform vec2  u_res;\n"
    "uniform float u_time;\n"
    "uniform float u_rpm;     /* 0..1 (0..7000 rpm) */\n"
    "uniform float u_power;   /* 0..1 (0..265 kW)   */\n"
    "uniform float u_speed;   /* 0..1 (0..240 km/h) */\n"
    "uniform vec2  u_g;       /* (lat_g, long_g)    */\n"
    "float sdBox(vec2 p, vec2 b) {\n"
    "  vec2 d = abs(p) - b;\n"
    "  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);\n"
    "}\n"
    "void main() {\n"
    "  vec2 px = v_uv * u_res;\n"
    "  vec2 ctr = vec2(u_res.x * 0.5, u_res.y * 0.56);\n"
    "  vec2 dp = (px - ctr) / u_res.y;\n"
    "  /* Deep dark cockpit slate background with subtle radial vignette */\n"
    "  float rad = length(dp * vec2(0.55, 1.0));\n"
    "  vec3 col = mix(vec3(0.055, 0.082, 0.125), vec3(0.015, 0.022, 0.036), clamp(rad * 1.35, 0.0, 1.0));\n"
    "  /* Perspective motion horizon lines (speed-reactive) */\n"
    "  if (v_uv.y > 0.55) {\n"
    "    float py = (v_uv.y - 0.55) / 0.45;\n"
    "    float z = 1.0 / (py + 0.08);\n"
    "    float grid_z = abs(fract(z * 0.45 - u_time * (0.25 + u_speed * 2.8)) - 0.5);\n"
    "    float grid_x = abs(fract((v_uv.x - 0.5) * z * 1.4) - 0.5);\n"
    "    float ln = (smoothstep(0.47, 0.50, 0.5 - grid_z) + smoothstep(0.47, 0.50, 0.5 - grid_x)) * py * 0.16;\n"
    "    col += vec3(0.12, 0.65, 0.88) * ln;\n"
    "  }\n"
    "  /* Top symmetric dual-wing RPM / power glow bar (x: 340..1580, y: 168..184) */\n"
    "  float bar_y = 176.0;\n"
    "  float half_w = 560.0;\n"
    "  float dx = abs(px.x - u_res.x * 0.5);\n"
    "  float dy = abs(px.y - bar_y);\n"
    "  if (dx <= half_w && dy <= 16.0) {\n"
    "    float t = dx / half_w;\n"
    "    float seg = step(0.18, fract(t * 28.0));\n"
    "    float track = smoothstep(7.0, 5.0, dy) * seg;\n"
    "    col = mix(col, vec3(0.11, 0.15, 0.22), track * 0.85);\n"
    "    float fill = step(t, u_rpm);\n"
    "    vec3 bar_c = t > 0.82 ? vec3(0.98, 0.22, 0.28) : (t > 0.62 ? vec3(0.98, 0.68, 0.14) : vec3(0.10, 0.85, 0.88));\n"
    "    float pulse = (u_rpm > 0.82) ? (0.85 + 0.15 * sin(u_time * 18.0)) : 1.0;\n"
    "    col += bar_c * track * fill * pulse * 0.95;\n"
    "    float glow = exp(-dy * 0.24) * fill * 0.22;\n"
    "    col += bar_c * glow;\n"
    "  }\n"
    "  /* Central smooth G-force / chassis dynamics ring */\n"
    "  vec2 gc = vec2(u_res.x * 0.5, 392.0);\n"
    "  vec2 gp = px - gc;\n"
    "  float gr = length(gp);\n"
    "  float ring1 = smoothstep(2.0, 0.5, abs(gr - 110.0));\n"
    "  float ring2 = smoothstep(1.6, 0.4, abs(gr - 55.0));\n"
    "  float cross_h = smoothstep(1.5, 0.3, abs(gp.y)) * step(abs(gp.x), 110.0);\n"
    "  float cross_v = smoothstep(1.5, 0.3, abs(gp.x)) * step(abs(gp.y), 110.0);\n"
    "  col += vec3(0.18, 0.52, 0.68) * (ring1 * 0.45 + ring2 * 0.22 + (cross_h + cross_v) * 0.14);\n"
    "  vec2 gdot = clamp(u_g, vec2(-1.0), vec2(1.0)) * 96.0;\n"
    "  float gd = length(gp - vec2(gdot.x, -gdot.y));\n"
    "  col += vec3(0.14, 0.92, 0.95) * smoothstep(11.0, 6.0, gd);\n"
    "  col += vec3(0.10, 0.72, 0.92) * exp(-gd * 0.08) * 0.55;\n"
    "  /* Subtle power accent bar near bottom center */\n"
    "  float pb = sdBox(px - vec2(u_res.x * 0.5, 558.0), vec2(260.0, 4.0));\n"
    "  if (pb < 8.0) {\n"
    "    float pt = clamp((px.x - (u_res.x * 0.5 - 260.0)) / 520.0, 0.0, 1.0);\n"
    "    float inside = smoothstep(1.0, -1.0, pb);\n"
    "    col = mix(col, vec3(0.10, 0.14, 0.20), inside * 0.8);\n"
    "    col += vec3(0.20, 0.86, 0.62) * inside * step(pt, u_power) * 0.9;\n"
    "  }\n"
    "  gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

/* Premultiplied/straight alpha RGBA texture overlay shader for crisp dash.c / navbox.c cards */
static const char *gles60_tex_fs_src =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "void main() {\n"
    "  vec4 c = texture2D(u_tex, v_uv);\n"
    "  /* dash.c writes BGRA bytes on little-endian QNX; swizzle b/r for GL_RGBA */\n"
    "  gl_FragColor = vec4(c.b, c.g, c.r, c.a);\n"
    "}\n";

static GLuint gles60_build_prog(const char *vs_s, const char *fs_s) {
    GLuint vs = gl_CreateShader(GL_VERTEX_SHADER);
    gl_ShaderSource(vs, 1, &vs_s, NULL);
    gl_CompileShader(vs);
    GLuint fs = gl_CreateShader(GL_FRAGMENT_SHADER);
    gl_ShaderSource(fs, 1, &fs_s, NULL);
    gl_CompileShader(fs);
    GLuint prog = gl_CreateProgram();
    gl_AttachShader(prog, vs);
    gl_AttachShader(prog, fs);
    gl_LinkProgram(prog);
    return prog;
}

static void gles60_quad_px(float v[24], float x, float y, float w, float h, float win_w, float win_h) {
    float x0 = (x / win_w) * 2.0f - 1.0f;
    float x1 = ((x + w) / win_w) * 2.0f - 1.0f;
    float y0 = 1.0f - (y / win_h) * 2.0f;
    float y1 = 1.0f - ((y + h) / win_h) * 2.0f;
    float q[24] = {
        x0, y0, 0.0f, 0.0f,
        x1, y0, 1.0f, 0.0f,
        x0, y1, 0.0f, 1.0f,
        x1, y0, 1.0f, 0.0f,
        x1, y1, 1.0f, 1.0f,
        x0, y1, 0.0f, 1.0f
    };
    memcpy(v, q, sizeof(q));
}

static void *gles60_sym(const char *name) {
    void *p = (void *)eglGetProcAddress(name);
    if (!p) p = dlsym(RTLD_DEFAULT, name);
    return p;
}

static int run_gles60_mode(int argc, char **argv) {
    (void)argc; (void)argv;
    signal(SIGTERM, gles60_on_sig);
    signal(SIGINT,  gles60_on_sig);

    screen_context_t ctx = NULL;
    if (screen_create_context(&ctx, SCREEN_APPLICATION_CONTEXT) != 0) {
        LOG("gles60: screen_create_context failed\n");
        return 10;
    }

    screen_window_t win = NULL;
    if (screen_create_window(&win, ctx) != 0) {
        LOG("gles60: screen_create_window failed\n");
        return 11;
    }
    const char *did = cl_disp_id();
    screen_set_window_property_cv(win, SCREEN_PROPERTY_ID_STRING, (int)strlen(did), did);
    int sz[2] = { DISP_W, DISP_H };
    int fmt = SCREEN_FORMAT_RGBX8888;
    int usage = SCREEN_USAGE_OPENGL_ES2;
    int transp = SCREEN_TRANSPARENCY_NONE;
    int vis = 1;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_SIZE, sz);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_BUFFER_SIZE, sz);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_FORMAT, &fmt);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_USAGE, &usage);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_TRANSPARENCY, &transp);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_VISIBLE, &vis);
    screen_flush_context(ctx, 0);
    if (screen_create_window_buffers(win, 2) != 0) {
        LOG("gles60: screen_create_window_buffers failed\n");
        return 12;
    }

    EGLDisplay edpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (edpy == EGL_NO_DISPLAY || !eglInitialize(edpy, NULL, NULL)) return 13;
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLConfig ecfg; EGLint ncfg = 0;
    if (!eglChooseConfig(edpy, cfg_attr, &ecfg, 1, &ncfg) || ncfg < 1) return 14;
    EGLSurface esurf = eglCreateWindowSurface(edpy, ecfg, (EGLNativeWindowType)win, NULL);
    if (esurf == EGL_NO_SURFACE) return 15;
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ectx = eglCreateContext(edpy, ecfg, EGL_NO_CONTEXT, ctx_attr);
    if (ectx == EGL_NO_CONTEXT) return 16;
    if (!eglMakeCurrent(edpy, esurf, esurf, ectx)) return 17;
    eglSwapInterval(edpy, 1); /* lock to cluster VSync (60 fps) */

    while (eglGetError() != EGL_SUCCESS) {}
    load_gl();
    PFN_Uniform1f gl_Uniform1f = (PFN_Uniform1f)gles60_sym("glUniform1f");
    PFN_Uniform2f gl_Uniform2f = (PFN_Uniform2f)gles60_sym("glUniform2f");
    PFN_Enable    gl_Enable    = (PFN_Enable)gles60_sym("glEnable");
    PFN_Disable   gl_Disable   = (PFN_Disable)gles60_sym("glDisable");
    PFN_BlendFunc gl_BlendFunc = (PFN_BlendFunc)gles60_sym("glBlendFunc");
    if (!gl_CreateShader || !gl_DrawArrays || !gl_Uniform1f || !gl_Uniform2f) {
        LOG("gles60: missing GLES2 symbols\n");
        return 18;
    }

    GLuint bg_prog  = gles60_build_prog(gles60_vs_src, gles60_bg_fs_src);
    GLuint tex_prog = gles60_build_prog(gles60_vs_src, gles60_tex_fs_src);

    GLint bg_pos = gl_GetAttribLocation(bg_prog, "pos");
    GLint bg_uv  = gl_GetAttribLocation(bg_prog, "uv");
    GLint u_res  = gl_GetUniformLocation(bg_prog, "u_res");
    GLint u_time = gl_GetUniformLocation(bg_prog, "u_time");
    GLint u_rpm  = gl_GetUniformLocation(bg_prog, "u_rpm");
    GLint u_pwr  = gl_GetUniformLocation(bg_prog, "u_power");
    GLint u_spd  = gl_GetUniformLocation(bg_prog, "u_speed");
    GLint u_g    = gl_GetUniformLocation(bg_prog, "u_g");

    GLint tx_pos = gl_GetAttribLocation(tex_prog, "pos");
    GLint tx_uv  = gl_GetAttribLocation(tex_prog, "uv");
    GLint u_tex  = gl_GetUniformLocation(tex_prog, "u_tex");

    /* Load dash.conf layout into pure CPU RGBA buffers (g_dash_pxbuf[0..1], zero QNX pixmap dependency) */
    {
        static const char *cl[] = { "/tmp/cl.conf", "/mnt/app/eso/bin/apps/cluster/cl.conf" };
        static const char *dc[] = { "/tmp/dash.conf", "/mnt/app/eso/bin/apps/cluster/dash.conf" };
        dash_read_conf_file(cl);
        dash_read_conf_file(dc);
        dash_layout_all();
    }
    int box_en[2] = {0, 0}, box_x[2] = {0, 0}, box_y[2] = {0, 0}, box_w[2] = {0, 0}, box_h[2] = {0, 0};
    GLuint card_tex[2] = {0, 0};
    gl_GenTextures(2, card_tex);
    for (int b = 0; b < 2; b++) {
        dash_use(b);
        if (g_dash_conf.enabled && g_nrows > 0 && g_dash_w > 0 && g_dash_h > 0) {
            int x = g_dash_conf.x, y = g_dash_conf.y < 0 ? (DISP_H - g_dash_h) / 2 : g_dash_conf.y;
            if (x < 0) x = 0; if (x + g_dash_w > DISP_W) x = DISP_W - g_dash_w;
            if (y < 0) y = 0; if (y + g_dash_h > DISP_H) y = DISP_H - g_dash_h;
            box_en[b] = 1; box_x[b] = x; box_y[b] = y; box_w[b] = g_dash_w; box_h[b] = g_dash_h;
            gl_BindTexture(GL_TEXTURE_2D, card_tex[b]);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, box_w[b], box_h[b], 0,
                          GL_RGBA, GL_UNSIGNED_BYTE, g_dash_pxbuf[b]);
        }
    }
    dash_use(0);

    float full_q[24];
    gles60_quad_px(full_q, 0.0f, 0.0f, (float)DISP_W, (float)DISP_H, (float)DISP_W, (float)DISP_H);

    /* Notify clusterd that window 58 is ready */
    {
        int rfd = open("/tmp/cl_render_ready", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (rfd >= 0) {
            char rb[24];
            int rl = snprintf(rb, sizeof(rb), "%d\n", (int)getpid());
            (void)write(rfd, rb, (size_t)rl);
            close(rfd);
        }
    }

    LOG("gles60: started %dx%d on displayable %s (60 fps VSync)\n", DISP_W, DISP_H, did);

    uint64_t t_start = now_us();
    uint64_t t_prev  = t_start;
    uint64_t last_poll_ms = 0;
    float s_rpm = 0.0f, s_pwr = 0.0f, s_spd = 0.0f, s_gx = 0.0f, s_gy = 0.0f;
    float t_rpm = 0.0f, t_pwr = 0.0f, t_spd = 0.0f, t_gx = 0.0f, t_gy = 0.0f;
    dash_vals_t cur_v;
    memset(&cur_v, 0, sizeof(cur_v));

    while (!g_gles60_stop) {
        /* Pause GPU rendering if user manually switched cluster back to native map via OK double-tap */
        if (access("/tmp/vwcn_user_native", F_OK) == 0) {
            usleep(250000);
            t_prev = now_us();
            continue;
        }
        uint64_t t_now = now_us();
        float dt = (float)(t_now - t_prev) * 1e-6f;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.1f)   dt = 0.1f;
        t_prev = t_now;
        uint64_t now_ms = t_now / 1000ULL;

        /* Poll /tmp/dash.state at ~30 Hz (Dash.java writes at 10 Hz) */
        if (now_ms - last_poll_ms >= 33ULL) {
            last_poll_ms = now_ms;
            int updated = dash_poll(now_ms);
            FILE *sf = fopen(DASH_STATE, "r");
            if (sf) {
                char l[2048];
                if (fgets(l, sizeof(l), sf) && strchr(l, '\n')) {
                    dash_parse(l, &cur_v);
                    if (cur_v.rpm >= 0) t_rpm = (float)cur_v.rpm / 7000.0f;
                    if (!DASH_NAN(cur_v.p)) t_pwr = cur_v.p / 265.0f;
                    if (!DASH_NAN(cur_v.spd)) t_spd = cur_v.spd / 240.0f;
                    if (!DASH_NAN(cur_v.gx)) t_gx = cur_v.gx;
                    if (!DASH_NAN(cur_v.gy)) t_gy = g_dash_conf.lat_flip ? -cur_v.gy : cur_v.gy;
                }
                fclose(sf);
            }
            if (updated && g_dash_have) {
                for (int b = 0; b < 2; b++) {
                    if (box_en[b]) {
                        gl_BindTexture(GL_TEXTURE_2D, card_tex[b]);
                        gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, box_w[b], box_h[b],
                                         GL_RGBA, GL_UNSIGNED_BYTE, g_dash_pxbuf[b]);
                    }
                }
            }
        }

        /* Exponential damping filter: smoothly interpolates 10 Hz telemetry into 60 fps motion */
        float alpha = 1.0f - expf(-12.0f * dt);
        s_rpm += (t_rpm - s_rpm) * alpha;
        s_pwr += (t_pwr - s_pwr) * alpha;
        s_spd += (t_spd - s_spd) * alpha;
        s_gx  += (t_gx  - s_gx)  * alpha;
        s_gy  += (t_gy  - s_gy)  * alpha;

        gl_Viewport(0, 0, DISP_W, DISP_H);
        if (gl_Disable) gl_Disable(GL_BLEND);

        /* Pass 1: 60 fps GPU procedural background & dynamics */
        gl_UseProgram(bg_prog);
        gl_Uniform2f(u_res, (float)DISP_W, (float)DISP_H);
        gl_Uniform1f(u_time, (float)(t_now - t_start) * 1e-6f);
        gl_Uniform1f(u_rpm,  s_rpm < 0.0f ? 0.0f : (s_rpm > 1.0f ? 1.0f : s_rpm));
        gl_Uniform1f(u_pwr,  s_pwr < 0.0f ? 0.0f : (s_pwr > 1.0f ? 1.0f : s_pwr));
        gl_Uniform1f(u_spd,  s_spd < 0.0f ? 0.0f : (s_spd > 1.0f ? 1.0f : s_spd));
        gl_Uniform2f(u_g,    s_gy, s_gx);
        gl_EnableVertexAttribArray(bg_pos);
        gl_EnableVertexAttribArray(bg_uv);
        gl_VertexAttribPointer(bg_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), full_q);
        gl_VertexAttribPointer(bg_uv,  2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), full_q + 2);
        gl_DrawArrays(GL_TRIANGLES, 0, 6);

        /* Pass 2: crisp telemetry side cards via GPU alpha blending */
        if (g_dash_have && gl_Enable && gl_BlendFunc) {
            gl_Enable(GL_BLEND);
            gl_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            gl_UseProgram(tex_prog);
            gl_ActiveTexture(GL_TEXTURE0);
            gl_Uniform1i(u_tex, 0);
            gl_EnableVertexAttribArray(tx_pos);
            gl_EnableVertexAttribArray(tx_uv);
            for (int b = 0; b < 2; b++) {
                if (!box_en[b]) continue;
                float q[24];
                gles60_quad_px(q, (float)box_x[b], (float)box_y[b],
                               (float)box_w[b], (float)box_h[b],
                               (float)DISP_W, (float)DISP_H);
                gl_BindTexture(GL_TEXTURE_2D, card_tex[b]);
                gl_VertexAttribPointer(tx_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), q);
                gl_VertexAttribPointer(tx_uv,  2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), q + 2);
                gl_DrawArrays(GL_TRIANGLES, 0, 6);
            }
        }

        eglSwapBuffers(edpy, esurf);
    }

    LOG("gles60: exiting cleanly\n");
    eglMakeCurrent(edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(edpy, ectx);
    eglDestroySurface(edpy, esurf);
    eglTerminate(edpy);
    screen_destroy_window(win);
    screen_destroy_context(ctx);
    return 0;
}

#endif /* USE_DASH */
