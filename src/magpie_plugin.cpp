#include <windows.h>
#include <shellapi.h>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include "TrafficMonitor/PluginInterface.h"
#include "route_model.h"
#include "http.h"

namespace {
HMODULE moduleHandle = nullptr;

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

const ItemSpec specs[ITEM_COUNT] = {
    {L"Magpie 模型", L"MagpieRoute.Model.v1", L"MDL: ", L"ModelCharacters", 14, 100, L'0'},
    {L"Magpie 供应商", L"MagpieRoute.Provider.v1", L"PRV: ", L"ProviderCharacters", 8, 100, L'0'},
    {L"Magpie 路由分组", L"MagpieRoute.Group.v1", L"GRP: ", L"GroupCharacters", 16, 100, L'0'},
    {L"Magpie 活动请求数", L"MagpieRoute.Active.v1", L"ACT: ", L"ActiveCharacters", 3, 10, L'9'},
    {L"Magpie 额度", L"MagpieRoute.Quota.v1", L"QTA: ", L"QuotaCharacters", 12, 40, L'0'},
    {L"Magpie 网关状态", L"MagpieRoute.Status.v1", L"STS: ", L"StatusCharacters", 10, 30, L'0'},
    {L"Magpie 排队请求数", L"MagpieRoute.Waiting.v1", L"WAIT: ", L"WaitingCharacters", 3, 10, L'9'},
    {L"Magpie 首字延迟", L"MagpieRoute.Latency.v1", L"LAT: ", L"LatencyCharacters", 6, 20, L'0'},
    {L"Magpie 今日费用", L"MagpieRoute.Cost.v1", L"COST: ", L"CostCharacters", 8, 20, L'0'},
    {L"Magpie 今日令牌数", L"MagpieRoute.Tokens.v1", L"TOK: ", L"TokenCharacters", 9, 30, L'0'},
    {L"Magpie 当前代理", L"MagpieRoute.Agent.v1", L"AGT: ", L"AgentCharacters", 10, 100, L'0'},
    {L"Magpie 输出令牌速度", L"MagpieRoute.Speed.v1", L"TPS: ", L"SpeedCharacters", 9, 20, L'0'},
};

struct Config {
    unsigned short port = 3425;
    unsigned interval = 2000;
    bool compactName = true;
    std::array<unsigned, ITEM_COUNT> characters{};
    std::string agent;
};

struct Display {
    std::array<std::wstring, ITEM_COUNT> values;
    std::wstring tooltip = L"Magpie: waiting for first refresh";
    Display() { values.fill(L"-"); values[ITEM_MODEL] = L"LOADING"; }
};

class Plugin;

class Item : public IPluginItem {
public:
    Item(Plugin& plugin, ItemType type) : plugin_(plugin), type_(type) {}
    const wchar_t* GetItemName() const override { return specs[type_].name; }
    const wchar_t* GetItemId() const override { return specs[type_].id; }
    const wchar_t* GetItemLableText() const override { return specs[type_].label; }
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override;
private:
    Plugin& plugin_;
    ItemType type_;
};

class Plugin : public ITMPlugin {
public:
    Plugin()
        : items_{Item(*this, ITEM_MODEL), Item(*this, ITEM_PROVIDER), Item(*this, ITEM_GROUP), Item(*this, ITEM_ACTIVE),
                 Item(*this, ITEM_QUOTA), Item(*this, ITEM_STATUS), Item(*this, ITEM_WAITING), Item(*this, ITEM_LATENCY),
                 Item(*this, ITEM_COST), Item(*this, ITEM_TOKENS), Item(*this, ITEM_AGENT), Item(*this, ITEM_SPEED)}
    {
        for (int index = 0; index < ITEM_COUNT; ++index)
            characters_[index] = specs[index].defaultCharacters;
    }
    IPluginItem* GetItem(int index) override {
        return index >= 0 && index < ITEM_COUNT ? &items_[index] : nullptr;
    }
    void DataRequired() override {
        const auto now = GetTickCount64();
        if (now < nextPoll_.load() || busy_.exchange(true)) return;
        HMODULE reference = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(&moduleHandle), &reference)) {
            busy_ = false;
            return;
        }
        if (!TrySubmitThreadpoolCallback(RefreshCallback, this, nullptr)) {
            FreeLibrary(reference);
            nextPoll_ = now + 2000;
            busy_ = false;
        }
    }
    const wchar_t* GetInfo(PluginInfoIndex index) override {
        switch (index) {
        case TMI_NAME: return L"Magpie Route";
        case TMI_DESCRIPTION: return L"Magpie gateway route, quota, status, latency and usage";
        case TMI_VERSION: return L"0.3.0";
        case TMI_URL: return L"https://github.com/yetone/magpie";
        default: return L"";
        }
    }
    const wchar_t* GetTooltipInfo() override {
        thread_local std::wstring text;
        text = ReadDisplay().tooltip;
        return text.c_str();
    }
    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override {
        if (index != EI_CONFIG_DIR || !data) return;
        std::lock_guard<std::mutex> lock(mutex_);
        configPath_ = data;
        if (!configPath_.empty() && configPath_.back() != L'\\' && configPath_.back() != L'/')
            configPath_ += L'\\';
        configPath_ += L"MagpieRoute.ini";
        nextPoll_ = 0;
    }
    OptionReturn ShowOptionsDialog(void* parent) override {
        std::wstring path;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            path = configPath_;
        }
        if (path.empty()) return OR_OPTION_NOT_PROVIDED;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            bool written = WritePrivateProfileStringW(L"Magpie", L"Port", L"3425", path.c_str()) &&
                WritePrivateProfileStringW(L"Magpie", L"Agent", L"", path.c_str()) &&
                WritePrivateProfileStringW(L"Magpie", L"RefreshMs", L"2000", path.c_str()) &&
                WritePrivateProfileStringW(L"Magpie", L"CompactName", L"1", path.c_str());
            for (const auto& spec : specs)
                written = written && WritePrivateProfileStringW(L"Magpie", spec.key, std::to_wstring(spec.defaultCharacters).c_str(), path.c_str());
            if (!written) {
                MessageBoxW(static_cast<HWND>(parent), L"Cannot create MagpieRoute.ini", L"Magpie Route", MB_ICONERROR);
                return OR_OPTION_UNCHANGED;
            }
        }
        ShellExecuteW(static_cast<HWND>(parent), L"open", L"notepad.exe", (L"\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
        return OR_OPTION_UNCHANGED;
    }
    Display ReadDisplay() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return display_;
    }
    unsigned MaxCharacters(ItemType type) const { return characters_[type].load(); }
