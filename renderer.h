#ifndef RENDERER_H
#define RENDERER_H

#include "gui/gui.h"
#include "platform/platform_internal.h"
#include "glad/glad.h"

typedef struct Renderer
{
    PlatformWindow window;
    HWND hwnd;
    HDC device_context;
    HGLRC gl_context;
    GLuint font_display_list_base;
    HFONT font;
    Vec2 framebuffer_size;
    Rect2 clip_stack[64];
    usize clip_stack_count;
} Renderer;

b32 Renderer_Initialize (Renderer *renderer, PlatformWindow window);
void Renderer_Shutdown (Renderer *renderer);
void Renderer_BeginFrame (Renderer *renderer, Vec2 framebuffer_size, Vec4 clear_color);
void Renderer_DrawGUI (Renderer *renderer, const GUIDrawCommandBuffer *commands);
void Renderer_EndFrame (Renderer *renderer);

#endif // RENDERER_H
