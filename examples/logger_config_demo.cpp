/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-02-22
 * @Description: LoggerV2 测试程序
 * 
 * 测试新的生产级日志系统的各项功能
 */

#include "logger_config.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

using namespace utils;

namespace {

struct TestContext {
    bool verbose = false;
};

struct CliOptions {
    bool ok = true;
    bool help = false;
    bool list = false;
    bool verbose = false;
    bool selftest = false;
    std::string error;
    std::vector<std::string> tests;  // empty => run all
};

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::string trim(std::string s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

static std::vector<std::string> splitCsv(const std::string& csv) {
    std::vector<std::string> out;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = toLower(trim(item));
        if (!item.empty()) {
            out.push_back(item);
        }
    }
    return out;
}

static CliOptions parseCli(int argc, char** argv) {
    CliOptions opt;
    if (argc <= 0 || argv == nullptr) {
        return opt;
    }

    auto setError = [&opt](const std::string& msg) {
        opt.ok = false;
        opt.error = msg;
    };

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? std::string(argv[i]) : std::string();

        if (arg == "--help" || arg == "-h") {
            opt.help = true;
            continue;
        }
        if (arg == "--list") {
            opt.list = true;
            continue;
        }
        if (arg == "--verbose" || arg == "-v") {
            opt.verbose = true;
            continue;
        }
        if (arg == "--selftest") {
            opt.selftest = true;
            continue;
        }

        std::string tests_value;
        if (arg == "--tests") {
            if (i + 1 >= argc) {
                setError("Missing value for --tests");
                return opt;
            }
            tests_value = argv[++i] ? std::string(argv[i]) : std::string();
        } else if (arg.rfind("--tests=", 0) == 0) {
            tests_value = arg.substr(std::string("--tests=").size());
        }

        if (!tests_value.empty() || arg == "--tests" || arg.rfind("--tests=", 0) == 0) {
            const std::string normalized = toLower(trim(tests_value));
            if (normalized.empty()) {
                setError("Empty value for --tests");
                return opt;
            }
            if (normalized == "all") {
                opt.tests.clear();
                continue;
            }

            const std::vector<std::string> parsed = splitCsv(tests_value);
            if (parsed.empty()) {
                setError("No valid tests in --tests value");
                return opt;
            }

            for (size_t k = 0; k < parsed.size(); ++k) {
                const std::string& name = parsed[k];
                if (std::find(opt.tests.begin(), opt.tests.end(), name) == opt.tests.end()) {
                    opt.tests.push_back(name);
                }
            }
            continue;
        }

        setError("Unknown option: " + arg);
        return opt;
    }

    return opt;
}

static void printUsage(const char* prog) {
    const std::string p = prog ? prog : "Logger_Test";
    std::cout
        << "Usage:\n"
        << "  " << p << " [--tests basic,perf,thread] [--verbose]\n"
        << "  " << p << " --list\n"
        << "  " << p << " --help\n"
        << "\n"
        << "Options:\n"
        << "  --tests <csv>   Run selected tests (comma-separated), or 'all'\n"
        << "  --list          List available tests\n"
        << "  --verbose       More output (still avoids spamming logs)\n"
        << "  --selftest      Run CLI parser self-test\n"
        << "  --help, -h      Show this help\n";
}

static int runSelfTest() {
    {
        const char* argv[] = {"Logger_Test"};
        CliOptions opt = parseCli(1, const_cast<char**>(argv));
        if (!opt.ok || !opt.tests.empty()) {
            std::cerr << "[selftest] default args expected ok and empty tests\n";
            return 1;
        }
    }
    {
        const char* argv[] = {"Logger_Test", "--tests", "basic,perf"};
        CliOptions opt = parseCli(3, const_cast<char**>(argv));
        if (!opt.ok || opt.tests.size() != 2 || opt.tests[0] != "basic" || opt.tests[1] != "perf") {
            std::cerr << "[selftest] --tests basic,perf parse failed\n";
            return 1;
        }
    }
    {
        const char* argv[] = {"Logger_Test", "--tests=basic, perf"};
        CliOptions opt = parseCli(2, const_cast<char**>(argv));
        if (!opt.ok || opt.tests.size() != 2 || opt.tests[0] != "basic" || opt.tests[1] != "perf") {
            std::cerr << "[selftest] --tests=basic, perf parse failed\n";
            return 1;
        }
    }
    {
        const char* argv[] = {"Logger_Test", "--list"};
        CliOptions opt = parseCli(2, const_cast<char**>(argv));
        if (!opt.ok || !opt.list) {
            std::cerr << "[selftest] --list parse failed\n";
            return 1;
        }
    }
    {
        const char* argv[] = {"Logger_Test", "--unknown"};
        CliOptions opt = parseCli(2, const_cast<char**>(argv));
        if (opt.ok) {
            std::cerr << "[selftest] unknown option should fail\n";
            return 1;
        }
    }

    return 0;
}

}  // namespace

