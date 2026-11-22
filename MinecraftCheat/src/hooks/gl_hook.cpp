// src/hooks/gl_hook.cpp
#include "gl_hook.h"
#include "../gui/gui_manager.h"
#include "../gui/clickgui.h"
#include "../overlay/stream_overlay.h"
#include "../modules/module_viewmodel.h"
#include "../modules/module_aspect.h"
#include "../modules/module_titles.h"
#include "../jvm/jvm_bridge.h"
#include <MinHook.h>
#include <cmath>

bool g_optViewModel = false;
float g_vmRightX = 0.0f, g_vmRightY = 0.0f, g_vmRightZ = 0.0f, g_vmRightScale = 1.0f;
float g_vmLeftX = 0.0f, g_vmLeftY = 0.0f, g_vmLeftZ = 0.0f, g_vmLeftScale = 1.0f;

bool g_optAspect    = false;
float g_aspectRatioVal = 1.0f;

bool g_optTitles    = false;
float g_titlesScaleVal = 1.0f;

static int64_t s_perfFreq          = 0;
static int64_t s_lastFPSTime       = 0;
static int     s_framesInInterval  = 0;
static int     s_exactFPS          = 0;

void GLHook::RecordSwapFrame() {
    if (s_perfFreq == 0) {
        QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&s_perfFreq));
    }
    int64_t now = 0;
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&now));
    s_framesInInterval++;

    if (s_lastFPSTime == 0) {
        s_lastFPSTime = now;
        return;
    }

    double elapsed = static_cast<double>(now - s_lastFPSTime) / static_cast<double>(s_perfFreq);
    if (elapsed >= 0.5) { // обновление 2 раза в секунду
        s_exactFPS = static_cast<int>(std::round(static_cast<double>(s_framesInInterval) / elapsed));
        s_framesInInterval = 0;
        s_lastFPSTime = now;
    }
}

int GLHook::GetExactFPS() {
    return s_exactFPS > 0 ? s_exactFPS : 0;
}

static void* s_targetTranslate    = nullptr;
static void* s_targetTranslated   = nullptr;
static void* s_targetNglTranslate = nullptr;

static void* s_targetMultMatrix   = nullptr;
static void* s_targetLoadMatrix   = nullptr;
static void* s_targetNglMult      = nullptr;
static void* s_targetNglLoad      = nullptr;

static void* s_targetScalef       = nullptr;
static void* s_targetScaled       = nullptr;
static void* s_targetNglScale     = nullptr;

bool g_hideCrosshair = false;
static bool s_crosshairMasked = false;

static void* s_targetNglBlendFuncSeparate = nullptr;
static void* s_targetNglBlendFunc         = nullptr;
static void* s_targetGlBlendFunc          = nullptr;

static fnNglBlendFuncSeparate OriginalNglBlendFuncSeparate = nullptr;
static fnNglBlendFunc         OriginalNglBlendFunc         = nullptr;
static fnGlBlendFunc          OriginalGlBlendFunc          = nullptr;

