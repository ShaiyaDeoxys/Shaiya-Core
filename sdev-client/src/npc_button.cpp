// ===========================================================================
// npc_button.cpp — Inventory NPC shortcut buttons
//
// Draws a vertical strip of icon buttons along the right edge of the
// inventory panel.  Each button loads its texture from data.saf at
// Assets/General/inven_new_buton{1..6}.png (89x89 x 4 sprite strip:
// normal, hover, pressed, disabled).
//
// Hook site: 0x518215 (inventory panel render path, 9-byte detour).
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <external/stb/stb_image.h>
#include <util/util.h>
#include <shaiya/include/common/NpcTypes.h>
#include <shaiya/include/network/game/incoming/0200.h>
#include "include/main.h"
#include "include/shaiya/CNetwork.h"
#include "include/shaiya/CPlayerData.h"
#include "include/shaiya/Static.h"
using namespace shaiya;

namespace
{
    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------
    constexpr int kButtonCount        = 6;
    constexpr int kFramePixels        = 89;   // each sprite frame is 89x89 in the PNG
    constexpr int kButtonSize         = 32;   // draw size on screen (matches inventory slot)
    constexpr int kFramesPerStrip     = 4;    // normal, hover, pressed, disabled
    constexpr int kFrameNormal        = 0;
    constexpr int kFrameHover         = 1;
    constexpr int kFramePressed       = 2;

    // SAH data folder where button PNGs live inside data.saf.
    constexpr const char* kAssetFolder = "assets\\general";

    // File names inside kAssetFolder (case-insensitive match).
    constexpr const char* kButtonFileNames[kButtonCount] =
    {
        "inven_new_buton1.png",
        "inven_new_buton2.png",
        "inven_new_buton3.png",
        "inven_new_buton4.png",
        "inven_new_buton5.png",
        "inven_new_buton6.png",
    };

    // -----------------------------------------------------------------------
    // Remote NPC actions (no physical NPC proximity required)
    // -----------------------------------------------------------------------
    enum class RemoteNpcAction
    {
        Vet,
        Bank,
        Guild
    };

    void open_remote_npc(RemoteNpcAction action)
    {
        if (g_pPlayerData->windowType != WindowType::None)
        {
            Static::SysMsgToChatBox(ChatType::Acquire31, 806, 12);
            return;
        }

        switch (action)
        {
        case RemoteNpcAction::Vet:
        {
            GameStatusResultInfoIncoming outgoing{};
            CNetwork::Send(&outgoing, sizeof(GameStatusResultInfoIncoming));

            g_var->killLv = 0;
            g_var->deathLv = 0;

            g_pPlayerData->npcType = std::to_underlying(NpcType::VetManager);
            g_pPlayerData->npcTypeId = g_pPlayerData->country == Country::Light ? 1 : 2;
            g_pPlayerData->npcIcon = 55;
            g_pPlayerData->textBuffer[0] = '\0';
            break;
        }
        case RemoteNpcAction::Bank:
        {
            g_pPlayerData->npcType = std::to_underlying(NpcType::Merchant);
            g_pPlayerData->npcTypeId = g_pPlayerData->country == Country::Light ? 179 : 180;
            g_pPlayerData->npcIcon = 55;
            g_pPlayerData->textBuffer[0] = '\0';
            g_pPlayerData->windowType = WindowType::BankTeller;
            break;
        }
        case RemoteNpcAction::Guild:
        {
            g_pPlayerData->npcType = std::to_underlying(NpcType::GuildMaster);
            g_pPlayerData->npcTypeId = g_pPlayerData->country == Country::Light ? 1 : 2;
            g_pPlayerData->npcIcon = 55;
            g_pPlayerData->textBuffer[0] = '\0';
            g_pPlayerData->windowType = WindowType::GuildMaster;
            break;
        }
        default:
            break;
        }
    }

    // -----------------------------------------------------------------------
    // Button definitions
    // -----------------------------------------------------------------------
    enum class ButtonActionType
    {
        LegacyNpcId,
        RemoteNpc
    };

    struct InventoryNpcButton
    {
        ButtonActionType actionType;
        int actionValue;
        int xStart;
        int yStart;
    };

