#include "renderer.h"

#include <stdio.h>

static void Renderer_LogInitFailure (const char *message)
{
    DWORD error_code;

    error_code = GetLastError();
    fprintf(stderr, "Renderer init failure: %s (GetLastError=%lu)\n", message, (unsigned long) error_code);
}

static void *Renderer_GetGLProcAddress (const char *name)
{
    void *result;
    HMODULE opengl_module;

    ASSERT(name != NULL);

    result = (void *) wglGetProcAddress(name);
    if ((result == NULL) || (result == (void *) 0x1) || (result == (void *) 0x2) || (result == (void *) 0x3) || (result == (void *) -1))
    {
        opengl_module = GetModuleHandleA("opengl32.dll");
        if (opengl_module == NULL)
        {
            opengl_module = LoadLibraryA("opengl32.dll");
        }

        if (opengl_module != NULL)
        {
            result = (void *) GetProcAddress(opengl_module, name);
        }
    }

    return result;
}

static b32 Renderer_SetPixelFormat (HDC device_context)
{
    PIXELFORMATDESCRIPTOR pixel_format = {0};
    i32 pixel_format_index;

    pixel_format.nSize = sizeof(pixel_format);
    pixel_format.nVersion = 1;
    pixel_format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pixel_format.iPixelType = PFD_TYPE_RGBA;
    pixel_format.cColorBits = 32;
    pixel_format.cAlphaBits = 8;
    pixel_format.cDepthBits = 24;
    pixel_format.cStencilBits = 8;
    pixel_format.iLayerType = PFD_MAIN_PLANE;

    pixel_format_index = ChoosePixelFormat(device_context, &pixel_format);
    if (pixel_format_index == 0)
    {
        return false;
    }

    return SetPixelFormat(device_context, pixel_format_index, &pixel_format) == TRUE;
}

static void Renderer_ApplyClipRect (Renderer *renderer, Rect2 rect)
{
    i32 x;
    i32 y;
    i32 width;
    i32 height;

    x = (i32) rect.min.x;
    y = (i32) (renderer->framebuffer_size.y - rect.max.y);
    width = MAX(0, (i32) (rect.max.x - rect.min.x));
    height = MAX(0, (i32) (rect.max.y - rect.min.y));

    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, width, height);
}

static Rect2 Renderer_IntersectRect (Rect2 a, Rect2 b)
{
    return Rect2_Create(
        MAX(a.min.x, b.min.x),
        MAX(a.min.y, b.min.y),
        MIN(a.max.x, b.max.x),
        MIN(a.max.y, b.max.y)
    );
}

static void Renderer_PushClipRect (Renderer *renderer, Rect2 rect)
{
    Rect2 framebuffer_rect;

    ASSERT(renderer->clip_stack_count < ARRAY_COUNT(renderer->clip_stack));

    framebuffer_rect = Rect2_Create(0.0f, 0.0f, renderer->framebuffer_size.x, renderer->framebuffer_size.y);
    rect = Renderer_IntersectRect(rect, framebuffer_rect);
    if (renderer->clip_stack_count > 0)
    {
        rect = Renderer_IntersectRect(rect, renderer->clip_stack[renderer->clip_stack_count - 1]);
    }

    if (rect.max.x < rect.min.x)
    {
        rect.max.x = rect.min.x;
    }

    if (rect.max.y < rect.min.y)
    {
        rect.max.y = rect.min.y;
    }

    renderer->clip_stack[renderer->clip_stack_count] = rect;
    renderer->clip_stack_count += 1;
    Renderer_ApplyClipRect(renderer, rect);
}

static void Renderer_PopClipRect (Renderer *renderer)
{
    ASSERT(renderer->clip_stack_count > 0);

    renderer->clip_stack_count -= 1;
    if (renderer->clip_stack_count == 0)
    {
        glDisable(GL_SCISSOR_TEST);
    }
    else
    {
        Renderer_ApplyClipRect(renderer, renderer->clip_stack[renderer->clip_stack_count - 1]);
    }
}

static void Renderer_SetColor (Vec4 color)
{
    glColor4f(color.x, color.y, color.z, color.w);
}

static void Renderer_DrawFilledRect (Rect2 rect, Vec4 color)
{
    Renderer_SetColor(color);
    glBegin(GL_QUADS);
    glVertex2f(rect.min.x, rect.min.y);
    glVertex2f(rect.max.x, rect.min.y);
    glVertex2f(rect.max.x, rect.max.y);
    glVertex2f(rect.min.x, rect.max.y);
    glEnd();
}

static void Renderer_DrawStrokedRect (Rect2 rect, Vec4 color, GUIEdgeThickness thickness)
{
    Rect2 top_rect;
    Rect2 bottom_rect;
    Rect2 left_rect;
    Rect2 right_rect;

    top_rect = Rect2_Create(rect.min.x, rect.min.y, rect.max.x, MIN(rect.max.y, rect.min.y + thickness.top));
    bottom_rect = Rect2_Create(rect.min.x, MAX(rect.min.y, rect.max.y - thickness.bottom), rect.max.x, rect.max.y);
    left_rect = Rect2_Create(rect.min.x, rect.min.y, MIN(rect.max.x, rect.min.x + thickness.left), rect.max.y);
    right_rect = Rect2_Create(MAX(rect.min.x, rect.max.x - thickness.right), rect.min.y, rect.max.x, rect.max.y);

    Renderer_DrawFilledRect(top_rect, color);
    Renderer_DrawFilledRect(bottom_rect, color);
    Renderer_DrawFilledRect(left_rect, color);
    Renderer_DrawFilledRect(right_rect, color);
}