bool GLHook::Initialize() {
    MH_Initialize();

    HMODULE ogl = GetModuleHandleA("opengl32.dll");
    if (!ogl) return false;

    // 1. Hook opengl32.dll!wglSwapBuffers
    void* targetSwap = reinterpret_cast<void*>(GetProcAddress(ogl, "wglSwapBuffers"));
    if (targetSwap) {
        if (MH_CreateHook(targetSwap,
                          reinterpret_cast<void*>(&Hooked_wglSwapBuffers),
                          reinterpret_cast<void**>(&Original)) == MH_OK) {
            MH_EnableHook(targetSwap);
        }
    }

    // 1b. Hook gdi32.dll!SwapBuffers (LWJGL 2 в WindowsDisplay.nUpdate)
    HMODULE gdi = GetModuleHandleA("gdi32.dll");
    if (!gdi) gdi = LoadLibraryA("gdi32.dll");
    if (gdi) {
        void* targetGdiSwap = reinterpret_cast<void*>(GetProcAddress(gdi, "SwapBuffers"));
        if (targetGdiSwap) {
            if (MH_CreateHook(targetGdiSwap,
                              reinterpret_cast<void*>(&Hooked_gdiSwapBuffers),
                              reinterpret_cast<void**>(&OriginalGdiSwap)) == MH_OK) {
                MH_EnableHook(targetGdiSwap);
            }
        }
    }

    // 2. Хуки для ViewModel (создаем, но НЕ включаем по умолчанию для сохранения 2500 FPS)
    s_targetTranslate = reinterpret_cast<void*>(GetProcAddress(ogl, "glTranslatef"));
    if (s_targetTranslate) {
        MH_CreateHook(s_targetTranslate,
                      reinterpret_cast<void*>(&Hooked_glTranslatef),
                      reinterpret_cast<void**>(&OriginalTranslatef));
    }

    s_targetTranslated = reinterpret_cast<void*>(GetProcAddress(ogl, "glTranslated"));
    if (s_targetTranslated) {
        MH_CreateHook(s_targetTranslated,
                      reinterpret_cast<void*>(&Hooked_glTranslated),
                      reinterpret_cast<void**>(&OriginalTranslated));
    }

    // 3. Хуки для AspectRatio (создаем, но НЕ включаем по умолчанию)
    s_targetMultMatrix = reinterpret_cast<void*>(GetProcAddress(ogl, "glMultMatrixf"));
    if (s_targetMultMatrix) {
        MH_CreateHook(s_targetMultMatrix,
                      reinterpret_cast<void*>(&Hooked_glMultMatrixf),
                      reinterpret_cast<void**>(&OriginalMultMatrixf));
    }

    s_targetLoadMatrix = reinterpret_cast<void*>(GetProcAddress(ogl, "glLoadMatrixf"));
    if (s_targetLoadMatrix) {
        MH_CreateHook(s_targetLoadMatrix,
                      reinterpret_cast<void*>(&Hooked_glLoadMatrixf),
                      reinterpret_cast<void**>(&OriginalLoadMatrixf));
    }

    // 4. Хуки для масштабирования титров Titles (создаем, но НЕ включаем по умолчанию)
    s_targetScalef = reinterpret_cast<void*>(GetProcAddress(ogl, "glScalef"));
    if (s_targetScalef) {
        MH_CreateHook(s_targetScalef,
                      reinterpret_cast<void*>(&Hooked_glScalef),
                      reinterpret_cast<void**>(&OriginalScalef));
    }

    s_targetScaled = reinterpret_cast<void*>(GetProcAddress(ogl, "glScaled"));
    if (s_targetScaled) {
        MH_CreateHook(s_targetScaled,
                      reinterpret_cast<void*>(&Hooked_glScaled),
                      reinterpret_cast<void**>(&OriginalScaled));
    }

    // 5. Hooks из lwjgl64.dll / lwjgl.dll (LWJGL 2 JNI перехваты)
    HMODULE lwjgl = GetModuleHandleA("lwjgl64.dll");
    if (!lwjgl) lwjgl = GetModuleHandleA("lwjgl.dll");
    if (lwjgl) {
        s_targetNglTranslate = reinterpret_cast<void*>(GetProcAddress(lwjgl, "Java_org_lwjgl_opengl_GL11_nglTranslatef"));
        if (s_targetNglTranslate) {
            MH_CreateHook(s_targetNglTranslate,
                          reinterpret_cast<void*>(&Hooked_nglTranslatef),
                          reinterpret_cast<void**>(&OriginalNtranslate));
        }

        s_targetNglScale = reinterpret_cast<void*>(GetProcAddress(lwjgl, "Java_org_lwjgl_opengl_GL11_nglScalef"));
        if (s_targetNglScale) {
            MH_CreateHook(s_targetNglScale,
                          reinterpret_cast<void*>(&Hooked_nglScalef),
                          reinterpret_cast<void**>(&OriginalNscale));
        }

        s_targetNglMult = reinterpret_cast<void*>(GetProcAddress(lwjgl, "Java_org_lwjgl_opengl_GL11_nglMultMatrixf"));
        if (s_targetNglMult) {
            MH_CreateHook(s_targetNglMult,
                          reinterpret_cast<void*>(&Hooked_nglMultMatrixf),
                          reinterpret_cast<void**>(&OriginalNmultMatrix));
        }

        s_targetNglLoad = reinterpret_cast<void*>(GetProcAddress(lwjgl, "Java_org_lwjgl_opengl_GL11_nglLoadMatrixf"));
        if (s_targetNglLoad) {
            MH_CreateHook(s_targetNglLoad,
                          reinterpret_cast<void*>(&Hooked_nglLoadMatrixf),
                          reinterpret_cast<void**>(&OriginalNloadMatrix));
        }

        // 6. Hooks для скрытия серверного прицела при активном Crosshair
        s_targetNglBlendFuncSeparate = reinterpret_cast<void*>(GetProcAddress(lwjgl, "Java_org_lwjgl_opengl_GL14_nglBlendFuncSeparate"));
        if (s_targetNglBlendFuncSeparate) {
            MH_CreateHook(s_targetNglBlendFuncSeparate,
                          reinterpret_cast<void*>(&Hooked_nglBlendFuncSeparate),
                          reinterpret_cast<void**>(&OriginalNglBlendFuncSeparate));
            MH_EnableHook(s_targetNglBlendFuncSeparate);
        }

        s_targetNglBlendFunc = reinterpret_cast<void*>(GetProcAddress(lwjgl, "Java_org_lwjgl_opengl_GL11_nglBlendFunc"));
        if (s_targetNglBlendFunc) {
            MH_CreateHook(s_targetNglBlendFunc,
                          reinterpret_cast<void*>(&Hooked_nglBlendFunc),
                          reinterpret_cast<void**>(&OriginalNglBlendFunc));
            MH_EnableHook(s_targetNglBlendFunc);
        }
    }

    s_targetGlBlendFunc = reinterpret_cast<void*>(GetProcAddress(ogl, "glBlendFunc"));
    if (s_targetGlBlendFunc) {
        MH_CreateHook(s_targetGlBlendFunc,
                      reinterpret_cast<void*>(&Hooked_glBlendFunc),
                      reinterpret_cast<void**>(&OriginalGlBlendFunc));
        MH_EnableHook(s_targetGlBlendFunc);
    }

    Ready = true;
    m_unloading = false;
    return true;
}