    // Button positions relative to the inventory panel top-left.
    // Each button is kFrameSize x kFrameSize (89x89).
    constexpr std::array<InventoryNpcButton, kButtonCount> kButtons{{
        { ButtonActionType::LegacyNpcId, 0x65, 253, 291 },
        { ButtonActionType::LegacyNpcId, 0x66, 253, 328 },
        { ButtonActionType::LegacyNpcId, 0x79, 253, 365 },
        { ButtonActionType::RemoteNpc, static_cast<int>(RemoteNpcAction::Vet),   253, 402 },
        { ButtonActionType::RemoteNpc, static_cast<int>(RemoteNpcAction::Bank),  253, 439 },
        { ButtonActionType::RemoteNpc, static_cast<int>(RemoteNpcAction::Guild), 253, 476 },
    }};

    // -----------------------------------------------------------------------
    // Texture state per button
    // -----------------------------------------------------------------------
    struct ButtonTextureInfo
    {
        uint64_t sahOffset = 0;
        uint64_t sahSize   = 0;
        bool     found     = false;
        bool     loadAttempted = false;
        LPDIRECT3DTEXTURE9 texture = nullptr;
        int      texWidth  = 0;
        int      texHeight = 0;
    };

    ButtonTextureInfo g_btnTex[kButtonCount]{};
    bool g_sahScanned = false;
    unsigned u0x518220 = 0x518220;
    bool g_leftMouseWasDown = false;

    // -----------------------------------------------------------------------
    // Helpers: game-relative path, SAH parsing, SAF reading
    // -----------------------------------------------------------------------
    std::string get_game_path(const char* relativePath)
    {
        char modulePath[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, modulePath, sizeof(modulePath)) == 0)
            return relativePath ? relativePath : "";

        std::string path(modulePath);
        auto slash = path.find_last_of("\\/");
        if (slash == std::string::npos)
            return relativePath ? relativePath : "";