// 测试函数
static void testBasicLogging(const TestContext& ctx) {
    std::cout << "\n[TEST] basic - 基础日志功能\n";

    if (ctx.verbose) {
        LOG_TRACE("This is a TRACE level message");
        LOG_DEBUG("This is a DEBUG level message");
    }
    LOG_INFO("This is an INFO level message");
    LOG_WARN("This is a WARN level message");
    LOG_ERROR("This is an ERROR level message");
    if (ctx.verbose) {
        LOG_FATAL("This is a FATAL level message");
    }

    std::cout << "✓ done\n";
}

static void testStructuredLogging(const TestContext&) {
    std::cout << "\n[TEST] fields - 结构化日志\n";

    LogFields fields = {
        {"user_id", "12345"},
        {"session_id", "abcde"},
        {"action", "login"},
        {"ip_address", "192.168.1.100"}
    };
    
    LOG_INFO_FIELDS(fields, "User action performed");

    std::cout << "✓ done\n";
}

static void testConditionalLogging(const TestContext&) {
    std::cout << "\n[TEST] cond - 条件日志\n";

    LOG_INFO_IF(false, "This should NOT appear");
    LOG_INFO_IF(true, "This should appear (condition=true)");

    std::cout << "✓ done\n";
}

static void testOnceLogging(const TestContext&) {
    std::cout << "\n[TEST] once - 一次性日志\n";

    LOG_INFO_ONCE("This message will only appear once");
    LOG_INFO_ONCE("This message will only appear once");
    LOG_INFO_ONCE("This message will only appear once");

    std::cout << "✓ done\n";
}

static void testLogLevelControl(const TestContext& ctx) {
    std::cout << "\n[TEST] level - 日志级别控制\n";

    // 设置日志级别为ERROR
    LoggerV2::setLevel(LogLevel::ERROR);
    std::cout << "level=ERROR (WARN/INFO suppressed)\n";
    if (ctx.verbose) {
        LOG_INFO("This should NOT appear (INFO < ERROR)");
        LOG_WARN("This should NOT appear (WARN < ERROR)");
    }
    LOG_ERROR("This should appear (ERROR)");
    LOG_FATAL("This should appear (FATAL)");
    
    // 恢复默认级别
    LoggerV2::setLevel(LogLevel::INFO);
    std::cout << "level=INFO (DEBUG suppressed)\n";
    if (ctx.verbose) {
        LOG_DEBUG("This should NOT appear (DEBUG < INFO)");
    }
    LOG_INFO("This should appear (INFO)");
    LOG_ERROR("This should appear (ERROR)");

    std::cout << "✓ done\n";
}

static void testThreadSafety(const TestContext& ctx) {
    std::cout << "\n[TEST] thread - 线程安全\n";

    const int num_threads = ctx.verbose ? 8 : 3;
    const int messages_per_thread = ctx.verbose ? 2000 : 30;
    const int sample_per_thread = ctx.verbose ? 4 : 2;
    std::vector<std::thread> threads;
    
    const LogLevel prev = LoggerV2::getLevel();
    LoggerV2::setLevel(LogLevel::TRACE);  // enable TRACE traffic without console spam (sink is INFO)

    auto worker = [sample_per_thread, messages_per_thread](int thread_id) {
        for (int i = 0; i < messages_per_thread; ++i) {
            if (i < sample_per_thread) {
                LOG_INFO("Thread %d sample %d", thread_id, i);
            } else {
                LOG_TRACE("Thread %d trace %d", thread_id, i);
            }
        }
    };
    
    // 启动多个线程
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }

    LoggerV2::setLevel(prev);
    std::cout << "✓ done (" << num_threads << " threads, "
              << num_threads * messages_per_thread << " msgs, "
              << (num_threads * sample_per_thread) << " printed)\n";
}

static void testQueueSize(const TestContext& ctx) {
    std::cout << "\n[TEST] queue - 队列大小监控\n";

    size_t initial_size = LoggerV2::queueSize();
    std::cout << "queueSize(initial) ~ " << initial_size << "\n";

    const LogLevel prev = LoggerV2::getLevel();
    LoggerV2::setLevel(LogLevel::TRACE);  // make TRACE enqueued; still not printed because sink is INFO

    const int burst = ctx.verbose ? 200000 : 40000;
    for (int i = 0; i < burst; ++i) {
        LOG_TRACE("queue probe %d", i);
    }

    const size_t after_burst = LoggerV2::queueSize();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const size_t after_drain = LoggerV2::queueSize();

    LoggerV2::setLevel(prev);

    std::cout << "queueSize(after burst) ~ " << after_burst << "\n";
    std::cout << "queueSize(after 20ms)  ~ " << after_drain << "\n";
    std::cout << "✓ done (size is approximate)\n";
}