static bool s_vmActive = false;
void GLHook::SetViewModelHookActive(bool active) {
    if (s_vmActive == active) return;
    s_vmActive = active;
    if (active) {
        if (s_targetTranslate)    MH_EnableHook(s_targetTranslate);
        if (s_targetTranslated)   MH_EnableHook(s_targetTranslated);
        if (s_targetNglTranslate) MH_EnableHook(s_targetNglTranslate);
    } else {
        if (s_targetTranslate)    MH_DisableHook(s_targetTranslate);
        if (s_targetTranslated)   MH_DisableHook(s_targetTranslated);
        if (s_targetNglTranslate) MH_DisableHook(s_targetNglTranslate);
    }
}

static bool s_aspectActive = false;
void GLHook::SetAspectRatioHookActive(bool active) {
    if (s_aspectActive == active) return;
    s_aspectActive = active;
    if (active) {
        if (s_targetMultMatrix) MH_EnableHook(s_targetMultMatrix);
        if (s_targetLoadMatrix) MH_EnableHook(s_targetLoadMatrix);
        if (s_targetNglMult)    MH_EnableHook(s_targetNglMult);
        if (s_targetNglLoad)    MH_EnableHook(s_targetNglLoad);
    } else {
        if (s_targetMultMatrix) MH_DisableHook(s_targetMultMatrix);
        if (s_targetLoadMatrix) MH_DisableHook(s_targetLoadMatrix);
        if (s_targetNglMult)    MH_DisableHook(s_targetNglMult);
        if (s_targetNglLoad)    MH_DisableHook(s_targetNglLoad);
    }
}

static bool s_titlesActive = false;
void GLHook::SetTitlesHookActive(bool active) {
    if (s_titlesActive == active) return;
    s_titlesActive = active;
    if (active) {
        if (s_targetScalef)   MH_EnableHook(s_targetScalef);
        if (s_targetScaled)   MH_EnableHook(s_targetScaled);
        if (s_targetNglScale) MH_EnableHook(s_targetNglScale);
    } else {
        if (s_targetScalef)   MH_DisableHook(s_targetScalef);
        if (s_targetScaled)   MH_DisableHook(s_targetScaled);
        if (s_targetNglScale) MH_DisableHook(s_targetNglScale);
    }
}

