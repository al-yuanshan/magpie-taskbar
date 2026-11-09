#include "options_dialog.h"
#include <commctrl.h>
#include <shellapi.h>
#include <memory>
#include <set>
#include <thread>
#include <vector>
#include "http.h"
#include "route_model.h"

namespace magpie {
namespace {

constexpr int ID_PORT = 1001;
constexpr int ID_PORT_SPIN = 1002;
constexpr int ID_REFRESH = 1003;
constexpr int ID_REFRESH_SPIN = 1004;
constexpr int ID_AGENT = 1005;
constexpr int ID_TEST = 1006;
constexpr int ID_TEST_RESULT = 1007;
constexpr int ID_COMPACT = 1008;
constexpr int ID_DEFAULTS = 1009;
constexpr int ID_OPEN_FILE = 1010;
constexpr int ID_CHARACTERS = 1100;
constexpr int ID_CHARACTERS_SPIN = 1200;
constexpr UINT WM_AGENTS_LOADED = WM_APP + 1;
constexpr UINT WM_TEST_FINISHED = WM_APP + 2;

constexpr int DIALOG_WIDTH = 332;
constexpr int ITEM_ROWS = (ITEM_COUNT + 1) / 2;
constexpr int DISPLAY_TOP = 92;
constexpr int ITEMS_TOP = DISPLAY_TOP + 28;
constexpr int ROW_HEIGHT = 17;
constexpr int DISPLAY_HEIGHT = ITEMS_TOP - DISPLAY_TOP + 10 + ITEM_ROWS * ROW_HEIGHT + 2;
constexpr int BUTTONS_TOP = DISPLAY_TOP + DISPLAY_HEIGHT + 8;
constexpr int DIALOG_HEIGHT = BUTTONS_TOP + 22;

constexpr const wchar_t* MISSING = L"\x1F";

struct DialogState {
    std::wstring path;
    Settings initial;
    bool saved = false;
};

struct TestResult {
    bool success = false;
    std::wstring text;
};

std::vector<WORD> BuildTemplate()
{
    std::vector<WORD> data(sizeof(DLGTEMPLATE) / sizeof(WORD), 0);
    auto* header = reinterpret_cast<DLGTEMPLATE*>(data.data());
    header->style = DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    header->cdit = 0;
    header->cx = DIALOG_WIDTH;
    header->cy = DIALOG_HEIGHT;
    auto append = [&](const wchar_t* text) {
        for (; *text; ++text) data.push_back(static_cast<WORD>(*text));
        data.push_back(0);
    };
    data.push_back(0);
    data.push_back(0);
    append(L"magpie-taskbar 设置");
    data.push_back(9);
    append(L"Microsoft YaHei UI");
    return data;
}

HWND Create(HWND dialog, const wchar_t* className, const wchar_t* text, DWORD style, int id, int x, int y, int width, int height, DWORD extended = 0)
{
    RECT rect{x, y, x + width, y + height};
    MapDialogRect(dialog, &rect);
    HWND control = CreateWindowExW(extended, className, text, WS_CHILD | WS_VISIBLE | style,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(control, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), FALSE);
    return control;
}

void CreateNumber(HWND dialog, int editId, int spinId, int x, int y, int width, unsigned minimum, unsigned maximum)
{
    HWND edit = Create(dialog, L"EDIT", L"", ES_NUMBER | ES_RIGHT | WS_TABSTOP, editId, x, y, width, 12, WS_EX_CLIENTEDGE);
    SendMessageW(edit, EM_SETLIMITTEXT, 5, 0);
    HWND spin = Create(dialog, UPDOWN_CLASSW, L"", UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS, spinId, 0, 0, 0, 0);
    SendMessageW(spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(edit), 0);
    SendMessageW(spin, UDM_SETRANGE32, minimum, maximum);
}

void SetNumber(HWND dialog, int id, unsigned value)
{
    SetDlgItemInt(dialog, id, value, FALSE);
}

void ApplySettings(HWND dialog, const Settings& settings)
{
    SetNumber(dialog, ID_PORT, settings.port);
    SetNumber(dialog, ID_REFRESH, settings.refreshMs);
    SetDlgItemTextW(dialog, ID_AGENT, settings.agent.c_str());
    CheckDlgButton(dialog, ID_COMPACT, settings.compactName ? BST_CHECKED : BST_UNCHECKED);
    for (int index = 0; index < ITEM_COUNT; ++index)
        SetNumber(dialog, ID_CHARACTERS + index, settings.characters[index]);
}

std::wstring ControlText(HWND dialog, int id)
{
    HWND control = GetDlgItem(dialog, id);
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
    text.resize(static_cast<size_t>(GetWindowTextW(control, text.data(), static_cast<int>(text.size()))));
    return text;
}

unsigned ReadNumber(HWND dialog, int id)
{
    BOOL valid = FALSE;
    const UINT value = GetDlgItemInt(dialog, id, &valid, FALSE);
    return valid ? value : 0;
}

std::wstring Trim(std::wstring text)
{
    const auto first = text.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return L"";
    return text.substr(first, text.find_last_not_of(L" \t") - first + 1);
}

int InvalidControl(const Settings& settings)
{
    if (settings.port < 1 || settings.port > 65535) return ID_PORT;
    if (settings.refreshMs < MIN_REFRESH_MS || settings.refreshMs > MAX_REFRESH_MS) return ID_REFRESH;
    for (int index = 0; index < ITEM_COUNT; ++index)
        if (settings.characters[index] < 1 || settings.characters[index] > specs[index].maximumCharacters)
            return ID_CHARACTERS + index;
    return 0;
}

Settings ReadControls(HWND dialog)
{
    Settings settings;
    settings.port = ReadNumber(dialog, ID_PORT);
    settings.refreshMs = ReadNumber(dialog, ID_REFRESH);
    settings.agent = Trim(ControlText(dialog, ID_AGENT));
    settings.compactName = IsDlgButtonChecked(dialog, ID_COMPACT) == BST_CHECKED;
    for (int index = 0; index < ITEM_COUNT; ++index)
        settings.characters[index] = ReadNumber(dialog, ID_CHARACTERS + index);
    return settings;
}

void LoadAgents(HWND dialog, unsigned port)
{
    std::thread([dialog, port] {
        auto* agents = new std::vector<std::wstring>;
        try {
            const auto routes = ParseRoutes(Get(static_cast<unsigned short>(port),
                "/v1/magpie/usage/requests?period=month&via=through&computer=this&limit=100&offset=0"), "", true);
            std::set<std::string> unique;
            for (const auto& route : routes)
                if (!route.agent.empty() && unique.insert(route.agent).second) agents->push_back(Wide(route.agent));
        } catch (...) {
        }
        if (!PostMessageW(dialog, WM_AGENTS_LOADED, 0, reinterpret_cast<LPARAM>(agents))) delete agents;
    }).detach();
}

void TestConnection(HWND dialog)
{
    const unsigned port = ReadNumber(dialog, ID_PORT);
    if (port < 1 || port > 65535) {
        SetDlgItemTextW(dialog, ID_TEST_RESULT, L"端口必须在 1–65535 之间");
        return;
    }
    EnableWindow(GetDlgItem(dialog, ID_TEST), FALSE);
    SetDlgItemTextW(dialog, ID_TEST_RESULT, L"正在连接…");
    std::thread([dialog, port] {
        auto* result = new TestResult;
        try {
            const auto concurrency = ParseConcurrency(Get(static_cast<unsigned short>(port), "/v1/magpie/concurrency"));
            result->success = true;
            result->text = L"连接成功：网关请求 " + std::to_wstring(concurrency.requests) + L" 个，排队 " +
                std::to_wstring(concurrency.waiting) + L" 个";
        } catch (const std::exception& error) {
            result->text = L"连接失败：" + Wide(error.what());
        } catch (...) {
            result->text = L"连接失败";
        }
        if (!PostMessageW(dialog, WM_TEST_FINISHED, 0, reinterpret_cast<LPARAM>(result))) delete result;
    }).detach();
}

void InitDialog(HWND dialog, DialogState& state)
{
    Create(dialog, L"BUTTON", L"连接", BS_GROUPBOX, -1, 7, 4, DIALOG_WIDTH - 14, 82);
    Create(dialog, L"STATIC", L"端口", SS_LEFT, -1, 16, 20, 50, 10);
    CreateNumber(dialog, ID_PORT, ID_PORT_SPIN, 70, 18, 52, 1, 65535);
    Create(dialog, L"STATIC", L"刷新间隔（毫秒）", SS_LEFT, -1, 150, 20, 70, 10);
    CreateNumber(dialog, ID_REFRESH, ID_REFRESH_SPIN, 222, 18, 52, MIN_REFRESH_MS, MAX_REFRESH_MS);
    UDACCEL refreshStep{0, 500};
    SendDlgItemMessageW(dialog, ID_REFRESH_SPIN, UDM_SETACCEL, 1, reinterpret_cast<LPARAM>(&refreshStep));
    Create(dialog, L"STATIC", L"代理筛选", SS_LEFT, -1, 16, 39, 50, 10);
    HWND agent = Create(dialog, L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP, ID_AGENT, 70, 37, 150, 120);
    SendMessageW(agent, CB_LIMITTEXT, 100, 0);
    Create(dialog, L"BUTTON", L"测试连接", BS_PUSHBUTTON | WS_TABSTOP, ID_TEST, 236, 36, 80, 14);
    Create(dialog, L"STATIC", L"留空表示统计全部代理；下拉列表来自最近的网关请求。", SS_LEFT, -1, 70, 53, 246, 10);
    Create(dialog, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, ID_TEST_RESULT, 16, 68, DIALOG_WIDTH - 32, 10);

    Create(dialog, L"BUTTON", L"显示", BS_GROUPBOX, -1, 7, DISPLAY_TOP, DIALOG_WIDTH - 14, DISPLAY_HEIGHT);
    Create(dialog, L"BUTTON", L"精简模型名称（如 CLAUDE-OPUS-5-5 显示为 OPUS-5.5）", BS_AUTOCHECKBOX | WS_TABSTOP, ID_COMPACT, 16, DISPLAY_TOP + 12, 280, 10);
    Create(dialog, L"STATIC", L"各显示项最多显示的字符数：", SS_LEFT, -1, 16, ITEMS_TOP - 3, 200, 10);
    for (int index = 0; index < ITEM_COUNT; ++index) {
        const int column = index / ITEM_ROWS;
        const int row = index % ITEM_ROWS;
        const int x = 16 + column * 156;
        const int y = ITEMS_TOP + 10 + row * ROW_HEIGHT;
        Create(dialog, L"STATIC", ShortItemName(index).c_str(), SS_LEFT | SS_ENDELLIPSIS, -1, x, y + 2, 100, 10);
        CreateNumber(dialog, ID_CHARACTERS + index, ID_CHARACTERS_SPIN + index, x + 102, y, 40, 1, specs[index].maximumCharacters);
    }

    Create(dialog, L"BUTTON", L"恢复默认", BS_PUSHBUTTON | WS_TABSTOP, ID_DEFAULTS, 7, BUTTONS_TOP, 56, 14);
    Create(dialog, L"BUTTON", L"打开配置文件", BS_PUSHBUTTON | WS_TABSTOP, ID_OPEN_FILE, 67, BUTTONS_TOP, 64, 14);
    Create(dialog, L"BUTTON", L"确定", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, DIALOG_WIDTH - 125, BUTTONS_TOP, 56, 14);
    Create(dialog, L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, DIALOG_WIDTH - 63, BUTTONS_TOP, 56, 14);

    ApplySettings(dialog, state.initial);
    if (state.initial.port >= 1 && state.initial.port <= 65535) LoadAgents(dialog, state.initial.port);
}

bool Save(HWND dialog, DialogState& state)
{
    const Settings settings = ReadControls(dialog);
    const std::wstring error = ValidateSettings(settings);
    if (!error.empty()) {
        MessageBoxW(dialog, error.c_str(), L"magpie-taskbar", MB_ICONWARNING);
        HWND control = GetDlgItem(dialog, InvalidControl(settings));
        SetFocus(control);
        SendMessageW(control, EM_SETSEL, 0, -1);
        return false;
    }
    if (!SaveSettings(state.path, settings)) {
        MessageBoxW(dialog, (L"无法写入配置文件：\n" + state.path).c_str(), L"magpie-taskbar", MB_ICONERROR);
        return false;
    }
    state.saved = true;
    return true;
}

INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    switch (message) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        InitDialog(dialog, *reinterpret_cast<DialogState*>(lParam));
        return TRUE;
    case WM_AGENTS_LOADED: {
        std::unique_ptr<std::vector<std::wstring>> agents(reinterpret_cast<std::vector<std::wstring>*>(lParam));
        const std::wstring current = ControlText(dialog, ID_AGENT);
        SendDlgItemMessageW(dialog, ID_AGENT, CB_RESETCONTENT, 0, 0);
        for (const auto& agent : *agents)
            SendDlgItemMessageW(dialog, ID_AGENT, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(agent.c_str()));
        SetDlgItemTextW(dialog, ID_AGENT, current.c_str());
        return TRUE;
    }
    case WM_TEST_FINISHED: {
        std::unique_ptr<TestResult> result(reinterpret_cast<TestResult*>(lParam));
        SetDlgItemTextW(dialog, ID_TEST_RESULT, result->text.c_str());
        EnableWindow(GetDlgItem(dialog, ID_TEST), TRUE);
        if (result->success) LoadAgents(dialog, ReadNumber(dialog, ID_PORT));
        return TRUE;
    }
    case WM_COMMAND:
        if (!state) break;
        switch (LOWORD(wParam)) {
        case IDOK:
            if (Save(dialog, *state)) EndDialog(dialog, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        case ID_TEST:
            TestConnection(dialog);
            return TRUE;
        case ID_DEFAULTS:
            ApplySettings(dialog, Settings());
            SetDlgItemTextW(dialog, ID_TEST_RESULT, L"已恢复默认值，点击“确定”后生效。");
            return TRUE;
        case ID_OPEN_FILE:
            if (GetFileAttributesW(state->path.c_str()) == INVALID_FILE_ATTRIBUTES && !SaveSettings(state->path, state->initial)) {
                MessageBoxW(dialog, (L"无法创建配置文件：\n" + state->path).c_str(), L"magpie-taskbar", MB_ICONERROR);
                return TRUE;
            }
            ShellExecuteW(dialog, L"open", L"notepad.exe", (L"\"" + state->path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

}

Settings LoadSettings(const std::wstring& path)
{
    SettingValues values;
    std::vector<const wchar_t*> keys{L"Port", L"Agent", L"RefreshMs", L"CompactName", LEGACY_CHARACTERS_KEY};
    for (const auto& spec : specs) keys.push_back(spec.key);
    for (const auto* key : keys) {
        wchar_t buffer[256]{};
        GetPrivateProfileStringW(SETTINGS_SECTION, key, MISSING, buffer, 256, path.c_str());
        if (std::wstring(buffer) != MISSING) values[key] = buffer;
    }
    return ParseSettings(values);
}

bool SaveSettings(const std::wstring& path, const Settings& settings)
{
    for (const auto& [key, value] : SerializeSettings(settings))
        if (!WritePrivateProfileStringW(SETTINGS_SECTION, key.c_str(), value.c_str(), path.c_str())) return false;
    return true;
}

bool ShowSettingsDialog(HWND parent, HINSTANCE instance, const std::wstring& path)
{
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    DialogState state;
    state.path = path;
    state.initial = LoadSettings(path);
    const auto data = BuildTemplate();
    DialogBoxIndirectParamW(instance, reinterpret_cast<const DLGTEMPLATE*>(data.data()), parent, DialogProc, reinterpret_cast<LPARAM>(&state));
    return state.saved;
}

}
