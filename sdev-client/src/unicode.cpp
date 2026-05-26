#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <util/util.h>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <unordered_set>
#include "include/game_addresses.h"
#include "include/main.h"
#include "include/shaiya/CDataFile.h"
#include "include/shaiya/ItemInfo.h"
#include "include/shaiya/MobInfo.h"
#include "include/shaiya/ProductInfo.h"
#include "include/shaiya/SkillInfo.h"

// Master toggle for the entire Unicode/UTF-8 feature.
// Set to false to completely disable Unicode window creation, UTF-8 textbox
// input, and all related patches. The client will behave as stock ANSI.
constexpr bool kEnableUnicode = false;

namespace
{
    constexpr std::int32_t kUtf8CodePage = CP_UTF8;
    constexpr std::int32_t kSDataLegacyCodePage = 1252;
    constexpr bool kEnableUnicodeWindowTitleFix = true;
    constexpr bool kEnableSDataLegacyTextToUtf8 = true;
    constexpr int kMaxSDataItemType = 150;
    constexpr int kMaxSDataItemTypeId = 2048;
    constexpr auto kCDataFileAddress = game_addr::CDataFile;
    constexpr auto kNpcSkillDataAddress = game_addr::NpcSkillDataFile;
    constexpr auto kNpcQuestDataAddress = game_addr::NpcQuestData;
    constexpr auto kCashProductTableAddress = game_addr::CashProductTable;
    constexpr auto kCashProductCountAddress = game_addr::CashProductCount;
    constexpr auto kGameAllocatorAddress = game_addr::GameAllocator;
    constexpr int kMinSDataSkillLevel = 1;
    constexpr int kMaxSDataSkillLevel = 15;
    constexpr int kNpcQuestGroupCount = 11;
    constexpr int kNpcQuestResultTextCount = 3;
    constexpr int kNpcQuestExtraActionTextCount = 6;
    constexpr int kNpcQuestExtraRewardTextCount = 4;
    constexpr int kNpcQuestFixedTextGridSide = 256;
    constexpr int kNpcQuestFixedTextRecordSize = 0x198;
    constexpr int kNpcQuestExtraRecordSize = 0x180;
    constexpr wchar_t kDefaultGameWindowTitle[] = L"Shaiya";
    inline HWND g_gameWindowHwnd = nullptr;
    constexpr char kGameWindowClassName[] = "GAME";