void GLHook::Shutdown() {
    m_unloading = true;
    Sleep(150); // Даём потоку рендера выйти из хука
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    Ready = false;
}

// ── Анимация чата: плавное появление при открытии ────────────────────────────
static float s_chatAnimProgress = 1.0f; // 1.0 = на месте, 0.0 = скрыт/выезжает

static void UpdateChatAnimation() {
}

static thread_local bool s_inSwapBuffersReentrancy = false;
static HWND s_cachedHwnd = nullptr;

// ── Хуки SwapBuffers ─────────────────────────────────────────────────────────
BOOL WINAPI Hooked_wglSwapBuffers(HDC hdc) {
    if (s_crosshairMasked) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        s_crosshairMasked = false;
    }

    if (GLHook::Get().IsUnloading()) {
        return GLHook::Get().Original ? GLHook::Get().Original(hdc) : TRUE;
    }

    if (s_inSwapBuffersReentrancy) {
        return GLHook::Get().Original ? GLHook::Get().Original(hdc) : TRUE;
    }
    s_inSwapBuffersReentrancy = true;

    GLHook::RecordSwapFrame();

    auto* clickGui = GUIManager::Get().GetClickGUI();
    bool screenProtect = clickGui && clickGui->IsScreenProtectEnabled();

    if (!s_cachedHwnd) s_cachedHwnd = WindowFromDC(hdc);
    if (s_cachedHwnd) {
        static bool s_lastAffinity = false;
        if (s_lastAffinity != screenProtect) {
            s_lastAffinity = screenProtect;
            // Окно Minecraft НИКОГДА не маркируется WDA_EXCLUDEFROMCAPTURE, чтобы игра не исчезала со стрима!
            SetWindowDisplayAffinity(s_cachedHwnd, WDA_NONE);
            if (screenProtect) {
                StreamOverlay::Get().Start(s_cachedHwnd, GetModuleHandleA("MyrLegit.dll"));
            } else {
                StreamOverlay::Get().Stop();
            }
        }
    }

    // Отрисовка ImGui интерфейса и оверлеев
    // При включенном ScreenProtect Render() пропускает вывод во фреймбуфер игры,
    // а независимый D3D9 оверлей StreamOverlay рисует поверх с флагом 0x11 (WDA_EXCLUDEFROMCAPTURE)
    GUIManager::Get().Render(hdc);
    BOOL res = GLHook::Get().Original ? GLHook::Get().Original(hdc) : TRUE;

    s_inSwapBuffersReentrancy = false;
    return res;
}

BOOL WINAPI Hooked_gdiSwapBuffers(HDC hdc) {
    if (s_crosshairMasked) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        s_crosshairMasked = false;
    }

    if (GLHook::Get().IsUnloading()) {
        return GLHook::Get().OriginalGdiSwap ? GLHook::Get().OriginalGdiSwap(hdc) : TRUE;
    }

    if (s_inSwapBuffersReentrancy) {
        return GLHook::Get().OriginalGdiSwap ? GLHook::Get().OriginalGdiSwap(hdc) : TRUE;
    }
    s_inSwapBuffersReentrancy = true;

    GLHook::RecordSwapFrame();

    auto* clickGui = GUIManager::Get().GetClickGUI();
    bool screenProtect = clickGui && clickGui->IsScreenProtectEnabled();

    if (!s_cachedHwnd) s_cachedHwnd = WindowFromDC(hdc);
    if (s_cachedHwnd) {
        static bool s_lastAffinityGdi = false;
        if (s_lastAffinityGdi != screenProtect) {
            s_lastAffinityGdi = screenProtect;
            SetWindowDisplayAffinity(s_cachedHwnd, WDA_NONE);
            if (screenProtect) {
                StreamOverlay::Get().Start(s_cachedHwnd, GetModuleHandleA("MyrLegit.dll"));
            } else {
                StreamOverlay::Get().Stop();
            }
        }
    }

    GUIManager::Get().Render(hdc);
    BOOL res = GLHook::Get().OriginalGdiSwap ? GLHook::Get().OriginalGdiSwap(hdc) : TRUE;

    s_inSwapBuffersReentrancy = false;
    return res;
}