private:
    static void CALLBACK RefreshCallback(PTP_CALLBACK_INSTANCE instance, void* context) {
        FreeLibraryWhenCallbackReturns(instance, moduleHandle);
        static_cast<Plugin*>(context)->Refresh();
    }
    Config ReadConfig() {
        std::wstring path;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            path = configPath_;
        }
        Config config;
        for (int index = 0; index < ITEM_COUNT; ++index)
            config.characters[index] = specs[index].defaultCharacters;
        if (path.empty()) return config;
        const auto port = GetPrivateProfileIntW(L"Magpie", L"Port", 3425, path.c_str());
        if (port == 0 || port > 65535) throw std::runtime_error("Port must be 1..65535");
        config.port = static_cast<unsigned short>(port);
        config.interval = std::clamp(GetPrivateProfileIntW(L"Magpie", L"RefreshMs", 2000, path.c_str()), 1000u, 60000u);
        config.compactName = GetPrivateProfileIntW(L"Magpie", L"CompactName", 1, path.c_str()) != 0;
        const auto legacy = GetPrivateProfileIntW(L"Magpie", L"MaxCharacters", specs[ITEM_MODEL].defaultCharacters, path.c_str());
        for (int index = 0; index < ITEM_COUNT; ++index) {
            const unsigned fallback = index == ITEM_MODEL ? legacy : specs[index].defaultCharacters;
            config.characters[index] = std::clamp(GetPrivateProfileIntW(L"Magpie", specs[index].key, fallback, path.c_str()), 1u, specs[index].maximumCharacters);
        }
        wchar_t agent[256]{};
        GetPrivateProfileStringW(L"Magpie", L"Agent", L"", agent, 256, path.c_str());
        config.agent = magpie::Utf8(agent);
        return config;
    }
    static std::wstring Details(const magpie::Route& route) {
        std::string text = "\nAgent: " + route.agent + "\nRoute: " + magpie::RouteText(route, false);
        if (!route.requested.empty()) text += "\nRequested: " + route.requested;
        if (!route.upstream.empty()) text += "\nUpstream: " + route.upstream;
        if (!route.time.empty()) text += "\nTime: " + route.time;
        if (!route.phase.empty()) text += "\nPhase: " + route.phase;
        if (route.fallback) text += "\nFallback: yes";
        if (route.status) text += "\nHTTP: " + std::to_string(route.status);
        if (route.ms > 0) text += "\nDuration: " + magpie::FormatLatency(route.ms);
        if (route.ttftMs > 0) text += "\nFirst token: " + magpie::FormatLatency(route.ttftMs);
        return magpie::Wide(text);
    }
    void Refresh() noexcept {
        unsigned interval = 2000;
        Display next;
        try {
            const Config config = ReadConfig();
            interval = config.interval;
            for (int index = 0; index < ITEM_COUNT; ++index)
                characters_[index] = config.characters[index];
            const ULONGLONG now = GetTickCount64();
            const auto live = magpie::ParseRoutes(magpie::Get(config.port, "/v1/magpie/route-events/snapshot"), config.agent, false);
            next.values[ITEM_ACTIVE] = std::to_wstring(live.size());
            next.tooltip = L"Magpie (127.0.0.1:" + std::to_wstring(config.port) + L")\nActive: " + next.values[ITEM_ACTIVE];
            if (!config.agent.empty()) next.tooltip += L"\nFilter: " + magpie::Wide(config.agent);

            try {
                const auto concurrency = magpie::ParseConcurrency(magpie::Get(config.port, "/v1/magpie/concurrency"));
                next.values[ITEM_WAITING] = std::to_wstring(concurrency.waiting);
                next.tooltip += L"\nWaiting: " + std::to_wstring(concurrency.waiting) + L"\nGateway requests: " + std::to_wstring(concurrency.requests);
                if (concurrency.tools) next.tooltip += L"\nWaiting for tool results: " + std::to_wstring(concurrency.tools);
            } catch (const std::exception& error) {
                next.tooltip += L"\nConcurrency unavailable: " + magpie::Wide(error.what());
            }

            if (live.empty() || now - historyAt_ >= 15000 || history_.empty()) {
                try {
                    history_ = magpie::ParseRoutes(magpie::Get(config.port, magpie::HistoryPath(config.agent)), config.agent, true);
                    historyAt_ = now;
                    historyError_.clear();
                } catch (const std::exception& error) {
                    historyError_ = magpie::Wide(error.what());
                }
            }

            if (!live.empty()) {
                const auto newest = std::max_element(live.begin(), live.end(), [](const auto& left, const auto& right) { return left.time < right.time; });
                next.values[ITEM_MODEL] = L"*" + magpie::Wide(magpie::RouteText(*newest, config.compactName));
                if (live.size() > 1) next.values[ITEM_MODEL] += L"(" + std::to_wstring(live.size()) + L")";
                next.values[ITEM_PROVIDER] = magpie::Wide(magpie::FormatProvider(newest->provider));
                next.values[ITEM_GROUP] = magpie::Wide(magpie::ExtractGroupName(newest->requested));
                next.values[ITEM_AGENT] = magpie::Wide(magpie::FormatProvider(newest->agent));
                next.tooltip += L"\nLive routing (up to 10 requests):";
                for (size_t index = 0; index < live.size() && index < 10; ++index)
                    next.tooltip += L"\n---" + Details(live[index]);
            } else if (!history_.empty()) {
                next.values[ITEM_MODEL] = magpie::Wide(magpie::RouteText(history_.front(), config.compactName));
                next.values[ITEM_PROVIDER] = magpie::Wide(magpie::FormatProvider(history_.front().provider));
                next.values[ITEM_GROUP] = magpie::Wide(magpie::ExtractGroupName(history_.front().requested));
                next.values[ITEM_AGENT] = magpie::Wide(magpie::FormatProvider(history_.front().agent));
                next.tooltip += L"\nMost recent completed request:" + Details(history_.front());
            } else {
                next.values[ITEM_MODEL] = historyError_.empty() ? L"IDLE" : L"-";
                next.tooltip += historyError_.empty() ? L"\nNo recent gateway requests (last 30 days)." : L"\nHistory unavailable: " + historyError_;
            }

            if (!history_.empty()) {
                const auto latency = history_.front().ttftMs > 0 ? history_.front().ttftMs : history_.front().ms;
                next.values[ITEM_LATENCY] = magpie::Wide(magpie::FormatLatency(latency));
                next.values[ITEM_SPEED] = magpie::Wide(magpie::FormatOutputSpeed(history_.front()));
                next.tooltip += L"\nOutput speed (last completed request): " + next.values[ITEM_SPEED] +
                    L"\nSpeed agent: " + magpie::Wide(magpie::FormatProvider(history_.front().agent));
            }
            next.values[ITEM_STATUS] = magpie::Wide(magpie::StatusText(live, history_));

            if (now - quotaAt_ >= 15000 || quotaAt_ == 0) {
                try {
                    const auto quota = magpie::ParseQuota(magpie::Get(config.port, "/v1/magpie/quotas?kept=1"));
                    quotaText_ = magpie::Wide(quota.text);
                    quotaDetail_ = magpie::Wide(quota.detail);
                    quotaError_.clear();
                } catch (const std::exception& error) {
                    quotaText_ = L"-";
                    quotaDetail_.clear();
                    quotaError_ = magpie::Wide(error.what());
                }
                quotaAt_ = now;
            }
            next.values[ITEM_QUOTA] = quotaText_;
            if (!quotaDetail_.empty()) next.tooltip += L"\n" + quotaDetail_;
            if (!quotaError_.empty()) next.tooltip += L"\nQuota unavailable: " + quotaError_;

            if (now - usageAt_ >= 30000 || usageAt_ == 0) {
                try {
                    const auto usage = magpie::ParseUsage(magpie::Get(config.port, "/v1/magpie/usage?period=today"));
                    usageCost_ = magpie::Wide(magpie::FormatCost(usage.cost));
                    usageTokens_ = magpie::Wide(magpie::FormatCount(usage.input + usage.output));
                    usageDetail_ = L"Today (all agents): " + std::to_wstring(usage.calls) + L" calls, " +
                        std::to_wstring(usage.errors) + L" errors\nCost: " + usageCost_ + L"\nInput: " +
                        magpie::Wide(magpie::FormatCount(usage.input)) + L"\nOutput: " + magpie::Wide(magpie::FormatCount(usage.output));
                    usageError_.clear();
                } catch (const std::exception& error) {
                    usageCost_ = usageTokens_ = L"-";
                    usageDetail_.clear();
                    usageError_ = magpie::Wide(error.what());
                }
                usageAt_ = now;
            }
            next.values[ITEM_COST] = usageCost_;
            next.values[ITEM_TOKENS] = usageTokens_;
            if (!usageDetail_.empty()) next.tooltip += L"\n" + usageDetail_;
            if (!usageError_.empty()) next.tooltip += L"\nUsage unavailable: " + usageError_;
        } catch (const std::exception& error) {
            next = Display();
            next.values.fill(L"-");
            next.values[ITEM_MODEL] = L"UNAVAILABLE";
            next.values[ITEM_STATUS] = L"OFFLINE";
            next.tooltip = L"Magpie unavailable: " + magpie::Wide(error.what()) + L"\nCheck Magpie is running, its version supports route-events, and the configured port.";
            historyAt_ = quotaAt_ = usageAt_ = 0;
        } catch (...) {
            next.values.fill(L"-");
            next.values[ITEM_STATUS] = L"OFFLINE";
        }
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            display_ = std::move(next);
        } catch (...) {}
        nextPoll_ = GetTickCount64() + interval;
        busy_ = false;
    }
    std::array<Item, ITEM_COUNT> items_;
    mutable std::mutex mutex_;
    std::wstring configPath_;
    Display display_;
    std::vector<magpie::Route> history_;
    std::wstring historyError_, quotaText_ = L"-", quotaDetail_, quotaError_, usageCost_ = L"-", usageTokens_ = L"-", usageDetail_, usageError_;
    ULONGLONG historyAt_ = 0, quotaAt_ = 0, usageAt_ = 0;
    std::atomic<bool> busy_{false};
    std::atomic<ULONGLONG> nextPoll_{0};
    std::array<std::atomic<unsigned>, ITEM_COUNT> characters_;
};

const wchar_t* Item::GetItemValueText() const
{
    thread_local std::wstring buffer;
    buffer = plugin_.ReadDisplay().values[type_];
    const size_t maximum = plugin_.MaxCharacters(type_);
    if (buffer.size() > maximum) {
        size_t keep = maximum > 1 ? maximum - 1 : 1;
        if (keep > 1 && buffer[keep - 1] >= 0xD800 && buffer[keep - 1] <= 0xDBFF) --keep;
        buffer.resize(keep);
        if (maximum > 1) buffer += L'…';
    }
    return buffer.c_str();
}

const wchar_t* Item::GetItemValueSampleText() const
{
    thread_local std::wstring sample;
    sample.assign(plugin_.MaxCharacters(type_), specs[type_].sample);
    return sample.c_str();
}
}

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    static Plugin plugin;
    return &plugin;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) moduleHandle = instance;
    return TRUE;
}
