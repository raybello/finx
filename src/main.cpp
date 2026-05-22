#include "SDL.h"
#ifdef __EMSCRIPTEN__
#include <SDL_opengles2.h>
#else
#include "SDL_opengl.h"
#endif
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "app.h"
#include "io/png_export.h"
#include <vector>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fetch.h>
#endif

#ifdef __EMSCRIPTEN__
// ── Clipboard bridge ────────────────────────────────────────────────────────
// SDL2's clipboard functions don't talk to the actual browser clipboard.
// We override ImGui's clipboard callbacks with EM_JS functions that use the
// real Web Clipboard API (navigator.clipboard) plus a fallback paste-event
// listener that caches text in window.__finx_clipboard synchronously.

static std::string g_clipboard_buf;

// Install a DOM paste listener so that when the user pastes (Ctrl+V or
// right-click → Paste) the text lands in window.__finx_clipboard before
// ImGui reads it on the next frame.
EM_JS(void, js_setup_clipboard, (), {
    window.__finx_clipboard = '';
    // Cache pasted text immediately (synchronous path)
    document.addEventListener('paste', function(e) {
        var text = (e.clipboardData || window.clipboardData).getData('text/plain');
        if (text) window.__finx_clipboard = text;
    }, true);
    // Also try async read on Ctrl+V so the cache stays fresh even when
    // the paste event fires before the clipboard permission is granted
    document.addEventListener('keydown', function(e) {
        if ((e.ctrlKey || e.metaKey) && e.key === 'v') {
            if (navigator.clipboard && navigator.clipboard.readText) {
                navigator.clipboard.readText().then(function(t) {
                    if (t) window.__finx_clipboard = t;
                }).catch(function() {});
            }
        }
    }, true);
});

EM_JS(char*, js_clipboard_read, (), {
    var text = window.__finx_clipboard || '';
    var len  = lengthBytesUTF8(text) + 1;
    var ptr  = _malloc(len);
    stringToUTF8(text, ptr, len);
    return ptr;
});

EM_JS(void, js_clipboard_write, (const char* ctext), {
    var text = UTF8ToString(ctext);
    window.__finx_clipboard = text;
    if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).catch(function() {
            // fallback: execCommand copy via hidden textarea
            var ta = document.createElement('textarea');
            ta.value = text;
            ta.style.cssText = 'position:fixed;opacity:0;top:0;left:0';
            document.body.appendChild(ta);
            ta.focus(); ta.select();
            try { document.execCommand('copy'); } catch(e) {}
            document.body.removeChild(ta);
        });
    } else {
        var ta = document.createElement('textarea');
        ta.value = text;
        ta.style.cssText = 'position:fixed;opacity:0;top:0;left:0';
        document.body.appendChild(ta);
        ta.focus(); ta.select();
        try { document.execCommand('copy'); } catch(e) {}
        document.body.removeChild(ta);
    }
});

static const char* imgui_clipboard_get(void*) {
    char* raw = js_clipboard_read();
    if (raw) { g_clipboard_buf = raw; free(raw); }
    return g_clipboard_buf.c_str();
}

static void imgui_clipboard_set(void*, const char* text) {
    g_clipboard_buf = text ? text : "";
    js_clipboard_write(text ? text : "");
}
#endif // __EMSCRIPTEN__ clipboard bridge

static App           g_app;
static SDL_Window*   g_window     = nullptr;
static SDL_GLContext g_gl_context;
static bool          g_running    = true;

// PNG export: path queued after modal confirm; captured one frame later so the
// modal is fully closed before the screenshot is taken.
static std::string g_png_queued_path;
static bool        g_png_capture_next = false;

static void main_loop() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL2_ProcessEvent(&e);
        if (e.type == SDL_QUIT) g_running = false;
        if (e.type == SDL_WINDOWEVENT &&
            e.window.event == SDL_WINDOWEVENT_CLOSE &&
            e.window.windowID == SDL_GetWindowID(g_window)) {
            g_running = false;
        }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    g_app.render();

    ImGui::Render();

    int w = 0, h = 0;
    SDL_GetWindowSize(g_window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // PNG capture: after render so no modal chrome appears in the screenshot.
    if (g_png_capture_next && !g_png_queued_path.empty()) {
        g_png_capture_next = false;
        int w = 0, h = 0;
        SDL_GetWindowSize(g_window, &w, &h);
        std::vector<unsigned char> pixels(static_cast<size_t>(w * h * 4));
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        png_export_pixels(g_png_queued_path, w, h, pixels.data());
        g_png_queued_path.clear();
    }
    if (!g_app.pending_png_path.empty()) {
        g_png_queued_path      = std::move(g_app.pending_png_path);
        g_app.pending_png_path.clear();
        g_png_capture_next     = true;
    }

    // Multi-viewport support (native only)
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        SDL_Window*   backup_win = SDL_GL_GetCurrentWindow();
        SDL_GLContext backup_ctx = SDL_GL_GetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        SDL_GL_MakeCurrent(backup_win, backup_ctx);
    }

    SDL_GL_SwapWindow(g_window);
}

int main(int /*argc*/, char** /*argv*/) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,         0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,  SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,         0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,  SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif

    g_window = SDL_CreateWindow(
        "finx",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_window) {
        SDL_Quit();
        return 1;
    }

    g_gl_context = SDL_GL_CreateContext(g_window);
    if (!g_gl_context) {
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_MakeCurrent(g_window, g_gl_context);
    SDL_GL_SetSwapInterval(1); // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#ifndef __EMSCRIPTEN__
    // Multi-viewport only on native (Emscripten does not support it)
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding  = 3.0f;
    style.GrabRounding   = 3.0f;
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding              = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplSDL2_InitForOpenGL(g_window, g_gl_context);
#ifdef __EMSCRIPTEN__
    ImGui_ImplOpenGL3_Init("#version 300 es");
    // Override ImGui clipboard AFTER ImGui_ImplSDL2_InitForOpenGL, which
    // installs its own SDL-based clipboard functions that don't reach the browser.
    js_setup_clipboard();
    io.GetClipboardTextFn = imgui_clipboard_get;
    io.SetClipboardTextFn = imgui_clipboard_set;
    io.ClipboardUserData  = nullptr;
#else
    ImGui_ImplOpenGL3_Init("#version 330");
#endif

    // SF Pro font — falls back to ImGui built-in if the file is absent
    if (!io.Fonts->AddFontFromFileTTF("assets/fonts/SF-Pro.ttf", 16.0f)) {
        io.Fonts->AddFontDefault();
    }

    g_app.init();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(main_loop, 0, 1);
#else
    while (g_running) {
        main_loop();
    }
#endif

    g_app.shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(g_gl_context);
    SDL_DestroyWindow(g_window);
    SDL_Quit();

    return 0;
}