// ── Обработчик glTranslated для ViewModel ────────────────────────────────────
void APIENTRY Hooked_glTranslated(GLdouble x, GLdouble y, GLdouble z) {
    if (GLHook::Get().IsUnloading() || !g_optViewModel) {
        GLHook::Get().OriginalTranslated(x, y, z);
        return;
    }

    if (std::fabs(z - (-0.72)) < 0.05) {
        if (std::fabs(x - 0.56) < 0.06) {
            GLHook::Get().OriginalTranslated(x + g_vmRightX, y + g_vmRightY, z + g_vmRightZ);
            glScaled(g_vmRightScale, g_vmRightScale, g_vmRightScale);
            return;
        } else if (std::fabs(x - (-0.56)) < 0.06) {
            GLHook::Get().OriginalTranslated(x + g_vmLeftX, y + g_vmLeftY, z + g_vmLeftZ);
            glScaled(g_vmLeftScale, g_vmLeftScale, g_vmLeftScale);
            return;
        }
    }

    GLHook::Get().OriginalTranslated(x, y, z);
}

// ── Обработчик glTranslatef (ViewModel) ──────────────────────────────────────
void APIENTRY Hooked_glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    if (GLHook::Get().IsUnloading() || !g_optViewModel) {
        GLHook::Get().OriginalTranslatef(x, y, z);
        return;
    }

    if (std::fabs(z - (-0.72f)) < 0.05f) {
        if (std::fabs(x - 0.56f) < 0.06f) {
            GLHook::Get().OriginalTranslatef(x + g_vmRightX, y + g_vmRightY, z + g_vmRightZ);
            glScalef(g_vmRightScale, g_vmRightScale, g_vmRightScale);
            return;
        } else if (std::fabs(x - (-0.56f)) < 0.06f) {
            GLHook::Get().OriginalTranslatef(x + g_vmLeftX, y + g_vmLeftY, z + g_vmLeftZ);
            glScalef(g_vmLeftScale, g_vmLeftScale, g_vmLeftScale);
            return;
        }
    }

    GLHook::Get().OriginalTranslatef(x, y, z);
}

// ── LWJGL2 JNI nglTranslatef ─────────────────────────────────────────────────
void __stdcall Hooked_nglTranslatef(void* env, void* clazz, float x, float y, float z, __int64 func) {
    if (GLHook::Get().IsUnloading() || !g_optViewModel) {
        GLHook::Get().OriginalNtranslate(env, clazz, x, y, z, func);
        return;
    }

    if (std::fabs(z - (-0.72f)) < 0.05f) {
        if (std::fabs(x - 0.56f) < 0.06f) {
            GLHook::Get().OriginalNtranslate(env, clazz, x + g_vmRightX, y + g_vmRightY, z + g_vmRightZ, func);
            glScalef(g_vmRightScale, g_vmRightScale, g_vmRightScale);
            return;
        } else if (std::fabs(x - (-0.56f)) < 0.06f) {
            GLHook::Get().OriginalNtranslate(env, clazz, x + g_vmLeftX, y + g_vmLeftY, z + g_vmLeftZ, func);
            glScalef(g_vmLeftScale, g_vmLeftScale, g_vmLeftScale);
            return;
        }
    }

    GLHook::Get().OriginalNtranslate(env, clazz, x, y, z, func);
}

static inline bool IsPerspectiveMatrix(const float* m) {
    if (!m) return false;
    // Перспективная матрица Minecraft: m[11] == -1.0f или m[15] == 0.0f при положительных диагоналях m[0] и m[5]
    return (std::fabs(m[11] - (-1.0f)) < 0.05f) ||
           (std::fabs(m[15]) < 0.001f && m[0] > 0.01f && m[5] > 0.01f);
}

// ── Обработчик glMultMatrixf для AspectRatio ──────────────────────────────────
void APIENTRY Hooked_glMultMatrixf(const GLfloat* m) {
    if (GLHook::Get().IsUnloading() || !g_optAspect || !m) {
        GLHook::Get().OriginalMultMatrixf(m);
        return;
    }

    if (IsPerspectiveMatrix(m)) {
        float copyM[16];
        memcpy(copyM, m, 16 * sizeof(float));
        if (std::fabs(g_aspectRatioVal - 1.0f) > 0.005f) {
            copyM[0] /= g_aspectRatioVal; // Растяжение/сжатие по горизонтали
        }
        GLHook::Get().OriginalMultMatrixf(copyM);
        return;
    }

    GLHook::Get().OriginalMultMatrixf(m);
}