static void testPerformance(const TestContext& ctx) {
    std::cout << "\n[TEST] perf - 性能测试 (no console IO)\n";

    const int num_messages = ctx.verbose ? 300000 : 80000;
    const LogLevel prev = LoggerV2::getLevel();
    LoggerV2::setLevel(LogLevel::TRACE);  // enable TRACE work; sink INFO won't print TRACE

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < num_messages; ++i) {
        LOG_TRACE("Performance test message %d", i);
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    LoggerV2::setLevel(prev);

    const double ms = static_cast<double>(duration.count());
    const double per_msg_ns = (ms <= 0.0) ? 0.0 : (ms * 1e6 / num_messages);
    std::cout << "messages=" << num_messages << ", time=" << duration.count() << " ms, avg=" << per_msg_ns << " ns/msg\n";
    std::cout << "✓ done\n";
}

static void testLogLevels(const TestContext&) {
    std::cout << "\n[TEST] enum - 日志级别枚举\n";

    std::cout << "TRACE: " << logLevelToString(LogLevel::TRACE) << "\n";
    std::cout << "DEBUG: " << logLevelToString(LogLevel::DEBUG) << "\n";
    std::cout << "INFO:  " << logLevelToString(LogLevel::INFO) << "\n";
    std::cout << "WARN:  " << logLevelToString(LogLevel::WARNING) << "\n";
    std::cout << "ERROR: " << logLevelToString(LogLevel::ERROR) << "\n";
    std::cout << "FATAL: " << logLevelToString(LogLevel::FATAL) << "\n";
    
    std::string level_str = "INFO";
    LogLevel parsed = stringToLogLevel(level_str);
    std::cout << "字符串转级别: '" << level_str << "' -> " 
              << logLevelToString(parsed) << "\n";

    std::cout << "✓ done\n";
}

// 主函数
int main(int argc, char** argv) {
    const CliOptions cli = parseCli(argc, argv);
    if (cli.selftest) {
        return runSelfTest();
    }
    if (!cli.ok) {
        std::cerr << "Error: " << cli.error << "\n\n";
        printUsage(argc > 0 ? argv[0] : "Logger_Test");
        return 2;
    }
    if (cli.help) {
        printUsage(argc > 0 ? argv[0] : "Logger_Test");
        return 0;
    }

    struct TestCase {
        const char* name;
        const char* desc;
        void (*fn)(const TestContext&);
    };

    const std::vector<TestCase> all_tests = {
        {"basic",  "基础日志功能",   &testBasicLogging},
        {"fields", "结构化日志",     &testStructuredLogging},
        {"cond",   "条件日志",       &testConditionalLogging},
        {"once",   "一次性日志",     &testOnceLogging},
        {"level",  "日志级别控制",   &testLogLevelControl},
        {"thread", "线程安全",       &testThreadSafety},
        {"queue",  "队列大小监控",   &testQueueSize},
        {"perf",   "性能测试",       &testPerformance},
        {"enum",   "日志级别枚举",   &testLogLevels},
    };

    if (cli.list) {
        std::cout << "Available tests:\n";
        for (size_t i = 0; i < all_tests.size(); ++i) {
            std::cout << "  " << all_tests[i].name << "  - " << all_tests[i].desc << "\n";
        }
        return 0;
    }

    std::cout << "========================================\n";
    std::cout << "    LoggerV2 生产级日志系统测试\n";
    std::cout << "========================================\n";

    // 初始化日志系统
    LoggerConfig config = LoggerConfig::defaultConfig();
    config.global_level = LogLevel::INFO;
    config.async = true;
    config.queue_capacity = 8192;
    config.flush_interval_ms = 1;
    LoggerV2::init(config);
    std::cout << "✓ logger init\n";

    const TestContext ctx{cli.verbose};

    std::unordered_map<std::string, const TestCase*> by_name;
    for (size_t i = 0; i < all_tests.size(); ++i) {
        by_name[all_tests[i].name] = &all_tests[i];
    }

    std::vector<const TestCase*> selected;
    if (cli.tests.empty()) {
        for (size_t i = 0; i < all_tests.size(); ++i) {
            selected.push_back(&all_tests[i]);
        }
    } else {
        for (size_t i = 0; i < cli.tests.size(); ++i) {
            const std::string& name = cli.tests[i];
            if (name == "all") {
                selected.clear();
                for (size_t j = 0; j < all_tests.size(); ++j) {
                    selected.push_back(&all_tests[j]);
                }
                break;
            }
            std::unordered_map<std::string, const TestCase*>::const_iterator it = by_name.find(name);
            if (it == by_name.end()) {
                std::cerr << "Unknown test: " << name << "\n";
                std::cerr << "Tip: use --list to see valid names.\n";
                LoggerV2::shutdown();
                return 2;
            }
            selected.push_back(it->second);
        }
    }

    for (size_t i = 0; i < selected.size(); ++i) {
        selected[i]->fn(ctx);
    }

    // 关闭日志系统
    LoggerV2::shutdown();
    std::cout << "\n✓ all done\n";
    std::cout << "========================================\n";

    return 0;
}
