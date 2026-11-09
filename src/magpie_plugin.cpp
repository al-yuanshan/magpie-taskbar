#include <windows.h>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include "TrafficMonitor/PluginInterface.h"
#include "route_model.h"
#include "http.h"
#include "options_dialog.h"

namespace {
HMODULE moduleHandle = nullptr;

using namespace magpie;

struct Config {
    unsigned short port = 3425;
    unsigned interval = 2000;
    bool compactName = true;
    std::array<unsigned, ITEM_COUNT> characters{};
    std::string agent;
};

struct Display {
    std::array<std::wstring, ITEM_COUNT> values;
    std::wstring tooltip = L"Magpie：等待首次刷新";
    Display() { values.fill(L"-"); }
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
        case TMI_NAME: return L"magpie-taskbar";
        case TMI_DESCRIPTION: return L"Magpie 网关路由、额度、状态、延迟和用量监控";
        case TMI_VERSION: return L"0.3.4";
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
        const std::wstring legacyPath = configPath_ + L"MagpieRoute.ini";
        configPath_ += L"magpie-taskbar.ini";
        if (GetFileAttributesW(configPath_.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(legacyPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            CopyFileW(legacyPath.c_str(), configPath_.c_str(), TRUE);
        nextPoll_ = 0;
    }
    OptionReturn ShowOptionsDialog(void* parent) override {
        std::wstring path;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            path = configPath_;
        }
        if (path.empty()) return OR_OPTION_NOT_PROVIDED;
        if (!ShowSettingsDialog(static_cast<HWND>(parent), moduleHandle, path)) return OR_OPTION_UNCHANGED;
        const Settings settings = LoadSettings(path);
        for (int index = 0; index < ITEM_COUNT; ++index)
            characters_[index] = settings.characters[index];
        reload_ = true;
        nextPoll_ = 0;
        return OR_OPTION_CHANGED;
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
        const Settings settings = path.empty() ? Settings() : LoadSettings(path);
        config.characters = settings.characters;
        if (settings.port < 1 || settings.port > 65535) throw std::runtime_error("端口必须在 1–65535 之间");
        config.port = static_cast<unsigned short>(settings.port);
        config.interval = settings.refreshMs;
        config.compactName = settings.compactName;
        config.agent = magpie::Utf8(settings.agent);
        return config;
    }
    static std::wstring Details(const magpie::Route& route) {
        std::string text = "\n代理：" + route.agent + "\n实际模型：" + magpie::RouteText(route, false);
        if (!route.requested.empty()) text += "\n请求模型或分组：" + route.requested;
        if (!route.upstream.empty()) text += "\n上游：" + route.upstream;
        if (!route.time.empty()) text += "\n请求时间：" + route.time;
        if (!route.phase.empty()) text += "\n阶段：" + magpie::PhaseText(route.phase);
        if (route.fallback) text += "\n降级：是";
        if (route.status) text += "\nHTTP: " + std::to_string(route.status);
        if (route.ms > 0) text += "\n总耗时：" + magpie::FormatLatency(route.ms);
        if (route.ttftMs > 0) text += "\n首字延迟：" + magpie::FormatLatency(route.ttftMs);
        return magpie::Wide(text);
    }
    void Refresh() noexcept {
        unsigned interval = 2000;
        Display next;
        try {
            if (reload_.exchange(false)) {
                historyAt_ = quotaAt_ = usageAt_ = 0;
                history_.clear();
            }
            const Config config = ReadConfig();
            interval = config.interval;
            for (int index = 0; index < ITEM_COUNT; ++index)
                characters_[index] = config.characters[index];
            const ULONGLONG now = GetTickCount64();
            const auto live = magpie::ParseRoutes(magpie::Get(config.port, "/v1/magpie/route-events/snapshot"), config.agent, false);
            next.values[ITEM_ACTIVE] = std::to_wstring(live.size());
            next.tooltip = L"Magpie (127.0.0.1:" + std::to_wstring(config.port) + L")\n活动请求数：" + next.values[ITEM_ACTIVE];
            if (!config.agent.empty()) next.tooltip += L"\n代理筛选：" + magpie::Wide(config.agent);

            try {
                const auto concurrency = magpie::ParseConcurrency(magpie::Get(config.port, "/v1/magpie/concurrency"));
                next.values[ITEM_WAITING] = std::to_wstring(concurrency.waiting);
                next.tooltip += L"\n排队请求数：" + std::to_wstring(concurrency.waiting) + L"\n网关请求数：" + std::to_wstring(concurrency.requests);
                if (concurrency.tools) next.tooltip += L"\n等待工具结果：" + std::to_wstring(concurrency.tools);
            } catch (const std::exception& error) {
                next.tooltip += L"\n并发信息获取失败：" + magpie::Wide(error.what());
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

            std::string currentModel;
            if (!live.empty()) {
                const auto newest = std::max_element(live.begin(), live.end(), [](const auto& left, const auto& right) { return left.time < right.time; });
                currentModel = magpie::ModelKey(*newest);
                next.values[ITEM_MODEL] = magpie::Wide(magpie::RouteText(*newest, config.compactName));
                next.values[ITEM_PROVIDER] = magpie::Wide(magpie::FormatProvider(newest->provider));
                next.values[ITEM_GROUP] = magpie::Wide(magpie::ExtractGroupName(newest->requested));
                next.values[ITEM_AGENT] = magpie::Wide(magpie::FormatProvider(newest->agent));
                next.tooltip += L"\n正在处理的路由（最多显示 10 条）：";
                for (size_t index = 0; index < live.size() && index < 10; ++index)
                    next.tooltip += L"\n---" + Details(live[index]);
            } else if (!history_.empty()) {
                currentModel = magpie::ModelKey(history_.front());
                next.values[ITEM_MODEL] = magpie::Wide(magpie::RouteText(history_.front(), config.compactName));
                next.values[ITEM_PROVIDER] = magpie::Wide(magpie::FormatProvider(history_.front().provider));
                next.values[ITEM_GROUP] = magpie::Wide(magpie::ExtractGroupName(history_.front().requested));
                next.values[ITEM_AGENT] = magpie::Wide(magpie::FormatProvider(history_.front().agent));
                next.tooltip += L"\n最近完成的请求：" + Details(history_.front());
            } else {
                next.tooltip += historyError_.empty() ? L"\n最近 30 天没有网关请求。" : L"\n历史记录获取失败：" + historyError_;
            }

            if (!history_.empty()) {
                next.values[ITEM_LATENCY] = magpie::Wide(magpie::FormatLatency(history_.front().ttftMs));
                next.tooltip += L"\n最近请求测速说明：" + magpie::Wide(history_.front().speedReason) +
                    L"\n上次请求代理：" + magpie::Wide(magpie::FormatProvider(history_.front().agent)) +
                    L"\n上次请求模型：" + magpie::Wide(magpie::RouteText(history_.front(), config.compactName)) +
                    L"\n上次请求时间：" + magpie::Wide(history_.front().time) +
                    L"\n上次首字延迟：" + next.values[ITEM_LATENCY] +
                    L"\n上次总耗时：" + magpie::Wide(magpie::FormatLatency(history_.front().ms)) +
                    L"\n上次请求结果：" + magpie::Wide(magpie::StatusDescription(magpie::StatusText({history_.front()})));
            }
            const auto speed = magpie::SummarizeSpeed(history_, currentModel);
            if (speed.samples) {
                next.values[ITEM_SPEED] = magpie::Wide(magpie::FormatSpeed(speed.speed, speed.estimated));
                next.tooltip += L"\n输出速度（令牌/秒）：" + next.values[ITEM_SPEED] +
                    L"\n测速模型：" + magpie::Wide(magpie::RouteText(*speed.latest, config.compactName)) +
                    L"\n测速样本：" + std::to_wstring(speed.samples) + L" 条，按令牌加权平均" +
                    (speed.estimated ? L"（样本不足 3 条，仅供参考）" : L"") +
                    L"\n单条范围：" + magpie::Wide(magpie::FormatSpeed(speed.minSpeed, false)) + L" ~ " +
                    magpie::Wide(magpie::FormatSpeed(speed.maxSpeed, false)) +
                    L"\n最新样本时间：" + magpie::Wide(speed.latest->time) +
                    L"\n测速口径：首个令牌到请求结束，包含思考令牌";
            } else {
                next.tooltip += currentModel.empty() ? L"\n输出速度：没有可测速的模型。"
                    : L"\n输出速度：当前模型在最近 20 条网关请求中没有有效测速样本。";
            }
            if (speed.excluded) {
                next.tooltip += L"\n排除样本：" + std::to_wstring(speed.excluded) + L" 条";
                for (const auto& [reason, count] : speed.reasons)
                    next.tooltip += L"\n  " + magpie::Wide(reason) + L"：" + std::to_wstring(count) + L" 条";
            }
            if (!historyError_.empty()) next.tooltip += L"\n历史刷新失败，以上数据可能已过期：" + historyError_;
            next.values[ITEM_STATUS] = magpie::Wide(magpie::StatusText(live));
            next.tooltip += L"\n当前状态：" + magpie::Wide(magpie::StatusDescription(magpie::StatusText(live)));

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
            if (!quotaError_.empty()) next.tooltip += L"\n额度获取失败：" + quotaError_;

            if (now - usageAt_ >= 30000 || usageAt_ == 0) {
                try {
                    const auto usage = magpie::ParseUsage(magpie::Get(config.port, "/v1/magpie/usage?period=today"));
                    usageCost_ = magpie::Wide(magpie::FormatCost(usage.cost));
                    usageTokens_ = magpie::Wide(magpie::FormatCount(usage.input + usage.output));
                    usageDetail_ = L"今日用量（所有代理）：" + std::to_wstring(usage.calls) + L" 次请求，" +
                        std::to_wstring(usage.errors) + L" 次错误\n费用：" + usageCost_ + L"\n输入令牌：" +
                        magpie::Wide(magpie::FormatCount(usage.input)) + L"\n输出令牌：" + magpie::Wide(magpie::FormatCount(usage.output));
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
            if (!usageError_.empty()) next.tooltip += L"\n用量获取失败：" + usageError_;
        } catch (const std::exception& error) {
            next = Display();
            next.values.fill(L"-");
            next.values[ITEM_STATUS] = L"OFFLINE";
            next.tooltip = L"Magpie 连接失败：" + magpie::Wide(error.what()) + L"\n请检查 Magpie 是否运行、端口是否正确，以及版本是否支持路由事件接口。";
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
    std::atomic<bool> reload_{false};
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