// ── Обработчик glLoadMatrixf для AspectRatio ──────────────────────────────────
void APIENTRY Hooked_glLoadMatrixf(const GLfloat* m) {
    if (GLHook::Get().IsUnloading() || !g_optAspect || !m) {
        GLHook::Get().OriginalLoadMatrixf(m);
        return;
    }

    if (IsPerspectiveMatrix(m)) {
        float copyM[16];
        memcpy(copyM, m, 16 * sizeof(float));
        if (std::fabs(g_aspectRatioVal - 1.0f) > 0.005f) {
            copyM[0] /= g_aspectRatioVal;
        }
        GLHook::Get().OriginalLoadMatrixf(copyM);
        return;
    }

    GLHook::Get().OriginalLoadMatrixf(m);
}

// ── LWJGL2 JNI nglMultMatrixf для AspectRatio ─────────────────────────────────
void __stdcall Hooked_nglMultMatrixf(void* env, void* clazz, __int64 m, __int64 func) {
    if (GLHook::Get().IsUnloading() || !g_optAspect || !m) {
        GLHook::Get().OriginalNmultMatrix(env, clazz, m, func);
        return;
    }

    float* pMat = reinterpret_cast<float*>(m);
    if (IsPerspectiveMatrix(pMat)) {
        float copyM[16];
        memcpy(copyM, pMat, 16 * sizeof(float));
        if (std::fabs(g_aspectRatioVal - 1.0f) > 0.005f) {
            copyM[0] /= g_aspectRatioVal;
        }
        GLHook::Get().OriginalNmultMatrix(env, clazz, reinterpret_cast<__int64>(copyM), func);
        return;
    }

    GLHook::Get().OriginalNmultMatrix(env, clazz, m, func);
}

// ── LWJGL2 JNI nglLoadMatrixf для AspectRatio ──────────────────────────────────
void __stdcall Hooked_nglLoadMatrixf(void* env, void* clazz, __int64 m, __int64 func) {
    if (GLHook::Get().IsUnloading() || !g_optAspect || !m) {
        GLHook::Get().OriginalNloadMatrix(env, clazz, m, func);
        return;
    }

    float* pMat = reinterpret_cast<float*>(m);
    if (IsPerspectiveMatrix(pMat)) {
        float copyM[16];
        memcpy(copyM, pMat, 16 * sizeof(float));
        if (std::fabs(g_aspectRatioVal - 1.0f) > 0.005f) {
            copyM[0] /= g_aspectRatioVal;
        }
        GLHook::Get().OriginalNloadMatrix(env, clazz, reinterpret_cast<__int64>(copyM), func);
        return;
    }

    GLHook::Get().OriginalNloadMatrix(env, clazz, m, func);
}

// ── Перехват glScalef (масштабирование серверных титров и надписей Minecraft 1.12.2 для модуля Titles) ──
static inline bool ShouldScaleTitles(float x, float y, float z) {
    if (!g_optTitles || std::fabs(g_titlesScaleVal - 1.0f) <= 0.005f) return false;

    // 1. Если открыт экран интерфейса (инвентарь, сундук, карта, чат, смерть, меню) — НИКОГДА не масштабируем
    const auto& mcData = JVMBridge::Get().Data();
    if (mcData.hasScreen) return false;

    // 2. Проверяем 2D ортографическую проекцию интерфейса Minecraft (HUD)
    GLfloat proj[16];
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    if (std::fabs(proj[15] - 1.0f) > 0.05f || std::fabs(proj[11]) > 0.05f) return false;
    if (proj[0] <= 0.0001f || proj[5] >= -0.0001f) return false;

    // 3. Масштабируем любые серверные титры, подзаголовки и интерактивные надписи в 2D HUD:
    // x и y пропорциональны (x == y) и находятся в диапазоне [1.4 .. 6.0]
    // 4.0F — Title ("Безопасная зона")
    // 2.0F — Subtitle ("Забрать [F]", "Следуй правилам...")
    // 1.5F .. 3.5F — кастомные серверные плагины подсказок и действий
    if (std::fabs(x - y) > 0.10f) return false;
    if (x < 1.4f || x > 6.5f) return false;

    return true;
}