    using RegisterClassExAProc = ATOM(WINAPI*)(const WNDCLASSEXA*);
    using CreateWindowExAProc = HWND(WINAPI*)(
        DWORD,
        LPCSTR,
        LPCSTR,
        DWORD,
        int,
        int,
        int,
        int,
        HWND,
        HMENU,
        HINSTANCE,
        LPVOID);
    using RegisterClassExWProc = ATOM(WINAPI*)(const WNDCLASSEXW*);
    using CreateWindowExWProc = HWND(WINAPI*)(
        DWORD,
        LPCWSTR,
        LPCWSTR,
        DWORD,
        int,
        int,
        int,
        int,
        HWND,
        HMENU,
        HINSTANCE,
        LPVOID);
    using SetWindowTextAProc = BOOL(WINAPI*)(HWND, LPCSTR);
    using SendMessageAProc = LRESULT(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
    using GameAllocProc = void* (__cdecl*)(std::size_t);

    RegisterClassExAProc g_originalRegisterClassExA = nullptr;
    CreateWindowExAProc g_originalCreateWindowExA = nullptr;
    RegisterClassExWProc g_originalRegisterClassExW = nullptr;
    CreateWindowExWProc g_originalCreateWindowExW = nullptr;
    SetWindowTextAProc g_originalSetWindowTextA = nullptr;
    SendMessageAProc g_originalSendMessageA = nullptr;

    // SData string UTF-8 support
    //
    // Game.exe can now render UTF-8 text, but several SData loaders still copy
    // legacy single-byte strings into runtime structs. Bytes such as CP1252
    // 0xF1 are invalid UTF-8, so the UTF-8-aware renderer shows '?' even though
    // the source data is valid. After the native loaders finish, normalize only
    // loaded SData text fields that contain non-ASCII legacy bytes.
    //
    // Already-valid UTF-8 is left untouched, so rebuilt SData files can move to
    // UTF-8 naturally without requiring a second conversion path.
    bool has_non_ascii_byte(const char* text)
    {
        if (!text)
            return false;

        for (auto cursor = reinterpret_cast<const unsigned char*>(text); *cursor; ++cursor)
        {
            if (*cursor >= 0x80)
                return true;
        }

        return false;
    }

    bool is_valid_utf8(const char* text)
    {
        if (!text)
            return true;

        auto count = MultiByteToWideChar(kUtf8CodePage, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
        return count > 0;
    }

    char* duplicate_process_string(const char* text, int byteCount)
    {
        if (!text || byteCount <= 0)
            return nullptr;

        // SData destructors free these pointers through the game's allocator,
        // so replacement strings must be allocated through the same path.
        auto gameAlloc = reinterpret_cast<GameAllocProc>(kGameAllocatorAddress);
        auto* memory = static_cast<char*>(gameAlloc(static_cast<std::size_t>(byteCount)));
        if (!memory)
            return nullptr;

        std::memcpy(memory, text, static_cast<std::size_t>(byteCount));
        return memory;
    }

    bool convert_legacy_sdata_string_to_utf8(char*& text)
    {
        if (!text || !has_non_ascii_byte(text) || is_valid_utf8(text))
            return false;

        auto wideCount = MultiByteToWideChar(kSDataLegacyCodePage, 0, text, -1, nullptr, 0);
        if (wideCount <= 0)
            return false;

        std::wstring wideText(static_cast<std::size_t>(wideCount), L'\0');
        if (MultiByteToWideChar(kSDataLegacyCodePage, 0, text, -1, wideText.data(), wideCount) <= 0)
            return false;

        auto utf8Count = WideCharToMultiByte(kUtf8CodePage, 0, wideText.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (utf8Count <= 0)
            return false;

        std::string utf8Text(static_cast<std::size_t>(utf8Count), '\0');
        if (WideCharToMultiByte(kUtf8CodePage, 0, wideText.c_str(), -1, utf8Text.data(), utf8Count, nullptr, nullptr) <= 0)
            return false;

        auto* replacement = duplicate_process_string(utf8Text.c_str(), utf8Count);
        if (!replacement)
            return false;

        text = replacement;
        return true;
    }

    bool decode_text_bytes_to_wide(const char* bytes, int byteCount, std::wstring& wideText)
    {
        if (!bytes || byteCount <= 0)
            return false;

        auto codePage = kUtf8CodePage;
        auto wideCount = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, bytes, byteCount, nullptr, 0);
        if (wideCount <= 0)
        {
            codePage = kSDataLegacyCodePage;
            wideCount = MultiByteToWideChar(codePage, 0, bytes, byteCount, nullptr, 0);
        }

        if (wideCount <= 0)
            return false;

        wideText.assign(static_cast<std::size_t>(wideCount), L'\0');
        auto flags = codePage == kUtf8CodePage ? MB_ERR_INVALID_CHARS : 0;
        if (MultiByteToWideChar(codePage, flags, bytes, byteCount, wideText.data(), wideCount) <= 0)
            return false;

        return true;
    }

    void convert_legacy_sdata_string_field(std::uint8_t* record, std::ptrdiff_t offset)
    {
        if (!record)
            return;

        auto& text = *reinterpret_cast<char**>(record + offset);
        convert_legacy_sdata_string_to_utf8(text);
    }

    void normalize_counted_wide_byte_text(std::uint8_t* record, std::ptrdiff_t countOffset, std::ptrdiff_t textOffset, std::uint32_t maxUnits)
    {
        if (!record)
            return;

        // NpcQuest.SData has fixed-size buffers copied as 2-byte units, but
        // each unit still contains one original file byte in the low 8 bits.
        // Convert those byte streams to real UTF-16 in-place for native wide UI.
        auto& count = *reinterpret_cast<std::uint32_t*>(record + countOffset);
        if (count == 0 || count > maxUnits)
            return;

        auto* units = reinterpret_cast<wchar_t*>(record + textOffset);
        std::string bytes;
        bytes.reserve(count);
        bool hasHighByte = false;

        for (std::uint32_t i = 0; i < count; ++i)
        {
            auto value = units[i];
            if (value > 0x00FF)
                return;

            auto byte = static_cast<char>(value & 0x00FF);
            bytes.push_back(byte);
            hasHighByte = hasHighByte || (static_cast<unsigned char>(byte) >= 0x80);
        }

        if (!hasHighByte)
            return;

        std::wstring wideText;
        if (!decode_text_bytes_to_wide(bytes.data(), static_cast<int>(bytes.size()), wideText))
            return;

        if (wideText.empty() || wideText.size() > count)
            return;

        std::memset(units, 0, static_cast<std::size_t>(count) * sizeof(wchar_t));
        std::memcpy(units, wideText.data(), wideText.size() * sizeof(wchar_t));
        count = static_cast<std::uint32_t>(wideText.size());
    }

    void normalize_sdata_item_text_to_utf8()
    {
        std::unordered_set<shaiya::ItemInfo*> seen;

        // Item.SData: visible item name and item description.
        for (int type = 1; type <= kMaxSDataItemType; ++type)
        {
            for (int typeId = 0; typeId < kMaxSDataItemTypeId; ++typeId)
            {
                auto* info = shaiya::CDataFile::GetItemInfo(type, typeId);
                if (!info || !seen.insert(info).second)
                    continue;

                convert_legacy_sdata_string_to_utf8(info->name);
                convert_legacy_sdata_string_to_utf8(info->description);
            }
        }
    }

    void normalize_sdata_monster_text_to_utf8()
    {
        auto maxMobId = *reinterpret_cast<const std::uint16_t*>(kCDataFileAddress + 0x14);

        // Monster.SData: only the monster name is visible text in MobInfo.
        for (int mobId = 0; mobId < maxMobId; ++mobId)
        {
            auto* info = shaiya::CDataFile::GetMobInfo(mobId);
            if (!info)
                continue;

            convert_legacy_sdata_string_to_utf8(info->name);
        }
    }

    void normalize_sdata_skill_text_to_utf8_at(std::uintptr_t dataFileAddress)
    {
        std::unordered_set<shaiya::SkillInfo*> seen;
        auto maxSkillId = *reinterpret_cast<const std::uint16_t*>(dataFileAddress + 0x1C);
        auto skillTable = *reinterpret_cast<std::uint8_t***>(dataFileAddress + 0x18);
        if (!skillTable)
            return;

        // Skill.SData/NpcSkill.SData: skill name and description. Both use the
        // same native loader and record layout, but player skills live in the
        // main CDataFile instance while NPC skills live in a second one.
        for (int skillId = 1; skillId <= maxSkillId; ++skillId)
        {
            auto* levelTable = skillTable[skillId - 1];
            if (!levelTable)
                continue;

            for (int skillLv = kMinSDataSkillLevel; skillLv <= kMaxSDataSkillLevel; ++skillLv)
            {
                auto* info = reinterpret_cast<shaiya::SkillInfo*>(levelTable + (skillLv - 1) * sizeof(shaiya::SkillInfo));
                if (!info || !seen.insert(info).second)
                    continue;

                convert_legacy_sdata_string_to_utf8(info->name);
                convert_legacy_sdata_string_to_utf8(info->description);
            }
        }
    }

    void normalize_sdata_skill_text_to_utf8()
    {
        normalize_sdata_skill_text_to_utf8_at(kCDataFileAddress);
        normalize_sdata_skill_text_to_utf8_at(kNpcSkillDataAddress);
    }

    shaiya::ProductInfo* cash_product_table()
    {
        return *reinterpret_cast<shaiya::ProductInfo**>(kCashProductTableAddress);
    }

    std::uint16_t cash_product_count()
    {
        return *reinterpret_cast<const std::uint16_t*>(kCashProductCountAddress);
    }

    void normalize_sdata_cash_text_to_utf8()
    {
        auto* products = cash_product_table();
        auto count = cash_product_count();
        if (!products || !count)
            return;

        // Cash.SData: product name, description, and product code strings. The
        // cash UI can read different fields depending on the panel, so normalize
        // all loader-owned strings and leave numeric metadata alone.
        for (std::uint16_t i = 0; i < count; ++i)
        {
            convert_legacy_sdata_string_to_utf8(products[i].productName);
            convert_legacy_sdata_string_to_utf8(products[i].description);
            convert_legacy_sdata_string_to_utf8(products[i].productCode);
        }
    }

    void normalize_npc_quest_record_284(std::uint8_t* record)
    {
        convert_legacy_sdata_string_field(record, 0x18);
        convert_legacy_sdata_string_field(record, 0x1C);
        normalize_counted_wide_byte_text(record, 0x0EC, 0x0F0, 100);
        normalize_counted_wide_byte_text(record, 0x1B8, 0x1BC, 100);
    }

    void normalize_npc_quest_record_1fc(std::uint8_t* record)
    {
        convert_legacy_sdata_string_field(record, 0x14);
        convert_legacy_sdata_string_field(record, 0x18);

        for (int i = 0; i < kNpcQuestResultTextCount; ++i)
            convert_legacy_sdata_string_field(record, 0x2C + i * 0x18);

        normalize_counted_wide_byte_text(record, 0x064, 0x068, 100);
        normalize_counted_wide_byte_text(record, 0x130, 0x134, 100);
    }

    void normalize_npc_quest_record_1b4(std::uint8_t* record)
    {
        convert_legacy_sdata_string_field(record, 0x14);
        convert_legacy_sdata_string_field(record, 0x18);
        normalize_counted_wide_byte_text(record, 0x01C, 0x020, 100);
        normalize_counted_wide_byte_text(record, 0x0E8, 0x0EC, 100);
    }

    void normalize_npc_quest_fixed_text_record(std::uint8_t* record)
    {
        normalize_counted_wide_byte_text(record, 0x000, 0x004, 100);
        normalize_counted_wide_byte_text(record, 0x0CC, 0x0D0, 100);
    }

    void normalize_npc_quest_extra_record(std::uint8_t* record)
    {
        convert_legacy_sdata_string_field(record, 0x004);
        convert_legacy_sdata_string_field(record, 0x008);

        for (int i = 0; i < kNpcQuestExtraActionTextCount; ++i)
            convert_legacy_sdata_string_field(record, 0x090 + i * 0x2C);

        for (int i = 0; i < kNpcQuestExtraRewardTextCount; ++i)
            convert_legacy_sdata_string_field(record, 0x170 + i * sizeof(void*));
    }

    void normalize_sdata_npc_quest_text_to_utf8()
    {
        auto base = reinterpret_cast<std::uint8_t*>(kNpcQuestDataAddress);

        // NpcQuest.SData: NPC names, quest labels, quest descriptions, objective
        // text, result strings, reward strings, and the fixed dialog matrix.
        // CNPCFile::Load owns several differently-sized tables; the offsets
        // below mirror that loader. Some fields are allocated as byte strings,
        // while long quest/dialog text is stored in counted 2-byte buffers.
        auto count284 = *reinterpret_cast<const std::uint32_t*>(base + 0x04);
        auto table284 = *reinterpret_cast<std::uint8_t**>(base + 0x08);
        if (table284)
        {
            for (std::uint32_t i = 0; i < count284; ++i)
                normalize_npc_quest_record_284(table284 + i * 0x284);
        }

        auto count1fc = *reinterpret_cast<const std::uint32_t*>(base + 0x0C);
        auto table1fc = *reinterpret_cast<std::uint8_t**>(base + 0x10);
        if (table1fc)
        {
            for (std::uint32_t i = 0; i < count1fc; ++i)
                normalize_npc_quest_record_1fc(table1fc + i * 0x1FC);
        }

        for (int group = 0; group < kNpcQuestGroupCount; ++group)
        {
            auto count = *reinterpret_cast<const std::uint32_t*>(base + 0x14 + group * sizeof(std::uint32_t));
            auto table = *reinterpret_cast<std::uint8_t**>(base + 0x4C + group * sizeof(void*));
            if (!table)
                continue;

            for (std::uint32_t i = 0; i < count; ++i)
                normalize_npc_quest_record_1b4(table + i * 0x1B4);
        }

        auto* fixedText = base + 0x8C;
        for (int i = 0; i < kNpcQuestFixedTextGridSide * kNpcQuestFixedTextGridSide; ++i)
            normalize_npc_quest_fixed_text_record(fixedText + i * kNpcQuestFixedTextRecordSize);

        // The tail of NpcQuest.SData is delegated by the native loader to an
        // internal table builder at 0x4896C0. It stores additional quest-facing
        // labels, descriptions, and reward/action strings at +0x84/+0x88.
        auto extraCount = *reinterpret_cast<const std::uint32_t*>(base + 0x84);
        auto extraTable = *reinterpret_cast<std::uint8_t**>(base + 0x88);
        if (extraTable)
        {
            for (std::uint32_t i = 0; i < extraCount; ++i)
                normalize_npc_quest_extra_record(extraTable + i * kNpcQuestExtraRecordSize);
        }
    }

    void normalize_sdata_text_to_utf8()
    {
        if (!kEnableSDataLegacyTextToUtf8)
            return;

        normalize_sdata_item_text_to_utf8();
        normalize_sdata_monster_text_to_utf8();
        normalize_sdata_skill_text_to_utf8();
        normalize_sdata_cash_text_to_utf8();
        normalize_sdata_npc_quest_text_to_utf8();
    }

    bool is_sdata_text_ready()
    {
        if (!shaiya::CDataFile::GetItemInfo(1, 1))
            return false;

        if (!cash_product_table() || cash_product_count() == 0)
            return false;

        if (!shaiya::CDataFile::GetMobInfo(0))
            return false;

        if (*reinterpret_cast<const std::uint32_t*>(kNpcQuestDataAddress + 0x04) == 0
            || !*reinterpret_cast<void**>(kNpcQuestDataAddress + 0x08))
            return false;

        auto playerMaxSkillId = *reinterpret_cast<const std::uint16_t*>(kCDataFileAddress + 0x1C);
        auto playerSkillTable = *reinterpret_cast<void**>(kCDataFileAddress + 0x18);
        auto npcMaxSkillId = *reinterpret_cast<const std::uint16_t*>(kNpcSkillDataAddress + 0x1C);
        auto npcSkillTable = *reinterpret_cast<void**>(kNpcSkillDataAddress + 0x18);
        if (!playerMaxSkillId || !playerSkillTable || !npcMaxSkillId || !npcSkillTable)
            return false;

        for (int skillId = 1; skillId <= playerMaxSkillId; ++skillId)
        {
            for (int skillLv = kMinSDataSkillLevel; skillLv <= kMaxSDataSkillLevel; ++skillLv)
            {
                if (shaiya::CDataFile::GetSkillInfo(skillId, skillLv))
                    return true;
            }
        }

        return false;
    }

    DWORD WINAPI sdata_text_normalizer_thread(LPVOID)
    {
        for (int attempts = 0; attempts < 600; ++attempts)
        {
            if (is_sdata_text_ready())
            {
                Sleep(1000);
                normalize_sdata_text_to_utf8();
                return 0;
            }

            Sleep(100);
        }

        return 0;
    }

    bool is_string_game_window_class(LPCSTR value)
    {
        return value
            && HIWORD(reinterpret_cast<std::uintptr_t>(value)) != 0
            && lstrcmpiA(value, kGameWindowClassName) == 0;
    }

    bool convert_ansi_to_wide(LPCSTR source, wchar_t* destination, int destinationCount)
    {
        if (!source || HIWORD(reinterpret_cast<std::uintptr_t>(source)) == 0 || !destination || destinationCount <= 0)
            return false;

        auto count = MultiByteToWideChar(CP_ACP, 0, source, -1, destination, destinationCount);
        if (count <= 0)
        {
            destination[0] = L'\0';
            return false;
        }

        destination[destinationCount - 1] = L'\0';
        return true;
    }

    bool convert_title_to_wide(LPCSTR source, wchar_t* destination, int destinationCount)
    {
        if (!source || HIWORD(reinterpret_cast<std::uintptr_t>(source)) == 0 || !destination || destinationCount <= 0)
            return false;

        // Some Shaiya builds pass a UTF-16LE title pointer through an ANSI API
        // slot. Read as UTF-16 when the byte pattern is S\0h\0...; otherwise
        // the ANSI conversion stops at the first null byte and the title
        // collapses to just "S".
        __try
        {
            if (source[0] != '\0' && source[1] == '\0' && source[2] != '\0' && source[3] == '\0')
            {
                lstrcpynW(destination, reinterpret_cast<LPCWSTR>(source), destinationCount);
                destination[destinationCount - 1] = L'\0';
                return destination[0] != L'\0';
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        // Window titles coming from external/custom code may already be UTF-8,
        // while the vanilla title is ANSI/ACP. Try strict UTF-8 first and fall
        // back to ACP so the fix remains safe for the stock "Shaiya" title.
        auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, destination, destinationCount);
        if (count <= 0)
            count = MultiByteToWideChar(CP_ACP, 0, source, -1, destination, destinationCount);

        if (count <= 0)
        {
            destination[0] = L'\0';
            return false;
        }

        destination[destinationCount - 1] = L'\0';
        return true;
    }

    bool is_string_game_window_class(LPCWSTR value)
    {
        return value
            && HIWORD(reinterpret_cast<std::uintptr_t>(value)) != 0
            && lstrcmpiW(value, L"GAME") == 0;
    }

    bool is_game_window_hwnd(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd))
            return false;

        if (g_gameWindowHwnd && hwnd == g_gameWindowHwnd)
            return true;

        if (!IsWindowUnicode(hwnd))
            return false;

        wchar_t className[64]{};
        if (!GetClassNameW(hwnd, className, _countof(className)))
            return false;

        return is_string_game_window_class(className);
    }

    BOOL WINAPI set_window_text_a_utf8_safe(HWND hwnd, LPCSTR text)
    {
        if (!kEnableUnicodeWindowTitleFix || !is_game_window_hwnd(hwnd))
            return g_originalSetWindowTextA(hwnd, text);

        wchar_t wideTitle[256]{};
        if (!convert_title_to_wide(text, wideTitle, _countof(wideTitle)))
            return g_originalSetWindowTextA(hwnd, text);

        return SetWindowTextW(hwnd, wideTitle);
    }

    void force_game_window_title(HWND hwnd)
    {
        if (!kEnableUnicodeWindowTitleFix || !is_game_window_hwnd(hwnd))
            return;

        SetWindowTextW(hwnd, kDefaultGameWindowTitle);
    }

    LRESULT WINAPI send_message_a_utf8_safe(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (!kEnableUnicodeWindowTitleFix || message != WM_SETTEXT || !is_game_window_hwnd(hwnd))
            return g_originalSendMessageA(hwnd, message, wParam, lParam);

        wchar_t wideTitle[256]{};
        if (!convert_title_to_wide(reinterpret_cast<LPCSTR>(lParam), wideTitle, _countof(wideTitle)))
            lstrcpynW(wideTitle, kDefaultGameWindowTitle, _countof(wideTitle));

        return SendMessageW(hwnd, WM_SETTEXT, wParam, reinterpret_cast<LPARAM>(wideTitle));
    }

    ATOM WINAPI register_class_ex_a_utf8_safe(const WNDCLASSEXA* wndClass)
    {
        if (!wndClass || !is_string_game_window_class(wndClass->lpszClassName))
            return g_originalRegisterClassExA(wndClass);

        wchar_t className[64]{};
        wchar_t menuName[260]{};
        convert_ansi_to_wide(wndClass->lpszClassName, className, _countof(className));

        WNDCLASSEXW wideClass{};
        wideClass.cbSize = sizeof(wideClass);
        wideClass.style = wndClass->style;
        wideClass.lpfnWndProc = wndClass->lpfnWndProc;
        wideClass.cbClsExtra = wndClass->cbClsExtra;
        wideClass.cbWndExtra = wndClass->cbWndExtra;
        wideClass.hInstance = wndClass->hInstance;
        wideClass.hIcon = wndClass->hIcon;
        wideClass.hCursor = wndClass->hCursor;
        wideClass.hbrBackground = wndClass->hbrBackground;
        wideClass.lpszClassName = className;
        wideClass.hIconSm = wndClass->hIconSm;

        if (wndClass->lpszMenuName && HIWORD(reinterpret_cast<std::uintptr_t>(wndClass->lpszMenuName)) == 0)
            wideClass.lpszMenuName = MAKEINTRESOURCEW(reinterpret_cast<std::uintptr_t>(wndClass->lpszMenuName) & 0xFFFF);
        else if (convert_ansi_to_wide(wndClass->lpszMenuName, menuName, _countof(menuName)))
            wideClass.lpszMenuName = menuName;

        return RegisterClassExW(&wideClass);
    }

    HWND WINAPI create_window_ex_a_utf8_safe(
        DWORD exStyle,
        LPCSTR className,
        LPCSTR windowName,
        DWORD style,
        int x,
        int y,
        int width,
        int height,
        HWND parent,
        HMENU menu,
        HINSTANCE instance,
        LPVOID param)
    {
        if (!is_string_game_window_class(className))
        {
            return g_originalCreateWindowExA(
                exStyle,
                className,
                windowName,
                style,
                x,
                y,
                width,
                height,
                parent,
                menu,
                instance,
                param);
        }

        wchar_t wideClassName[64]{};
        wchar_t wideWindowName[128]{};
        convert_ansi_to_wide(className, wideClassName, _countof(wideClassName));
        convert_title_to_wide(windowName, wideWindowName, _countof(wideWindowName));

        auto hwnd = CreateWindowExW(
            exStyle,
            wideClassName,
            wideWindowName,
            style,
            x,
            y,
            width,
            height,
            parent,
            menu,
            instance,
            param);

        if (hwnd)
            g_gameWindowHwnd = hwnd;
        force_game_window_title(hwnd);
        return hwnd;
    }

    ATOM WINAPI register_class_ex_w_utf8_safe(const WNDCLASSEXW* wndClass)
    {
        return g_originalRegisterClassExW(wndClass);
    }

    HWND WINAPI create_window_ex_w_utf8_safe(
        DWORD exStyle,
        LPCWSTR className,
        LPCWSTR windowName,
        DWORD style,
        int x,
        int y,
        int width,
        int height,
        HWND parent,
        HMENU menu,
        HINSTANCE instance,
        LPVOID param)
    {
        auto hwnd = g_originalCreateWindowExW(
            exStyle,
            className,
            windowName,
            style,
            x,
            y,
            width,
            height,
            parent,
            menu,
            instance,
            param);

        if (is_string_game_window_class(className))
        {
            if (hwnd)
                g_gameWindowHwnd = hwnd;
            force_game_window_title(hwnd);
        }

        return hwnd;
    }

    void patch_game_window_ansi_creation_to_unicode()
    {
        auto registerClassExAImport = reinterpret_cast<RegisterClassExAProc*>(0x746450);
        auto createWindowExAImport = reinterpret_cast<CreateWindowExAProc*>(0x74644C);
        auto registerClassExWImport = reinterpret_cast<RegisterClassExWProc*>(0x746470);
        auto createWindowExWImport = reinterpret_cast<CreateWindowExWProc*>(0x74646C);
        auto setWindowTextAImport = reinterpret_cast<SetWindowTextAProc*>(0x746494);
        auto sendMessageAImport = reinterpret_cast<SendMessageAProc*>(0x746504);

        if (!g_originalRegisterClassExA)
            g_originalRegisterClassExA = *registerClassExAImport;
        if (!g_originalCreateWindowExA)
            g_originalCreateWindowExA = *createWindowExAImport;
        if (!g_originalRegisterClassExW)
            g_originalRegisterClassExW = *registerClassExWImport;
        if (!g_originalCreateWindowExW)
            g_originalCreateWindowExW = *createWindowExWImport;
        if (!g_originalSetWindowTextA)
            g_originalSetWindowTextA = *setWindowTextAImport;
        if (!g_originalSendMessageA)
            g_originalSendMessageA = *sendMessageAImport;

        auto registerClassExAReplacement = register_class_ex_a_utf8_safe;
        auto createWindowExAReplacement = create_window_ex_a_utf8_safe;
        auto registerClassExWReplacement = register_class_ex_w_utf8_safe;
        auto createWindowExWReplacement = create_window_ex_w_utf8_safe;
        auto setWindowTextAReplacement = set_window_text_a_utf8_safe;
        auto sendMessageAReplacement = send_message_a_utf8_safe;

        // Keep this scoped to the main "GAME" window in the wrappers above.
        // Other ANSI helper windows still flow through the original imports.
        util::write_memory(registerClassExAImport, &registerClassExAReplacement, sizeof(registerClassExAReplacement));
        util::write_memory(createWindowExAImport, &createWindowExAReplacement, sizeof(createWindowExAReplacement));
        util::write_memory(registerClassExWImport, &registerClassExWReplacement, sizeof(registerClassExWReplacement));
        util::write_memory(createWindowExWImport, &createWindowExWReplacement, sizeof(createWindowExWReplacement));
        // Window title fix for the Unicode GAME HWND. Some legacy paths still
        // call SetWindowTextA after the HWND was upgraded to Unicode, which can
        // collapse "Shaiya" to "S". Convert only GAME window titles to UTF-16.
        util::write_memory(setWindowTextAImport, &setWindowTextAReplacement, sizeof(setWindowTextAReplacement));
        util::write_memory(sendMessageAImport, &sendMessageAReplacement, sizeof(sendMessageAReplacement));

        // If the class was registered through the ANSI route before this patch
        // ran, remove it while no game HWND exists so the next registration can
        // be Unicode. Failure is harmless when the class is not registered yet.
        auto instance = GetModuleHandleA(nullptr);
        UnregisterClassA(kGameWindowClassName, instance);
    }

    bool append_utf8_textbox_bytes(void* textBox, const char* utf8, int count)
    {
        if (!textBox || !utf8 || count <= 0)
            return false;

        auto base = reinterpret_cast<std::uint8_t*>(textBox);
        auto maxTextLength = *reinterpret_cast<std::uint32_t*>(base + 0x30);
        auto textSize = *reinterpret_cast<std::uint32_t*>(base + 0xC0);
        auto textCapacity = *reinterpret_cast<std::uint32_t*>(base + 0xC4);
        if (textSize >= 2048 || textCapacity >= 0x100000)
            return false;

        auto textObject = base + 0xAC;
        auto text = textCapacity >= 0x10
            ? *reinterpret_cast<const char**>(textObject + 0x04)
            : reinterpret_cast<const char*>(textObject + 0x04);
        if (!text)
            return false;

        auto nextSize = textSize + static_cast<std::uint32_t>(count);
        if (nextSize >= 2048 || (maxTextLength && nextSize > maxTextLength))
            return false;

        char merged[2048]{};
        if (textSize)
            std::memcpy(merged, text, textSize);
        std::memcpy(merged + textSize, utf8, count);

        using StringAssign = void*(__thiscall*)(void*, const char*, std::uint32_t);
        auto assignString = reinterpret_cast<StringAssign>(0x405670);

        // Avoid the client's ANSI-per-character path for Unicode input. Feed the
        // completed UTF-8 byte sequence into the textbox's narrow string at once;
        // then mirror it into the wide string for rendering in sync_textbox_utf8_display().
        assignString(textObject, merged, nextSize);
        sync_textbox_utf8_display(textBox);
        return true;
    }
}

void __stdcall insert_utf8_textbox_char(void* textBox, int wParam, int /*lParam*/)
{
    append_utf8_textbox_wchar(textBox, static_cast<wchar_t>(wParam));
}

bool append_utf8_textbox_wchar(void* textBox, wchar_t wideChar)
{
    char utf8[8]{};
    auto count = WideCharToMultiByte(kUtf8CodePage, 0, &wideChar, 1, utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
    if (count <= 0)
        return false;

    return append_utf8_textbox_bytes(textBox, utf8, count);
}

bool append_utf8_textbox_text(void* textBox, const char* utf8)
{
    if (!utf8)
        return false;

    auto count = static_cast<int>(std::strlen(utf8));
    if (count <= 0)
        return false;

    return append_utf8_textbox_bytes(textBox, utf8, count);
}

bool backspace_utf8_textbox_char(void* textBox)
{
    if (!textBox)
        return false;

    auto base = reinterpret_cast<std::uint8_t*>(textBox);
    auto textSize = *reinterpret_cast<std::uint32_t*>(base + 0xC0);
    auto textCapacity = *reinterpret_cast<std::uint32_t*>(base + 0xC4);
    if (!textSize || textSize >= 2048 || textCapacity >= 0x100000)
        return false;

    auto textObject = base + 0xAC;
    auto text = textCapacity >= 0x10
        ? *reinterpret_cast<const char**>(textObject + 0x04)
        : reinterpret_cast<const char*>(textObject + 0x04);
    if (!text)
        return false;

    auto last = static_cast<unsigned char>(text[textSize - 1]);
    if (last < 0x80)
        return false;

    auto nextSize = textSize - 1;
    while (nextSize > 0 && (static_cast<unsigned char>(text[nextSize]) & 0xC0) == 0x80)
        --nextSize;

    using StringAssign = void*(__thiscall*)(void*, const char*, std::uint32_t);
    auto assignString = reinterpret_cast<StringAssign>(0x405670);

    // Unikey/IME can revise a composed Vietnamese glyph by sending backspace
    // before the final WM_CHAR. The stock ANSI handler removes only one byte,
    // leaving invalid UTF-8 that renders as the white replacement-glyph box.
    assignString(textObject, text, nextSize);
    sync_textbox_utf8_display(textBox);
    return true;
}

void sync_textbox_utf8_display(void* textBox)
{
    if (!textBox)
        return;

    auto base = reinterpret_cast<std::uint8_t*>(textBox);
    auto textSize = *reinterpret_cast<std::uint32_t*>(base + 0xC0);
    auto textCapacity = *reinterpret_cast<std::uint32_t*>(base + 0xC4);
    if (textSize >= 2048 || textCapacity >= 0x100000)
        return;

    auto textObject = base + 0xAC;
    auto text = textCapacity >= 0x10
        ? *reinterpret_cast<const char**>(textObject + 0x04)
        : reinterpret_cast<const char*>(textObject + 0x04);
    if (!text)
        return;

    wchar_t wideText[1024]{};
    auto wideCount = MultiByteToWideChar(kUtf8CodePage, 0, text, static_cast<int>(textSize), wideText, static_cast<int>(std::size(wideText) - 1));
    if (wideCount <= 0)
        return;

    wideText[wideCount] = L'\0';

    using WideTextAssign = void(__thiscall*)(void*, const wchar_t*, int);
    using TextboxRefresh = void(__thiscall*)(void*, int);
    auto assignWideText = reinterpret_cast<WideTextAssign>(0x424FA0);
    auto refreshTextbox = reinterpret_cast<TextboxRefresh>(0x565470);

    // The client keeps both a narrow string (+0xAC) and a wide string (+0x90).
    // The normal key handler updates the narrow side; this mirrors it into the
    // wide side as UTF-8 so chat can render characters without enabling the
    // global Vietnam codepage branch.
    assignWideText(base + 0x90, wideText, wideCount);
    refreshTextbox(textBox, 0);
}

unsigned u0x567703 = 0x567703;
unsigned u0x56771C = 0x56771C;
unsigned u0x56772D = 0x56772D;
unsigned u0x5648D0 = 0x5648D0;
unsigned u0x564FC6 = 0x564FC6;

void __declspec(naked) naked_0x564FC1()
{
    __asm
    {
        // Original:
        //   call 005648D0
        //
        // Keep the client's global LoginVersion untouched, but make the live
        // textbox use the wide-text path and decode composition text as UTF-8.
        // This scopes Unicode support to chat/input instead of enabling the
        // full Vietnam codepage branch, which also changes unrelated client
        // behavior and blocks non-Vietnam Windows keyboard layouts.
        call u0x5648D0
        mov word ptr [esi+0x1628], 11h
        mov ax, 0FDE9h
        jmp u0x564FC6
    }
}

void __declspec(naked) naked_0x5676FE()
{
    __asm
    {
        // Original branch only used the wide char handler for LoginVersion=3.
        // We are intentionally not using that global codepage mode now; instead,
        // convert non-ASCII WM_CHAR input to UTF-8 bytes and feed the normal
        // textbox handler so ES/US/Vietnam layouts all share one path.
        mov eax, [esp+0x18]
        cmp eax, 0x7F
        jbe normal_handler

        pushad
        mov eax, [esp+0x3C]
        mov edx, [esp+0x38]
        push eax
        push edx
        push esi
        call insert_utf8_textbox_char
        popad
        jmp u0x56772D

        normal_handler:
        jmp u0x56771C
    }
}

void hook::unicode()
{
    if (!kEnableUnicode)
        return;

    // Chat UTF-8 support without changing the client's global LoginVersion.
    // The stock Vietnam branch enables UTF-8 display, but it also takes over
    // keyboard layout behavior and prevents ES/US + Unikey users from typing.
    // Instead, create the main GAME window as a Unicode HWND, translate
    // non-ASCII WM_CHAR/IME input into UTF-8 textbox bytes ourselves, and force
    // only the narrow renderer/wrapping checks that are required for chat.
    patch_game_window_ansi_creation_to_unicode();
    util::detour((void*)0x5676FE, naked_0x5676FE, 5);
    // UTF-8 textbox codepage for IME/composition text, scoped to the live
    // textbox fields instead of the whole client codepage/login version.
    util::detour((void*)0x564FC1, naked_0x564FC1, 5);
    // UTF-8 textbox bytes in the normal handler.
    // Keep this permissive because some paths still validate one byte at a time.
    util::write_memory((void*)0x566AC5, "\x90\x90", 2);
    // The textbox renderer uses this helper to step over multibyte characters,
    // but the stock client only enables that UTF-8 logic when LoginVersion == 3.
    // We keep LoginVersion untouched to avoid the Vietnam keyboard-layout lock,
    // and only force the UTF-8 byte-length branch needed for chat wrapping/display.
    util::write_memory((void*)0x407F7E, "\x90\x90", 2);
    // Chat box rendering has a second LoginVersion == 3 branch for the Vietnam
    // text path. Force only that local branch so UTF-8 chat lines are measured
    // and split as multibyte text, without enabling the full global codepage
    // mode that blocks non-Vietnam keyboard layouts.
    util::write_memory((void*)0x422E2B, "\x90\x90\x90\x90\x90\x90", 6);
    // Do not force the client's alternate Unicode window creation branch at
    // 00408650. In this executable that path makes the default "Shaiya" title
    // collapse to "S". The IAT wrapper above upgrades the normal CreateWindowExA
    // path to a Unicode HWND without changing the title source.
    util::write_memory((void*)0x40CA47, "\xEB", 1);
    // Chat send path byte limit.
    // The stock client forcibly writes a string terminator at byte 127 before
    // sending chat. UTF-8 languages use multiple bytes per character, so that
    // cap can cut a valid Vietnamese sequence in half even when the textbox and
    // renderer are already UTF-8 aware. NOP only that forced terminator; do not
    // resize structs or global buffers here.
    util::write_memory((void*)0x47A6BC, "\x90\x90\x90\x90", 4);

    if (kEnableSDataLegacyTextToUtf8)
    {
        auto thread = CreateThread(nullptr, 0, sdata_text_normalizer_thread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
}
