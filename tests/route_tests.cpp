#include "route_model.h"
#include "settings.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main(int argumentCount, char** arguments)
{
    const auto active = magpie::ParseRoutes(R"({"active":3,"routes":[{"agent":"codex","provider":"deepseek","model":"deepseek-v4-pro","phase":"responding","fallback":true},{"agent":"claude","phase":"started"},{"agent":"codex","phase":"done"}]})", "codex", false);
    Require(active.size() == 1, "Filter by agent and ignore completed requests");
    Require(active[0].fallback, "Keep fallback information");
    Require(magpie::RouteText(active[0], true) == "V4-PRO", "Compact mode formats to uppercase model without provider");
    magpie::Route claudeRoute;
    claudeRoute.provider = "cpa";
    claudeRoute.served = "claude-sonnet-5-5";
    Require(magpie::RouteText(claudeRoute, true) == "SONNET-5.5", "Trims claude- prefix and formats version");
    Require(magpie::FormatProvider(claudeRoute.provider) == "CPA", "Uppercase provider");
    magpie::Route geminiRoute;
    geminiRoute.provider = "cpa";
    geminiRoute.served = "gemini-3.8-flash-high";
    geminiRoute.requested = "group/task-complexity";
    Require(magpie::RouteText(geminiRoute, true) == "GEMINI-3.8", "Strips flash/high suffixes and uppercase");
    Require(magpie::ExtractGroupName(geminiRoute.requested) == "TASK-COMPLEXITY", "Extract uppercase group name");
    Require(magpie::ExtractGroupName("claude-sonnet-5-5") == "DIRECT", "Explicit model request is direct");
    Require(magpie::ExtractGroupName("") == "-", "Missing request does not imply direct routing");
    Require(magpie::ExtractGroupName("group/") == "-", "Missing group name stays unknown");
    const auto starting = magpie::ParseRoutes(R"({"routes":[{"agent":"codex","phase":"started"}]})", "", false);
    Require(magpie::RouteText(starting[0]) == "...", "Do not invent provider before routing");
    Require(magpie::ParseRoutes(R"({"routes":[]})", "", false).empty(), "Idle snapshot");
    const auto history = magpie::ParseRoutes(R"({"rows":[{"agent":"codex","source":"log","model":"unverified"},{"agent":"codex","source":"reported","model":"direct"},{"agent":"codex","provider":"router","model":"alias","served":"vendor/actual","req":"group/work","status":503,"providerAccount":"private@example.org"}]})", "codex", true);
    Require(history.size() == 1, "Exclude direct session and reported calls");
    Require(magpie::RouteText(history[0], false) == "VENDOR/ACTUAL", "Prefer vendor-reported model");
    Require(history[0].requested == "group/work" && history[0].status == 503, "Keep requested group and failure status");
    magpie::Route prefixed;
    prefixed.provider = "deepseek";
    prefixed.model = "deepseek/model";
    Require(magpie::RouteText(prefixed, false) == "DEEPSEEK/MODEL", "Preserve model");
    Require(magpie::HistoryPath("codex &x=1").find("&agent=codex%20%26x%3D1") != std::string::npos, "Encode agent filter");
    Require(magpie::HistoryPath("").find("via=through&computer=this&limit=20") != std::string::npos, "Query bounded local gateway history");
    for (const auto* body : {"not json", "{}", "{\"routes\":null}", "{\"routes\":[1]}"}) {
        bool threw = false;
        try { magpie::ParseRoutes(body, "", false); } catch (...) { threw = true; }
        Require(threw, "Reject malformed response instead of showing idle");
    }
    const auto unicode = magpie::ParseRoutes(u8R"({"routes":[{"provider":"供应商","model":"模型","phase":"attempt"}]})", "", false);
    Require(magpie::RouteText(unicode[0], true) == u8"模型", "Preserve Unicode");
    const auto concurrency = magpie::ParseConcurrency(R"({"active":[],"data":{},"requests":3,"tools":1,"waiting":2})");
    Require(concurrency.requests == 3 && concurrency.waiting == 2 && concurrency.tools == 1, "Parse concurrency");
    const auto balanceQuota = magpie::ParseQuota(R"({"data":[{"provider":"guoyu","windows":[],"balance":"$91.19","last":true}]})");
    Require(balanceQuota.text == "$91.19", "Show balance when no windows");
    const auto windowQuota = magpie::ParseQuota(R"({"data":[{"provider":"a","windows":[{"name":"5h","used":12.4},{"name":"week","used":71.6},{"name":"free","used":99,"unlimited":true}]},{"provider":"b","last":true,"windows":[{"name":"5h","used":33}]}]})");
    Require(windowQuota.text == "5H 33%", "Prefer the last served account");
    const auto worstQuota = magpie::ParseQuota(R"({"data":[{"provider":"a","windows":[{"name":"5h","used":12.4},{"name":"week","used":71.6},{"name":"free","used":99,"unlimited":true}]}]})");
    Require(worstQuota.text == "WEEK 72%", "Show the most used finite window");
    Require(magpie::ParseQuota(R"({"data":[]})").text == "-", "No quota cards");
    const auto usage = magpie::ParseUsage(R"({"calls":383,"errors":9,"input":2991277,"output":222723,"cost":8.836482})");
    Require(usage.calls == 383 && usage.errors == 9, "Parse usage counts");
    Require(magpie::FormatCost(usage.cost) == "$8.84", "Format cost");
    const auto emptyUsage = magpie::ParseUsage(R"({"period":"today","calls":0,"errors":0,"input":0,"output":0,"cost":0,"series":[]})");
    Require(magpie::FormatCost(emptyUsage.cost) == "$0.00" && magpie::FormatCount(emptyUsage.input + emptyUsage.output) == "0", "No requests today shows zero usage");
    Require(magpie::ParseRoutes(R"({"active":0,"routes":[]})", "", false).empty(), "Empty live snapshot");
    const auto emptyHistory = magpie::ParseRoutes(R"({"rows":[],"total":0})", "codex", true);
    Require(emptyHistory.empty() && magpie::SummarizeSpeed(emptyHistory, "").samples == 0, "Empty history has no speed");
    Require(magpie::StatusText({}) == "OK", "No requests is still healthy");
    Require(magpie::FormatCount(usage.input + usage.output) == "3.2M", "Format token count");
    Require(magpie::FormatLatency(480) == "480MS" && magpie::FormatLatency(4880) == "4.9S" && magpie::FormatLatency(0) == "-", "Format latency");
    const auto failedLive = magpie::ParseRoutes(R"({"routes":[{"phase":"done","status":200},{"phase":"attempt","status":429,"failed":true},{"phase":"attempt","fallback":true}]})", "", false);
    Require(magpie::StatusText(failedLive) == "ERR 429", "Failed live route wins");
    const auto fallbackLive = magpie::ParseRoutes(R"({"routes":[{"phase":"attempt","fallback":true}]})", "", false);
    Require(magpie::StatusText(fallbackLive) == "FALLBACK", "Fallback status");
    const auto failedHistory = magpie::ParseRoutes(R"({"rows":[{"status":503,"failed":true}]})", "", true);
    Require(magpie::StatusText(failedHistory) == "ERR 503", "Historical result can be shown separately");
    Require(magpie::StatusText({}) == "OK", "Idle gateway does not inherit historical failure");
    Require(magpie::StatusText({magpie::Route{}}) == "OK", "Healthy active request");
    const auto speed = magpie::ParseRoute(magpie::Json::parse(R"({"agent":"codex","out":200,"ms":6000,"ttft_ms":1000})"), true);
    Require(magpie::FormatProvider(speed.agent) == "CODEX", "Uppercase agent name");
    Require(magpie::FormatProvider("") == "-", "Missing agent stays unknown");
    Require(magpie::FormatOutputSpeed(speed) == "40.0T/S", "Output speed excludes first token wait");
    const auto reasoningSpeed = magpie::ParseRoute(magpie::Json::parse(R"({"out":1200,"reasoning":1000,"ms":10000,"ttft_ms":1000,"first_text_ms":5000})"), true);
    Require(std::fabs(reasoningSpeed.outputSpeed - 1200.0 / 9) < 0.01, "Window starts at first token including reasoning");
    for (const auto* body : {"{}", R"({"out":200,"ms":6000})", R"({"out":200,"ms":6000,"ttft_ms":1000,"status":500})", R"({"out":200,"ms":6000,"ttft_ms":1000,"reasoning_hidden":true})", R"({"out":200,"ms":6000,"ttft_ms":1000,"first_text_ms":1000,"reasoning_hidden":true})"})
        Require(magpie::FormatOutputSpeed(magpie::ParseRoute(magpie::Json::parse(body), true)) == "-", "Do not invent speed from missing, failed or burst timing");
    Require(magpie::FormatOutputSpeed(magpie::ParseRoute(magpie::Json::parse(R"({"out":200,"ms":6000,"ttft_ms":1000})"), false)) == "-", "Active requests have no measured final speed");
    const auto shortSpeed = magpie::ParseRoute(magpie::Json::parse(R"({"out":147,"ms":7417,"ttft_ms":6566})"), true);
    Require(shortSpeed.outputSpeed == 0 && shortSpeed.speedReason.find("样本过小") == 0, "Short window is rejected");
    const auto fewTokens = magpie::ParseRoute(magpie::Json::parse(R"({"out":40,"ms":6000,"ttft_ms":1000})"), true);
    Require(fewTokens.outputSpeed == 0, "Too few tokens are rejected");
    const auto burstSpeed = magpie::ParseRoute(magpie::Json::parse(R"({"out":200,"ms":6000,"ttft_ms":1000,"flow_ms":100})"), true);
    Require(burstSpeed.outputSpeed == 0 && burstSpeed.speedReason.find("疑似集中返回") == 0, "Burst delivery is rejected");
    const auto realBurst = magpie::ParseRoute(magpie::Json::parse(R"({"model":"claude-opus-5-5","out":981,"reasoning":69,"reasoning_hidden":true,"ms":11051,"ttft_ms":3584,"first_text_ms":10799,"flow_ms":60,"status":200})"), true);
    Require(realBurst.outputSpeed == 0, "Regression: 3619 T/S burst sample is rejected");
    const auto realTiny = magpie::ParseRoute(magpie::Json::parse(R"({"model":"claude-opus-5-5","out":273,"ms":3326,"ttft_ms":3296,"flow_ms":13,"status":200})"), true);
    Require(realTiny.outputSpeed == 0, "Regression: 9100 T/S tiny window is rejected");
    const auto tooFast = magpie::ParseRoute(magpie::Json::parse(R"({"out":5000,"ms":5000,"ttft_ms":1000})"), true);
    Require(tooFast.outputSpeed == 0 && tooFast.speedReason.find("速度超过") == 0, "Implausible speed is rejected");
    const auto explicitZero = magpie::ParseRoute(magpie::Json::parse(R"({"out":200,"ms":6000,"ttft_ms":1000,"reasoning":0,"reasoning_hidden":true})"), true);
    Require(explicitZero.outputSpeed == 40, "Known zero reasoning differs from missing reasoning");
    auto Sample = [](const char* model, int out, int windowMs) {
        const std::string body = std::string(R"({"model":")") + model + R"(","out":)" + std::to_string(out) +
            R"(,"ttft_ms":1000,"ms":)" + std::to_string(1000 + windowMs) + "}";
        return magpie::ParseRoute(magpie::Json::parse(body), true);
    };
    const std::vector<magpie::Route> speedHistory{realBurst, Sample("other", 900, 1000), Sample("claude-opus-5-5", 400, 2000),
        Sample("claude-opus-5-5", 600, 6000), Sample("claude-opus-5-5", 1000, 2000)};
    const auto summary = magpie::SummarizeSpeed(speedHistory, "claude-opus-5-5");
    Require(summary.samples == 3 && !summary.estimated, "Average only valid samples of the current model");
    Require(magpie::FormatSpeed(summary.speed, summary.estimated) == "200.0T/S", "Token-weighted average");
    Require(summary.minSpeed == 100 && summary.maxSpeed == 500 && summary.latest == &speedHistory[2], "Keep range and latest sample");
    Require(summary.excluded == 1 && summary.reasons.size() == 1, "Count excluded samples by reason");
    const auto fewSamples = magpie::SummarizeSpeed({Sample("m", 400, 2000)}, "m");
    Require(magpie::FormatSpeed(fewSamples.speed, fewSamples.estimated) == "~200.0T/S", "Fewer than 3 samples is estimated");
    std::vector<magpie::Route> manySamples;
    for (int index = 0; index < 7; ++index) manySamples.push_back(Sample("m", 200 * (index + 1), 2000));
    Require(magpie::SummarizeSpeed(manySamples, "m").samples == magpie::SPEED_SAMPLES, "Use at most 5 recent samples");
    Require(magpie::SummarizeSpeed(speedHistory, "").samples == 0 && magpie::SummarizeSpeed({}, "m").samples == 0, "No model or history has no speed");
    const auto defaults = magpie::ParseSettings({});
    Require(defaults.port == 3425 && defaults.refreshMs == 2000 && defaults.compactName && defaults.agent.empty(), "Default settings");
    Require(defaults.characters[magpie::ITEM_SPEED] == 9 && magpie::ValidateSettings(defaults).empty(), "Default characters are valid");
    const auto parsed = magpie::ParseSettings({{L"Port", L" 8080"}, {L"RefreshMs", L"10"}, {L"CompactName", L"0"}, {L"Agent", L"codex"},
        {L"MaxCharacters", L"20"}, {L"SpeedCharacters", L"999"}, {L"ActiveCharacters", L"abc"}});
    Require(parsed.port == 8080 && parsed.refreshMs == 1000 && !parsed.compactName && parsed.agent == L"codex", "Parse and clamp connection settings");
    Require(parsed.characters[magpie::ITEM_MODEL] == 20, "Legacy MaxCharacters applies to model");
    Require(parsed.characters[magpie::ITEM_SPEED] == 20 && parsed.characters[magpie::ITEM_ACTIVE] == 3, "Clamp characters and ignore invalid numbers");
    Require(magpie::ParseSettings({{L"ModelCharacters", L"12"}, {L"MaxCharacters", L"20"}}).characters[magpie::ITEM_MODEL] == 12, "Explicit model characters override legacy key");
    Require(magpie::ParseSettings({{L"Port", L"70000"}}).port == 70000, "Invalid port is preserved for validation");
    auto invalid = defaults;
    invalid.port = 0;
    Require(!magpie::ValidateSettings(invalid).empty(), "Reject port 0");
    invalid = defaults;
    invalid.characters[magpie::ITEM_SPEED] = 21;
    Require(magpie::ValidateSettings(invalid) == L"TPS 平均输出令牌速度 的字符数必须在 1–20 之间", "Reject characters above item maximum");
    const auto entries = magpie::SerializeSettings(parsed);
    Require(entries.size() == 4 + magpie::ITEM_COUNT && entries[0].first == L"Port" && entries[0].second == L"8080", "Serialize all settings");
    magpie::SettingValues roundTrip(entries.begin(), entries.end());
    const auto reparsed = magpie::ParseSettings(roundTrip);
    Require(reparsed.port == parsed.port && reparsed.agent == parsed.agent && reparsed.characters == parsed.characters &&
        reparsed.compactName == parsed.compactName && reparsed.refreshMs == parsed.refreshMs, "Settings round trip");
    if (argumentCount == 3) {
        for (int index = 1; index <= 2; ++index) {
            std::ifstream input(arguments[index]);
            Require(input.good(), "Open live response fixture");
            const std::string body((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            const auto routes = magpie::ParseRoutes(body, "", index == 2);
            for (const auto& route : routes)
                Require(!magpie::RouteText(route).empty(), "Render live response");
            std::cout << (index == 1 ? "Live snapshot" : "Live history") << " parsed: " << routes.size() << " routes\n";
        }
    }
    std::cout << "All route tests passed\n";
}