        path.resize(slash + 1);
        if (relativePath)
            path += relativePath;
        return path;
    }

    bool sah_read_u32(const std::vector<char>& d, std::size_t& o, uint32_t& v)
    {
        if (o + 4 > d.size()) return false;
        std::memcpy(&v, d.data() + o, 4); o += 4;
        return true;
    }

    bool sah_read_u64(const std::vector<char>& d, std::size_t& o, uint64_t& v)
    {
        if (o + 8 > d.size()) return false;
        std::memcpy(&v, d.data() + o, 8); o += 8;
        return true;
    }

    bool sah_read_string(const std::vector<char>& d, std::size_t& o, std::string& v)
    {
        uint32_t len = 0;
        if (!sah_read_u32(d, o, len) || o + len > d.size()) return false;
        v.assign(d.data() + o, d.data() + o + len);
        while (!v.empty() && v.back() == '\0') v.pop_back();
        o += len;
        return true;
    }

    std::string to_lower(std::string s)
    {
        for (auto& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // Recursive SAH directory scanner — only captures files matching our
    // button file names inside the kAssetFolder directory.
    bool scan_sah_directory(const std::vector<char>& data, std::size_t& offset,
                            const std::string& parentPath)
    {
        std::string name;
        if (!sah_read_string(data, offset, name))
            return false;

        auto path = parentPath;
        if (!name.empty())
            path = path.empty() ? name : path + "\\" + name;

        uint32_t fileCount = 0;
        if (!sah_read_u32(data, offset, fileCount))
            return false;

        auto lowerPath = to_lower(path);
        bool isTargetFolder = (lowerPath == kAssetFolder)
                            || (lowerPath == std::string("data\\") + kAssetFolder);

        for (uint32_t i = 0; i < fileCount; ++i)
        {
            std::string fileName;
            uint64_t fileOffset = 0, fileSize = 0;
            if (!sah_read_string(data, offset, fileName)
                || !sah_read_u64(data, offset, fileOffset)
                || !sah_read_u64(data, offset, fileSize))
                return false;

            if (isTargetFolder)
            {
                auto lowerFile = to_lower(fileName);
                for (int b = 0; b < kButtonCount; ++b)
                {
                    if (!g_btnTex[b].found && lowerFile == kButtonFileNames[b])
                    {
                        g_btnTex[b].sahOffset = fileOffset;
                        g_btnTex[b].sahSize   = fileSize;
                        g_btnTex[b].found     = true;
                        break;
                    }
                }
            }
        }

        uint32_t dirCount = 0;
        if (!sah_read_u32(data, offset, dirCount))
            return false;

        for (uint32_t i = 0; i < dirCount; ++i)
        {
            if (!scan_sah_directory(data, offset, path))
                return false;
        }
        return true;
    }

    void scan_sah_for_button_textures()
    {
        if (g_sahScanned)
            return;
        g_sahScanned = true;

        auto sahPath = get_game_path("data.sah");
        std::ifstream stream(sahPath, std::ios::binary);
        if (!stream)
            return;

        std::vector<char> data(
            (std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>());
        if (data.size() <= 0x34 || std::memcmp(data.data(), "SAH", 3) != 0)
            return;

        auto offset = std::size_t{ 0x34 };
        scan_sah_directory(data, offset, "");
    }

    // -----------------------------------------------------------------------
    // Texture creation from PNG in data.saf (stb_image → D3D9 A8R8G8B8)
    // -----------------------------------------------------------------------
    LPDIRECT3DTEXTURE9 create_texture_from_png(LPDIRECT3DDEVICE9 device,
                                                const void* data, UINT dataSize,
                                                int& outW, int& outH)
    {
        if (!device || !data || dataSize == 0)
            return nullptr;

        int channels = 0;
        auto* pixels = stbi_load_from_memory(
            static_cast<const stbi_uc*>(data),
            static_cast<int>(dataSize),
            &outW, &outH, &channels, 4);
        if (!pixels)
            return nullptr;

        LPDIRECT3DTEXTURE9 tex = nullptr;
        if (FAILED(device->CreateTexture(
                static_cast<UINT>(outW), static_cast<UINT>(outH),
                1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) || !tex)
        {
            stbi_image_free(pixels);
            return nullptr;
        }

        D3DLOCKED_RECT locked{};
        if (FAILED(tex->LockRect(0, &locked, nullptr, 0)))
        {
            tex->Release();
            stbi_image_free(pixels);
            return nullptr;
        }

        // stb_image RGBA → D3D9 BGRA swizzle
        for (int y = 0; y < outH; ++y)
        {
            auto* src = pixels + y * outW * 4;
            auto* dst = static_cast<BYTE*>(locked.pBits) + y * locked.Pitch;
            for (int x = 0; x < outW; ++x)
            {
                dst[x * 4 + 0] = src[x * 4 + 2]; // B
                dst[x * 4 + 1] = src[x * 4 + 1]; // G
                dst[x * 4 + 2] = src[x * 4 + 0]; // R
                dst[x * 4 + 3] = src[x * 4 + 3]; // A
            }
        }

        tex->UnlockRect(0);
        stbi_image_free(pixels);
        return tex;
    }

    void ensure_button_texture_loaded(int index, LPDIRECT3DDEVICE9 device)
    {
        auto& bt = g_btnTex[index];
        if (bt.texture || bt.loadAttempted)
            return;
        bt.loadAttempted = true;

        if (!bt.found || bt.sahSize == 0 || !device)
            return;

        auto safPath = get_game_path("data.saf");
        std::ifstream stream(safPath, std::ios::binary);
        if (!stream)
            return;

        stream.seekg(static_cast<std::streamoff>(bt.sahOffset), std::ios::beg);
        if (!stream)
            return;

        std::vector<char> fileData(static_cast<std::size_t>(bt.sahSize), 0);
        stream.read(fileData.data(), static_cast<std::streamsize>(fileData.size()));
        if (stream.gcount() != static_cast<std::streamsize>(fileData.size()))
            return;

        bt.texture = create_texture_from_png(device, fileData.data(),
                                              static_cast<UINT>(fileData.size()),
                                              bt.texWidth, bt.texHeight);
    }

    // -----------------------------------------------------------------------
    // DX9 textured-quad drawing (uses the active device render state)
    // -----------------------------------------------------------------------
    struct ScreenVertex
    {
        float x, y, z, rhw;
        D3DCOLOR color;
        float u, v;
    };
    constexpr DWORD kVertexFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

    void draw_sprite_frame(LPDIRECT3DDEVICE9 device, LPDIRECT3DTEXTURE9 tex,
                           int texW, int texH, int frameIndex,
                           float dstX, float dstY, float dstW, float dstH)
    {
        if (!device || !tex || texW <= 0 || texH <= 0)
            return;

        // UV coordinates for the requested frame in the horizontal strip.
        float frameW = static_cast<float>(kFramePixels) / texW;
        float u0 = frameIndex * frameW;
        float u1 = u0 + frameW;
        float v0 = 0.0f;
        float v1 = 1.0f;

        // Half-pixel offset for DX9 texel-to-pixel alignment.
        float x0 = dstX - 0.5f;
        float y0 = dstY - 0.5f;
        float x1 = dstX + dstW - 0.5f;
        float y1 = dstY + dstH - 0.5f;

        ScreenVertex quad[4] =
        {
            { x0, y0, 0.0f, 1.0f, 0xFFFFFFFF, u0, v0 },
            { x1, y0, 0.0f, 1.0f, 0xFFFFFFFF, u1, v0 },
            { x0, y1, 0.0f, 1.0f, 0xFFFFFFFF, u0, v1 },
            { x1, y1, 0.0f, 1.0f, 0xFFFFFFFF, u1, v1 },
        };

        // Save render state we touch.
        DWORD prevFVF, prevLighting, prevCull, prevZEnable;
        DWORD prevAlphaBlend, prevSrcBlend, prevDstBlend;
        DWORD prevColorOp, prevColorArg1, prevAlphaOp, prevAlphaArg1;
        LPDIRECT3DTEXTURE9 prevTex = nullptr;

        device->GetFVF(&prevFVF);
        device->GetRenderState(D3DRS_LIGHTING, &prevLighting);
        device->GetRenderState(D3DRS_CULLMODE, &prevCull);
        device->GetRenderState(D3DRS_ZENABLE, &prevZEnable);
        device->GetRenderState(D3DRS_ALPHABLENDENABLE, &prevAlphaBlend);
        device->GetRenderState(D3DRS_SRCBLEND, &prevSrcBlend);
        device->GetRenderState(D3DRS_DESTBLEND, &prevDstBlend);
        device->GetTextureStageState(0, D3DTSS_COLOROP, &prevColorOp);
        device->GetTextureStageState(0, D3DTSS_COLORARG1, &prevColorArg1);
        device->GetTextureStageState(0, D3DTSS_ALPHAOP, &prevAlphaOp);
        device->GetTextureStageState(0, D3DTSS_ALPHAARG1, &prevAlphaArg1);
        device->GetTexture(0, reinterpret_cast<IDirect3DBaseTexture9**>(&prevTex));

        // Configure for 2D alpha-blended textured quad.
        device->SetFVF(kVertexFVF);
        device->SetTexture(0, tex);
        device->SetRenderState(D3DRS_LIGHTING, FALSE);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_ZENABLE, FALSE);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

        device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(ScreenVertex));

        // Restore previous state.
        device->SetTexture(0, prevTex);
        if (prevTex) prevTex->Release();
        device->SetFVF(prevFVF);
        device->SetRenderState(D3DRS_LIGHTING, prevLighting);
        device->SetRenderState(D3DRS_CULLMODE, prevCull);
        device->SetRenderState(D3DRS_ZENABLE, prevZEnable);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, prevAlphaBlend);
        device->SetRenderState(D3DRS_SRCBLEND, prevSrcBlend);
        device->SetRenderState(D3DRS_DESTBLEND, prevDstBlend);
        device->SetTextureStageState(0, D3DTSS_COLOROP, prevColorOp);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, prevColorArg1);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, prevAlphaOp);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, prevAlphaArg1);
    }

    // -----------------------------------------------------------------------
    // Cursor hit-test using the game's global cursor position
    // -----------------------------------------------------------------------
    bool is_cursor_in_rect(long x, long y, long width, long height)
    {
        auto cursorX = *reinterpret_cast<int*>(0x7C3C0C);
        auto cursorY = *reinterpret_cast<int*>(0x7C3C10);

        return cursorX >= x
            && cursorX < x + width
            && cursorY >= y
            && cursorY < y + height;
    }

    // -----------------------------------------------------------------------
    // Action dispatch
    // -----------------------------------------------------------------------
    void open_legacy_inventory_npc(int npcId)
    {
        *reinterpret_cast<int*>(0x91AD44) = 0x1;
        *reinterpret_cast<int*>(0x91AD40) = 0x12C;
        *reinterpret_cast<int*>(0x9144F0) = -1;
        *reinterpret_cast<int*>(0x22AB7B8) = 0x0;
        *reinterpret_cast<int*>(0x9144E4) = npcId;
    }

    void trigger_button(const InventoryNpcButton& button)
    {
        switch (button.actionType)
        {
        case ButtonActionType::LegacyNpcId:
            open_legacy_inventory_npc(button.actionValue);
            break;
        case ButtonActionType::RemoteNpc:
            open_remote_npc(static_cast<RemoteNpcAction>(button.actionValue));
            break;
        }
    }

    // -----------------------------------------------------------------------
    // Draw buttons + handle clicks (called every inventory render frame)
    // -----------------------------------------------------------------------
    void draw_and_handle_buttons(long baseX, long baseY)
    {
        scan_sah_for_button_textures();

        auto* device = g_var ? g_var->device : nullptr;
        auto leftMouseDown = (GetKeyState(VK_LBUTTON) < 0);
        auto clicked = leftMouseDown && !g_leftMouseWasDown;
        g_leftMouseWasDown = leftMouseDown;

        for (int i = 0; i < kButtonCount; ++i)
        {
            const auto& btn = kButtons[i];
            auto x = baseX + btn.xStart;
            auto y = baseY + btn.yStart;

            // Load texture on first use.
            if (device)
                ensure_button_texture_loaded(i, device);

            bool hovered = is_cursor_in_rect(x, y, kButtonSize, kButtonSize);
            bool pressed = hovered && leftMouseDown;

            // Pick sprite frame: pressed > hovered > normal.
            int frame = kFrameNormal;
            if (pressed)       frame = kFramePressed;
            else if (hovered)  frame = kFrameHover;

            // Draw the button texture if available.
            auto& bt = g_btnTex[i];
            if (bt.texture && device)
            {
                draw_sprite_frame(device, bt.texture,
                                  bt.texWidth, bt.texHeight, frame,
                                  static_cast<float>(x), static_cast<float>(y),
                                  static_cast<float>(kButtonSize),
                                  static_cast<float>(kButtonSize));
            }
            else if (hovered)
            {
                // Fallback: transparent hover rect when texture is missing.
                constexpr D3DCOLOR hoverColor = 0x30FFFFFF;
                Static::DrawRect(hoverColor, x + 2, y,              kButtonSize - 4, 1);
                Static::DrawRect(hoverColor, x + 1, y + 1,          kButtonSize - 2, 1);
                Static::DrawRect(hoverColor, x,     y + 2,          kButtonSize,     kButtonSize - 4);
                Static::DrawRect(hoverColor, x + 1, y + kButtonSize - 2, kButtonSize - 2, 1);
                Static::DrawRect(hoverColor, x + 2, y + kButtonSize - 1, kButtonSize - 4, 1);
            }

            if (hovered && clicked)
            {
                trigger_button(btn);
                return;
            }
        }
    }
}

// ===========================================================================
// Naked detour — bridges the inventory render path to our draw handler
// ===========================================================================
void __declspec(naked) naked_inventory_npc_buttons()
{
    __asm
    {
        pushad
        pushfd

        mov eax, [esi + 0x8]   // baseY
        push eax
        mov ecx, [esi + 0x4]   // baseX
        push ecx
        call draw_and_handle_buttons
        add esp, 0x8

        popfd
        popad
        jmp u0x518220
    }
}

// ===========================================================================
// hook::npc_button — public entry point
// ===========================================================================
void hook::npc_button()
{
    // Inventory NPC shortcut buttons (detour into the inventory render path).
    util::detour((void*)0x518215, naked_inventory_npc_buttons, 9);
}
