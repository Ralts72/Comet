#pragma once

struct ImGuiContext;

// 前端和 GPU 录制各自选择当前上下文；不共享可变的进程级指针。
inline thread_local ImGuiContext* comet_imgui_context = nullptr;
#define GImGui comet_imgui_context
