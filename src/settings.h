#pragma once
#include <algorithm>
#include <array>
#include <cwctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace magpie {

enum ItemType {
    ITEM_MODEL, ITEM_PROVIDER, ITEM_GROUP, ITEM_ACTIVE,
    ITEM_QUOTA, ITEM_STATUS, ITEM_WAITING, ITEM_LATENCY, ITEM_COST, ITEM_TOKENS,
    ITEM_AGENT, ITEM_SPEED,
    ITEM_COUNT
};

struct ItemSpec {
    const wchar_t* name;
    const wchar_t* id;
    const wchar_t* label;
    const wchar_t* key;
    unsigned defaultCharacters;
    unsigned maximumCharacters;
    wchar_t sample;
};

inline const ItemSpec specs[ITEM_COUNT] = {
    {L"Magpie 模型", L"MagpieRoute.Model.v1", L"MDL: ", L"ModelCharacters", 14, 100, L'0'},
    {L"Magpie 供应商", L"MagpieRoute.Provider.v1", L"PRV: ", L"ProviderCharacters", 8, 100, L'0'},
    {L"Magpie 路由分组", L"MagpieRoute.Group.v1", L"GRP: ", L"GroupCharacters", 16, 100, L'0'},
    {L"Magpie 活动请求数", L"MagpieRoute.Active.v1", L"ACT: ", L"ActiveCharacters", 3, 10, L'9'},
    {L"Magpie 额度", L"MagpieRoute.Quota.v1", L"QTA: ", L"QuotaCharacters", 12, 40, L'0'},
    {L"Magpie 网关状态", L"MagpieRoute.Status.v1", L"STS: ", L"StatusCharacters", 10, 30, L'0'},
    {L"Magpie 排队请求数", L"MagpieRoute.Waiting.v1", L"WAIT: ", L"WaitingCharacters", 3, 10, L'9'},
    {L"Magpie 上次首字延迟", L"MagpieRoute.Latency.v1", L"LAT: ", L"LatencyCharacters", 6, 20, L'0'},
    {L"Magpie 今日费用", L"MagpieRoute.Cost.v1", L"COST: ", L"CostCharacters", 8, 20, L'0'},
    {L"Magpie 今日令牌数", L"MagpieRoute.Tokens.v1", L"TOK: ", L"TokenCharacters", 9, 30, L'0'},
    {L"Magpie 当前代理", L"MagpieRoute.Agent.v1", L"AGT: ", L"AgentCharacters", 10, 100, L'0'},
    {L"Magpie 平均输出令牌速度", L"MagpieRoute.Speed.v1", L"TPS: ", L"SpeedCharacters", 9, 20, L'0'},
};

constexpr unsigned DEFAULT_PORT = 3425;
constexpr unsigned DEFAULT_REFRESH_MS = 2000;
constexpr unsigned MIN_REFRESH_MS = 1000;
constexpr unsigned MAX_REFRESH_MS = 60000;
constexpr const wchar_t* SETTINGS_SECTION = L"Magpie";
constexpr const wchar_t* LEGACY_CHARACTERS_KEY = L"MaxCharacters";

struct Settings {
    unsigned port = DEFAULT_PORT;
    unsigned refreshMs = DEFAULT_REFRESH_MS;
    bool compactName = true;
    std::wstring agent;
    std::array<unsigned, ITEM_COUNT> characters{};
    Settings() {
        for (int index = 0; index < ITEM_COUNT; ++index)
            characters[index] = specs[index].defaultCharacters;
    }
};

using SettingValues = std::map<std::wstring, std::wstring>;

inline unsigned ParseUnsigned(const SettingValues& values, const wchar_t* key, unsigned fallback)
{
    const auto found = values.find(key);
    if (found == values.end()) return fallback;
    const auto& text = found->second;
    size_t position = 0;
    while (position < text.size() && std::iswspace(text[position])) ++position;
    if (position == text.size() || !std::iswdigit(text[position])) return fallback;
    unsigned long long value = 0;
    for (; position < text.size() && std::iswdigit(text[position]); ++position)
        value = std::min<unsigned long long>(value * 10 + (text[position] - L'0'), 0xFFFFFFFFull);
    return static_cast<unsigned>(value);
}

inline Settings ParseSettings(const SettingValues& values)
{
    Settings settings;
    settings.port = ParseUnsigned(values, L"Port", DEFAULT_PORT);
    settings.refreshMs = std::clamp(ParseUnsigned(values, L"RefreshMs", DEFAULT_REFRESH_MS), MIN_REFRESH_MS, MAX_REFRESH_MS);
    settings.compactName = ParseUnsigned(values, L"CompactName", 1) != 0;
    const auto agent = values.find(L"Agent");
    if (agent != values.end()) settings.agent = agent->second;
    const unsigned legacy = ParseUnsigned(values, LEGACY_CHARACTERS_KEY, specs[ITEM_MODEL].defaultCharacters);
    for (int index = 0; index < ITEM_COUNT; ++index) {
        const unsigned fallback = index == ITEM_MODEL ? legacy : specs[index].defaultCharacters;
        settings.characters[index] = std::clamp(ParseUnsigned(values, specs[index].key, fallback), 1u, specs[index].maximumCharacters);
    }
    return settings;
}

inline std::vector<std::pair<std::wstring, std::wstring>> SerializeSettings(const Settings& settings)
{
    std::vector<std::pair<std::wstring, std::wstring>> entries{
        {L"Port", std::to_wstring(settings.port)},
        {L"Agent", settings.agent},
        {L"RefreshMs", std::to_wstring(settings.refreshMs)},
        {L"CompactName", settings.compactName ? L"1" : L"0"},
    };
    for (int index = 0; index < ITEM_COUNT; ++index)
        entries.emplace_back(specs[index].key, std::to_wstring(settings.characters[index]));
    return entries;
}

inline std::wstring ShortItemName(int index)
{
    std::wstring label = specs[index].label;
    while (!label.empty() && (label.back() == L' ' || label.back() == L':')) label.pop_back();
    std::wstring name = specs[index].name;
    const std::wstring prefix = L"Magpie ";
    if (name.compare(0, prefix.size(), prefix) == 0) name.erase(0, prefix.size());
    return label + L" " + name;
}

inline std::wstring ValidateSettings(const Settings& settings)
{
    if (settings.port < 1 || settings.port > 65535) return L"端口必须在 1–65535 之间";
    if (settings.refreshMs < MIN_REFRESH_MS || settings.refreshMs > MAX_REFRESH_MS) return L"刷新间隔必须在 1000–60000 毫秒之间";
    for (int index = 0; index < ITEM_COUNT; ++index)
        if (settings.characters[index] < 1 || settings.characters[index] > specs[index].maximumCharacters)
            return ShortItemName(index) + L" 的字符数必须在 1–" + std::to_wstring(specs[index].maximumCharacters) + L" 之间";
    return L"";
}

}