void APIENTRY Hooked_glScalef(GLfloat x, GLfloat y, GLfloat z) {
    if (GLHook::Get().IsUnloading()) {
        if (GLHook::Get().OriginalScalef) GLHook::Get().OriginalScalef(x, y, z);
        return;
    }

    if (ShouldScaleTitles(x, y, z)) {
        x *= g_titlesScaleVal;
        y *= g_titlesScaleVal;
        z *= g_titlesScaleVal;
    }

    if (GLHook::Get().OriginalScalef) {
        GLHook::Get().OriginalScalef(x, y, z);
    }
}

void APIENTRY Hooked_glScaled(GLdouble x, GLdouble y, GLdouble z) {
    if (GLHook::Get().IsUnloading()) {
        if (GLHook::Get().OriginalScaled) GLHook::Get().OriginalScaled(x, y, z);
        return;
    }

    if (ShouldScaleTitles(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z))) {
        x *= static_cast<GLdouble>(g_titlesScaleVal);
        y *= static_cast<GLdouble>(g_titlesScaleVal);
        z *= static_cast<GLdouble>(g_titlesScaleVal);
    }

    if (GLHook::Get().OriginalScaled) {
        GLHook::Get().OriginalScaled(x, y, z);
    }
}

void __stdcall Hooked_nglScalef(void* env, void* clazz, float x, float y, float z, __int64 func) {
    if (GLHook::Get().IsUnloading()) {
        if (GLHook::Get().OriginalNscale) GLHook::Get().OriginalNscale(env, clazz, x, y, z, func);
        return;
    }

    if (ShouldScaleTitles(x, y, z)) {
        x *= g_titlesScaleVal;
        y *= g_titlesScaleVal;
        z *= g_titlesScaleVal;
    }

    if (GLHook::Get().OriginalNscale) {
        GLHook::Get().OriginalNscale(env, clazz, x, y, z, func);
    }
}

// ── Перехват BlendFunc для скрытия серверного прицела при активном Crosshair ─
// В Minecraft ванильный/серверный прицел рисуется через инверсный блендинг:
// - tryBlendFuncSeparate(GL_ONE_MINUS_DST_COLOR (775), GL_ONE_MINUS_SRC_COLOR (776), 1, 0)
// - или glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR)
// Отключаем запись цвета (glColorMask) на время отрисовки этого квада.
void __stdcall Hooked_nglBlendFuncSeparate(void* env, void* clazz, int sfactorRGB, int dfactorRGB, int sfactorAlpha, int dfactorAlpha, __int64 func) {
    if (g_hideCrosshair) {
        if ((sfactorRGB == 775 && dfactorRGB == 776) || (sfactorRGB == 776 && dfactorRGB == 775)) {
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            s_crosshairMasked = true;
        } else if (s_crosshairMasked) {
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            s_crosshairMasked = false;
        }
    } else if (s_crosshairMasked) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        s_crosshairMasked = false;
    }

    if (OriginalNglBlendFuncSeparate) {
        OriginalNglBlendFuncSeparate(env, clazz, sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha, func);
    }
}

void __stdcall Hooked_nglBlendFunc(void* env, void* clazz, int sfactor, int dfactor, __int64 func) {
    if (g_hideCrosshair) {
        if ((sfactor == 775 && dfactor == 776) || (sfactor == 776 && dfactor == 775)) {
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            s_crosshairMasked = true;
        } else if (s_crosshairMasked) {
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            s_crosshairMasked = false;
        }
    } else if (s_crosshairMasked) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        s_crosshairMasked = false;
    }

    if (OriginalNglBlendFunc) {
        OriginalNglBlendFunc(env, clazz, sfactor, dfactor, func);
    }
}

void APIENTRY Hooked_glBlendFunc(GLenum sfactor, GLenum dfactor) {
    if (g_hideCrosshair) {
        if ((sfactor == 775 && dfactor == 776) || (sfactor == 776 && dfactor == 775)) {
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            s_crosshairMasked = true;
        } else if (s_crosshairMasked) {
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            s_crosshairMasked = false;
        }
    } else if (s_crosshairMasked) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        s_crosshairMasked = false;
    }

    if (OriginalGlBlendFunc) {
        OriginalGlBlendFunc(sfactor, dfactor);
    }
}
