#include <util/util.h>
#include "include/imgui_layer_internal.h"
#include "include/main.h"
#include "include/shaiya/CNetwork.h"
#include "include/custom_chat.h"

unsigned u0x422B96 = 0x422B96;
void __declspec(naked) naked_chat_add_token_filter()
{
    using namespace imgui_layer;
    __asm
    {
        push eax
        push ecx
        push edx

        mov eax, dword ptr[esp+0x10]
        mov edx, dword ptr[esp+0x14]
        push edx
        push eax
        call imgui_layer::prepare_chat_text_for_emojis
        add esp, 0x8
        mov dword ptr[esp+0x14], eax

        pop edx
        pop ecx
        pop eax

        sub esp, 0xA4C
        jmp u0x422B96
    }
}

unsigned u0x422636 = 0x422636;
void __declspec(naked) naked_chatbox_add_line_filter()
{
    __asm
    {
        pushad
        call custom_chat::hide_native_chat_visuals
        test al, al
        popad
        jnz suppressed

        // Replay overwritten prologue at 0x422630.
        push ecx
        push ebx
        mov ebx, dword ptr [esp + 0x0C]
        jmp u0x422636

    suppressed:
        ret 0x08
    }
}

unsigned u0x41FCC0 = 0x41FCC0;
void __declspec(naked) naked_chat_balloon_text_create()
{
    __asm
    {
        push eax
        push ecx
        push edx

        mov eax, dword ptr[esp+0x10]
        push eax
        call imgui_layer::prepare_floating_text_for_emojis
        add esp, 0x04
        mov dword ptr[esp+0x10], eax

        pop edx
        pop ecx
        pop eax

        jmp u0x41FCC0
    }
}

unsigned u0x412753 = 0x412753;
void __declspec(naked) naked_capture_chat_balloon_text()
{
    __asm
    {
        pushad
        push eax
        call imgui_layer::capture_chat_balloon_text
        add esp, 0x04
        popad

        fld dword ptr ds:[0x747538]
        jmp u0x412753
    }
}

unsigned u0x41FCF0 = 0x41FCF0;
void __declspec(naked) naked_floating_text_create()
{
    __asm
    {
        push eax
        push ecx
        push edx

        mov eax, dword ptr[esp+0x10]
        push eax
        call imgui_layer::prepare_floating_text_for_emojis
        add esp, 0x04
        mov dword ptr[esp+0x10], eax

        pop edx
        pop ecx
        pop eax

        jmp u0x41FCF0
    }
}

unsigned u0x453DFD = 0x453DFD;
void __declspec(naked) naked_capture_floating_static_text()
{
    __asm
    {
        add esp, 0x0C

        pushad
        push eax
        call imgui_layer::capture_floating_static_text
        add esp, 0x04
        popad

        mov dword ptr[esi+0x324], eax
        jmp u0x453DFD
    }
}

unsigned u0x57C280 = 0x57C280;
void __declspec(naked) naked_static_text_create()
{
    __asm
    {
        push ecx
        push dword ptr[esp+0x08]
        call imgui_layer::prepare_static_text_for_emojis
        add esp, 0x04
        pop ecx

        push eax
        call u0x57C280

        push eax
        push eax
        call imgui_layer::capture_created_static_text
        add esp, 0x04
        pop eax

        ret 0x04
    }
}

unsigned u0x57CA20 = 0x57CA20;
void __declspec(naked) naked_floating_static_text_draw()
{
    __asm
    {
        pushad
        mov eax, dword ptr[esp+0x24]
        mov edx, dword ptr[esp+0x28]
        mov ecx, dword ptr[esp+0x2C]
        push ecx
        push edx
        push eax
        call imgui_layer::record_floating_static_text_render
        add esp, 0x0C
        popad

        jmp u0x57CA20
    }
}

unsigned u0x573C00 = 0x573C00;
void __declspec(naked) naked_native_text_draw_probe()
{
    __asm
    {
        pushad
        mov eax, dword ptr[esp+0x20]
        mov edx, dword ptr[esp+0x28]
        mov ecx, dword ptr[esp+0x2C]
        mov ebx, dword ptr[esp+0x38]
        push ecx
        push edx
        push ebx
        push eax
        call imgui_layer::record_native_text_draw_probe
        add esp, 0x10
        popad

        jmp u0x573C00
    }
}

unsigned u0x4DE6B0 = 0x4DE6B0;
void __declspec(naked) naked_capture_main_map_anchor()
{
    using namespace imgui_layer;
    __asm
    {
        mov g_mainMapObjectPtr, esi

        lea ecx, [esi+0xCB8]
        jmp u0x4DE6B0
    }
}

// Hook at VA 0x47D1F0 — chat panel init function.
// Original bytes: 51 53 55 56 8B F1 (push ecx; push ebx; push ebp; push esi; mov esi,ecx)
// We capture ecx (this = 0x75E0-byte chat panel object) then replay the
// overwritten prologue and jump back.
unsigned u0x47D1F6 = 0x47D1F6;
void __declspec(naked) naked_capture_chat_panel()
{
    using namespace imgui_layer;
    __asm
    {
        mov g_chatPanelPtr, ecx

        // Replay overwritten prologue (6 bytes)
        push ecx
        push ebx
        push ebp
        push esi
        mov esi, ecx
        jmp u0x47D1F6
    }
}

void tick_client_welcome_sysmsg()
{
    imgui_layer::send_welcome_sysmsg_once();
}

void hook::imgui_layer()
{
    // Capture main_map CWindow pos/size every frame for ImGui button anchoring.
    util::detour((void*)0x4DE6AA, naked_capture_main_map_anchor, 6);

    // Capture the chat panel object pointer (0x75E0-byte UI container)
    // so the custom chat can follow native resize and placement.
    util::detour((void*)0x47D1F0, naked_capture_chat_panel, 6);

    imgui_layer::install_chat_emoji_hook();
    util::detour((void*)0x422630, naked_chatbox_add_line_filter, 6);

    if (imgui_layer::g_running.exchange(true))
        return;

    auto thread = CreateThread(nullptr, 0, imgui_layer::render_thread, nullptr, 0, nullptr);
    if (thread)
        CloseHandle(thread);
}

bool handle_imgui_layer_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result)
{
    return imgui_layer::handle_wnd_proc(hwnd, msg, wParam, lParam, result);
}
