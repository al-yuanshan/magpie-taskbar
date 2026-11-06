#include "route_model.h"
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
    Require(magpie::HistoryPath("").find("via=through&computer=this&limit=1") != std::string::npos, "Query local gateway history only");
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
    Require(magpie::FormatOutputSpeed(reasoningSpeed) == "40.0T/S", "Exclude reasoning tokens and time");
    for (const auto* body : {"{}", R"({"out":200,"ms":6000})", R"({"out":200,"ms":6000,"ttft_ms":1000,"status":500})", R"({"out":200,"ms":6000,"ttft_ms":1000,"reasoning_hidden":true})", R"({"out":200,"ms":6000,"ttft_ms":1000,"flow_ms":100})", R"({"out":200,"ms":6000,"ttft_ms":5900})"})
        Require(magpie::FormatOutputSpeed(magpie::ParseRoute(magpie::Json::parse(body), true)) == "-", "Do not invent speed from missing, failed or burst timing");
    Require(magpie::FormatOutputSpeed(magpie::ParseRoute(magpie::Json::parse(R"({"out":200,"ms":6000,"ttft_ms":1000})"), false)) == "-", "Active requests have no measured final speed");
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
