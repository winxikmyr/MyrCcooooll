// src/hooks/gl_hook.h
#pragma once
#include <Windows.h>
#include <GL/gl.h>

using fnWglSwapBuffers  = BOOL(WINAPI*)(HDC);
using fnSwapBuffers     = BOOL(WINAPI*)(HDC);
using fnGlTranslatef    = void(APIENTRY*)(GLfloat, GLfloat, GLfloat);
using fnGlTranslated    = void(APIENTRY*)(GLdouble, GLdouble, GLdouble);
using fnNglTranslatef   = void(__stdcall*)(void* env, void* clazz, float x, float y, float z, __int64 func);
using fnGlScalef        = void(APIENTRY*)(GLfloat, GLfloat, GLfloat);
using fnGlScaled        = void(APIENTRY*)(GLdouble, GLdouble, GLdouble);
using fnNglScalef       = void(__stdcall*)(void* env, void* clazz, float x, float y, float z, __int64 func);
using fnGlMultMatrixf   = void(APIENTRY*)(const GLfloat*);
using fnNglMultMatrixf  = void(__stdcall*)(void* env, void* clazz, __int64 m, __int64 func);
using fnGlLoadMatrixf   = void(APIENTRY*)(const GLfloat*);
using fnNglLoadMatrixf  = void(__stdcall*)(void* env, void* clazz, __int64 m, __int64 func);
using fnGlBlendFunc          = void(APIENTRY*)(GLenum, GLenum);
using fnNglBlendFunc         = void(__stdcall*)(void* env, void* clazz, int, int, __int64);
using fnNglBlendFuncSeparate = void(__stdcall*)(void* env, void* clazz, int, int, int, int, __int64);

class GLHook {
public:
    static GLHook& Get() { static GLHook i; return i; }

    bool Initialize();
    void Shutdown();

    bool IsUnloading() const { return m_unloading; }

    void SetViewModelHookActive(bool active);
    void SetAspectRatioHookActive(bool active);
    void SetTitlesHookActive(bool active);

    fnWglSwapBuffers  Original            = nullptr;
    fnSwapBuffers     OriginalGdiSwap     = nullptr;
    fnGlTranslatef    OriginalTranslatef  = nullptr;
    fnGlTranslated    OriginalTranslated  = nullptr;
    fnNglTranslatef   OriginalNtranslate  = nullptr;
    fnGlScalef        OriginalScalef      = nullptr;
    fnGlScaled        OriginalScaled      = nullptr;
    fnNglScalef       OriginalNscale      = nullptr;
    fnGlMultMatrixf   OriginalMultMatrixf = nullptr;
    fnNglMultMatrixf  OriginalNmultMatrix = nullptr;
    fnGlLoadMatrixf   OriginalLoadMatrixf = nullptr;
    fnNglLoadMatrixf  OriginalNloadMatrix = nullptr;
    bool              Ready               = false;

    static void RecordSwapFrame();
    static int  GetExactFPS();

private:
    GLHook() = default;
    volatile bool     m_unloading = false;
};

extern bool g_optViewModel;
extern float g_vmRightX, g_vmRightY, g_vmRightZ, g_vmRightScale;
extern float g_vmLeftX, g_vmLeftY, g_vmLeftZ, g_vmLeftScale;
extern bool g_optAspect;
extern float g_aspectRatioVal;
extern bool g_optTitles;
extern float g_titlesScaleVal;
extern bool g_hideCrosshair;

BOOL WINAPI Hooked_wglSwapBuffers(HDC hdc);
BOOL WINAPI Hooked_gdiSwapBuffers(HDC hdc);
void APIENTRY Hooked_glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void APIENTRY Hooked_glTranslated(GLdouble x, GLdouble y, GLdouble z);
void __stdcall Hooked_nglTranslatef(void* env, void* clazz, float x, float y, float z, __int64 func);
void APIENTRY Hooked_glScalef(GLfloat x, GLfloat y, GLfloat z);
void APIENTRY Hooked_glScaled(GLdouble x, GLdouble y, GLdouble z);
void __stdcall Hooked_nglScalef(void* env, void* clazz, float x, float y, float z, __int64 func);
void APIENTRY Hooked_glMultMatrixf(const GLfloat* m);
void __stdcall Hooked_nglMultMatrixf(void* env, void* clazz, __int64 m, __int64 func);
void APIENTRY Hooked_glLoadMatrixf(const GLfloat* m);
void __stdcall Hooked_nglLoadMatrixf(void* env, void* clazz, __int64 m, __int64 func);
void APIENTRY Hooked_glBlendFunc(GLenum sfactor, GLenum dfactor);
void __stdcall Hooked_nglBlendFunc(void* env, void* clazz, int sfactor, int dfactor, __int64 func);
void __stdcall Hooked_nglBlendFuncSeparate(void* env, void* clazz, int sfactorRGB, int dfactorRGB, int sfactorAlpha, int dfactorAlpha, __int64 func);
