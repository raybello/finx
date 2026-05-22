#include "SDL.h"
#include "SDL_opengl.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "app.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fetch.h>
#endif

static App           g_app;
static SDL_Window*   g_window     = nullptr;
static SDL_GLContext g_gl_context;
static bool          g_running    = true;

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

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,         0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,  SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,          1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,            24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,          8);

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
    ImGui_ImplOpenGL3_Init("#version 330");

    // SF Pro font — falls back to ImGui built-in if the file is absent
    if (!io.Fonts->AddFontFromFileTTF("assets/fonts/SF-Pro.ttf", 14.0f)) {
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