static void Renderer_DrawText (Renderer *renderer, Vec2 position, String text, Vec4 color)
{
    ASSERT(renderer != NULL);

    if ((renderer->font_display_list_base == 0) || String_IsEmpty(text))
    {
        return;
    }

    Renderer_SetColor(color);
    glRasterPos2f(position.x, position.y);
    glListBase(renderer->font_display_list_base - 32u);
    glCallLists((GLsizei) text.count, GL_UNSIGNED_BYTE, text.data);
}

b32 Renderer_Initialize (Renderer *renderer, PlatformWindow window)
{
    PlatformWindowState *window_state;

    ASSERT(renderer != NULL);
    ASSERT(PlatformWindow_IsValid(window));

    Memory_ZeroStruct(renderer);

    window_state = Platform_GetWindowState(window);
    ASSERT(window_state != NULL);

    renderer->window = window;
    renderer->hwnd = window_state->hwnd;
    renderer->device_context = GetDC(renderer->hwnd);
    if (renderer->device_context == NULL)
    {
        Renderer_LogInitFailure("GetDC failed");
        return false;
    }

    if (!Renderer_SetPixelFormat(renderer->device_context))
    {
        Renderer_LogInitFailure("Renderer_SetPixelFormat failed");
        Renderer_Shutdown(renderer);
        return false;
    }

    renderer->gl_context = wglCreateContext(renderer->device_context);
    if (renderer->gl_context == NULL)
    {
        Renderer_LogInitFailure("wglCreateContext failed");
        Renderer_Shutdown(renderer);
        return false;
    }

    if (wglMakeCurrent(renderer->device_context, renderer->gl_context) != TRUE)
    {
        Renderer_LogInitFailure("wglMakeCurrent failed");
        Renderer_Shutdown(renderer);
        return false;
    }

    if (!gladLoadGLLoader((GLADloadproc) Renderer_GetGLProcAddress))
    {
        Renderer_LogInitFailure("gladLoadGLLoader failed");
        Renderer_Shutdown(renderer);
        return false;
    }

    renderer->font_display_list_base = glGenLists(96);
    if (renderer->font_display_list_base != 0)
    {
        renderer->font = CreateFontA(
            -16,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            ANSI_CHARSET,
            OUT_TT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY,
            FF_DONTCARE,
            "Segoe UI"
        );

        if (renderer->font != NULL)
        {
            SelectObject(renderer->device_context, renderer->font);
            if (!wglUseFontBitmapsA(renderer->device_context, 32u, 96u, renderer->font_display_list_base))
            {
                glDeleteLists(renderer->font_display_list_base, 96);
                renderer->font_display_list_base = 0;
            }
        }
    }

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    return true;
}

void Renderer_Shutdown (Renderer *renderer)
{
    ASSERT(renderer != NULL);

    if (renderer->font_display_list_base != 0)
    {
        glDeleteLists(renderer->font_display_list_base, 96);
        renderer->font_display_list_base = 0;
    }

    if (renderer->font != NULL)
    {
        DeleteObject(renderer->font);
        renderer->font = NULL;
    }

    if (renderer->gl_context != NULL)
    {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(renderer->gl_context);
        renderer->gl_context = NULL;
    }

    if ((renderer->hwnd != NULL) && (renderer->device_context != NULL))
    {
        ReleaseDC(renderer->hwnd, renderer->device_context);
        renderer->device_context = NULL;
    }

    renderer->hwnd = NULL;
    renderer->window = HANDLE64_INVALID;
    renderer->clip_stack_count = 0;
}

void Renderer_BeginFrame (Renderer *renderer, Vec2 framebuffer_size, Vec4 clear_color)
{
    ASSERT(renderer != NULL);

    renderer->framebuffer_size = framebuffer_size;
    renderer->clip_stack_count = 0;

    wglMakeCurrent(renderer->device_context, renderer->gl_context);

    glViewport(0, 0, (GLsizei) framebuffer_size.x, (GLsizei) framebuffer_size.y);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, framebuffer_size.x, framebuffer_size.y, 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void Renderer_DrawGUI (Renderer *renderer, const GUIDrawCommandBuffer *commands)
{
    usize command_index;

    ASSERT(renderer != NULL);
    ASSERT(commands != NULL);

    for (command_index = 0; command_index < commands->count; command_index += 1)
    {
        const GUIDrawCommand *command;

        command = commands->commands + command_index;
        switch (command->type)
        {
            case GUI_DRAW_COMMAND_TYPE_PUSH_CLIP_RECT:
            {
                Renderer_PushClipRect(renderer, command->data.push_clip_rect.rect);
            } break;

            case GUI_DRAW_COMMAND_TYPE_POP_CLIP_RECT:
            {
                Renderer_PopClipRect(renderer);
            } break;

            case GUI_DRAW_COMMAND_TYPE_FILLED_RECT:
            {
                Renderer_DrawFilledRect(command->data.filled_rect.rect, command->data.filled_rect.color);
            } break;

            case GUI_DRAW_COMMAND_TYPE_STROKED_RECT:
            {
                Renderer_DrawStrokedRect(
                    command->data.stroked_rect.rect,
                    command->data.stroked_rect.color,
                    command->data.stroked_rect.thickness
                );
            } break;

            case GUI_DRAW_COMMAND_TYPE_TEXT:
            {
                Renderer_DrawText(
                    renderer,
                    command->data.text.position,
                    command->data.text.text,
                    command->data.text.color
                );
            } break;

            default:
            {
            } break;
        }
    }
}

void Renderer_EndFrame (Renderer *renderer)
{
    ASSERT(renderer != NULL);
    SwapBuffers(renderer->device_context);
}
