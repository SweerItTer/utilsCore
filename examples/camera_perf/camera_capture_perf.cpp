#include <algorithm>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include <linux/videodev2.h>

#include "drm/deviceController.h"
#include "logger_config.h"
#include "logger_v2.h"
#include "sharedBufferState.h"
#include "v4l2/cameraController.h"
#include "v4l2/frame.h"

namespace {

struct Options {
    std::string device = "/dev/video0";
    std::string outputDir = "camera_perf_run";
    std::string pixelFormatName = "nv12";
    uint32_t width = 1280;
    uint32_t height = 720;
    uint32_t pixelFormat = V4L2_PIX_FMT_NV12;
    uint32_t planeCount = 2;
    int frames = 300;
    int warmup = 30;
    int bufferCount = 4;
    int sampleCount = 4;
    bool useDmabuf = false;
};

struct FrameEvent {
    uint64_t frameIndex = 0;
    uint64_t frameId = 0;
    uint64_t v4l2TimestampNs = 0;
    uint64_t callbackTimestampUs = 0;
    int bufferIndex = -1;
    size_t payloadBytes = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> sampleBytes;
};

struct MetricSample {
    uint64_t frameIndex = 0;
    uint64_t frameId = 0;
    uint64_t v4l2TimestampNs = 0;
    uint64_t callbackTimestampUs = 0;
    uint64_t frameIntervalUs = 0;
    int bufferIndex = -1;
    size_t payloadBytes = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct PersistedFrameSample {
    uint64_t frameIndex = 0;
    std::vector<uint8_t> bytes;
};

struct SummaryStats {
    double mean = 0.0;
    double median = 0.0;
    double stddev = 0.0;
    uint64_t min = 0;
    uint64_t max = 0;
    uint64_t p95 = 0;
    uint64_t p99 = 0;
};

struct RunState {
    explicit RunState(const Options& opts)
        : options(opts) {}

    Options options;
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<FrameEvent> pending;
    std::vector<MetricSample> metrics;
    std::vector<PersistedFrameSample> frameSamples;
    std::set<uint64_t> sampleIndices;
    uint64_t observedFrames = 0;
    uint64_t queuedFrames = 0;
    uint64_t processedFrames = 0;
    uint64_t previousCallbackUs = 0;
    uint32_t actualWidth = 0;
    uint32_t actualHeight = 0;
    bool stopRequested = false;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

uint64_t monotonicRawUs() {
    struct timespec ts {};
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        fail(std::string("clock_gettime(CLOCK_MONOTONIC_RAW) failed: ") + std::strerror(errno));
    }
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + static_cast<uint64_t>(ts.tv_nsec) / 1000ULL;
}

bool parseBool(const std::string& value) {
    if (value == "1" || value == "true" || value == "on" || value == "yes") {
        return true;
    }
    if (value == "0" || value == "false" || value == "off" || value == "no") {
        return false;
    }
    fail("Invalid boolean value: " + value);
}

uint32_t parsePixelFormat(const std::string& value, uint32_t& planeCount) {
    if (value == "nv12") {
        planeCount = 2;
        return V4L2_PIX_FMT_NV12;
    }
    if (value == "yuyv") {
        planeCount = 1;
        return V4L2_PIX_FMT_YUYV;
    }
    if (value == "rgb24") {
        planeCount = 1;
        return V4L2_PIX_FMT_RGB24;
    }
    fail("Unsupported format: " + value + " (expected nv12/yuyv/rgb24)");
}

int parseInt(const char* text, const char* name) {
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        fail(std::string("Invalid integer for ") + name + ": " + text);
    }
    return static_cast<int>(value);
}

void ensureDirectory(const std::string& path) {
    if (path.empty()) {
        fail("Output directory must not be empty");
    }

    std::string current;
    if (path[0] == '/') {
        current = "/";
    }

    std::stringstream stream(path);
    std::string segment;
    while (std::getline(stream, segment, '/')) {
        if (segment.empty()) {
            continue;
        }
        if (!current.empty() && current.back() != '/') {
            current += "/";
        }
        current += segment;

        if (::mkdir(current.c_str(), 0775) == 0) {
            continue;
        }
        if (errno != EEXIST) {
            fail("Failed to create directory " + current + ": " + std::strerror(errno));
        }
    }
}

void writeBinaryFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream output(path.c_str(), std::ios::binary);
    if (!output) {
        fail("Failed to open " + path + " for writing");
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        fail("Failed to write " + path);
    }
}

std::string joinPath(const std::string& base, const std::string& name) {
    if (base.empty() || base.back() == '/') {
        return base + name;
    }
    return base + "/" + name;
}

std::set<uint64_t> buildSampleIndices(int measuredFrames, int sampleCount) {
    std::set<uint64_t> indices;
    if (measuredFrames <= 0 || sampleCount <= 0) {
        return indices;
    }
    if (sampleCount == 1) {
        indices.insert(static_cast<uint64_t>(measuredFrames));
        return indices;
    }
    for (int i = 0; i < sampleCount; ++i) {
        const double ratio = sampleCount == 1 ? 1.0 : static_cast<double>(i) / static_cast<double>(sampleCount - 1);
        const uint64_t index = 1 + static_cast<uint64_t>(std::llround(ratio * static_cast<double>(measuredFrames - 1)));
        indices.insert(index);
    }
    return indices;
}

std::vector<uint8_t> copyFrameBytes(const FramePtr& frame, uint32_t planeCount) {
    if (!frame) {
        return {};
    }

    std::vector<uint8_t> bytes;
    bytes.reserve(frame->size());
    for (uint32_t plane = 0; plane < std::max<uint32_t>(planeCount, 1U); ++plane) {
        auto state = frame->sharedState(static_cast<int>(plane));
        if (!state) {
            if (plane == 0) {
                return {};
            }
            break;
        }

        const size_t planeBytes = state->length;
        if (planeBytes == 0) {
            continue;
        }

        if (state->backing == BufferBacking::MMAP) {
            const auto* ptr = static_cast<const uint8_t*>(frame->data(static_cast<int>(plane)));
            if (!ptr) {
                return {};
            }
            bytes.insert(bytes.end(), ptr, ptr + planeBytes);
            continue;
        }

        if (state->dmabuf_ptr) {
            auto mapped = state->dmabuf_ptr->scopedMap();
            uint8_t* ptr = mapped.get();
            if (!ptr) {
                return {};
            }
            bytes.insert(bytes.end(), ptr, ptr + planeBytes);
            continue;
        }

        const int fd = state->dmabuf_fd();
        if (fd < 0) {
            return {};
        }
        void* mapped = ::mmap(nullptr, planeBytes, PROT_READ, MAP_SHARED, fd, 0);
        if (mapped == MAP_FAILED) {
            return {};
        }
        const auto* ptr = static_cast<const uint8_t*>(mapped);
        bytes.insert(bytes.end(), ptr, ptr + planeBytes);
        ::munmap(mapped, planeBytes);
    }
    return bytes;
}

SummaryStats computeStats(std::vector<uint64_t> values) {
    SummaryStats stats;
    if (values.empty()) {
        return stats;
    }

    std::sort(values.begin(), values.end());
    stats.min = values.front();
    stats.max = values.back();
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    stats.mean = sum / static_cast<double>(values.size());
    if (values.size() % 2 == 0) {
        const size_t right = values.size() / 2;
        const size_t left = right - 1;
        stats.median = (static_cast<double>(values[left]) + static_cast<double>(values[right])) / 2.0;
    } else {
        stats.median = static_cast<double>(values[values.size() / 2]);
    }

    double variance = 0.0;
    for (const uint64_t value : values) {
        const double delta = static_cast<double>(value) - stats.mean;
        variance += delta * delta;
    }
    variance /= static_cast<double>(values.size());
    stats.stddev = std::sqrt(variance);

    auto percentile = [&values](double ratio) -> uint64_t {
        const double rawIndex = ratio * static_cast<double>(values.size() - 1);
        const size_t index = static_cast<size_t>(std::llround(rawIndex));
        return values[index];
    };

    stats.p95 = percentile(0.95);
    stats.p99 = percentile(0.99);
    return stats;
}

void writeMetricsCsv(const std::string& outputDir, const std::vector<MetricSample>& metrics) {
    const std::string path = joinPath(outputDir, "metrics.csv");
    std::ofstream output(path.c_str());
    if (!output) {
        fail("Failed to open " + path);
    }

    output << "frame_index,frame_id,v4l2_timestamp_ns,callback_timestamp_us,frame_interval_us,buffer_index,payload_bytes,width,height\n";
    for (const auto& metric : metrics) {
        output << metric.frameIndex << ','
               << metric.frameId << ','
               << metric.v4l2TimestampNs << ','
               << metric.callbackTimestampUs << ','
               << metric.frameIntervalUs << ','
               << metric.bufferIndex << ','
               << metric.payloadBytes << ','
               << metric.width << ','
               << metric.height << '\n';
    }
}

void writeSummary(const std::string& outputDir,
                  const Options& options,
                  const SummaryStats& intervalStats,
                  size_t metricCount,
                  size_t sampleCount,
                  uint32_t actualWidth,
                  uint32_t actualHeight) {
    const std::string path = joinPath(outputDir, "summary.txt");
    std::ofstream output(path.c_str());
    if (!output) {
        fail("Failed to open " + path);
    }

    const bool exactMatch = actualWidth == options.width && actualHeight == options.height;
    output << "device=" << options.device << '\n';
    output << "requested_width=" << options.width << '\n';
    output << "requested_height=" << options.height << '\n';
    output << "actual_width=" << actualWidth << '\n';
    output << "actual_height=" << actualHeight << '\n';
    output << "pixel_format=" << options.pixelFormatName << '\n';
    output << "frames=" << options.frames << '\n';
    output << "warmup=" << options.warmup << '\n';
    output << "buffer_count=" << options.bufferCount << '\n';
    output << "use_dmabuf=" << (options.useDmabuf ? 1 : 0) << '\n';
    output << "sample_count=" << sampleCount << '\n';
    output << "metric_rows=" << metricCount << '\n';
    output << "status=" << (exactMatch ? "supported" : "adjusted") << '\n';
    output << "frame_interval_mean_us=" << std::fixed << std::setprecision(3) << intervalStats.mean << '\n';
    output << "frame_interval_median_us=" << intervalStats.median << '\n';
    output << "frame_interval_stddev_us=" << intervalStats.stddev << '\n';
    output << "frame_interval_min_us=" << intervalStats.min << '\n';
    output << "frame_interval_max_us=" << intervalStats.max << '\n';
    output << "frame_interval_p95_us=" << intervalStats.p95 << '\n';
    output << "frame_interval_p99_us=" << intervalStats.p99 << '\n';
}

void persistFrameSamples(const std::string& outputDir,
                         const Options& options,
                         const std::vector<PersistedFrameSample>& samples) {
    for (const auto& sample : samples) {
        std::ostringstream name;
        name << "sample_frame_" << std::setfill('0') << std::setw(4) << sample.frameIndex
             << "." << options.pixelFormatName;
        writeBinaryFile(joinPath(outputDir, name.str()), sample.bytes);
    }
}

void printUsage(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " [options]\n"
        << "  --device <path>            Camera device path, default /dev/video0\n"
        << "  --width <pixels>           Capture width, default 1280\n"
        << "  --height <pixels>          Capture height, default 720\n"
        << "  --format <nv12|yuyv|rgb24> Pixel format, default nv12\n"
        << "  --frames <count>           Measured frames after warmup, default 300\n"
        << "  --warmup <count>           Warmup frames excluded from stats, default 30\n"
        << "  --buffer-count <count>     V4L2 buffer count, default 4\n"
        << "  --sample-count <count>     Sample raw frames to persist, default 4\n"
        << "  --use-dmabuf <bool>        Enable DMABUF mode, default false\n"
        << "  --output-dir <path>        Output directory, default ./camera_perf_run\n"
        << "  --help                     Show this message\n";
}

Options parseArgs(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto requireValue = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                fail(std::string("Missing value for ") + name);
            }
            return argv[++i];
        };

        if (arg == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        } else if (arg == "--device") {
            options.device = requireValue("--device");
        } else if (arg == "--width") {
            options.width = static_cast<uint32_t>(parseInt(requireValue("--width"), "--width"));
        } else if (arg == "--height") {
            options.height = static_cast<uint32_t>(parseInt(requireValue("--height"), "--height"));
        } else if (arg == "--frames") {
            options.frames = parseInt(requireValue("--frames"), "--frames");
        } else if (arg == "--warmup") {
            options.warmup = parseInt(requireValue("--warmup"), "--warmup");
        } else if (arg == "--buffer-count") {
            options.bufferCount = parseInt(requireValue("--buffer-count"), "--buffer-count");
        } else if (arg == "--sample-count") {
            options.sampleCount = parseInt(requireValue("--sample-count"), "--sample-count");
        } else if (arg == "--output-dir") {
            options.outputDir = requireValue("--output-dir");
        } else if (arg == "--use-dmabuf") {
            options.useDmabuf = parseBool(requireValue("--use-dmabuf"));
        } else if (arg == "--format") {
            options.pixelFormatName = requireValue("--format");
            options.pixelFormat = parsePixelFormat(options.pixelFormatName, options.planeCount);
        } else {
            fail("Unknown argument: " + arg);
        }
    }

    if (options.frames <= 0) {
        fail("--frames must be > 0");
    }
    if (options.warmup < 0) {
        fail("--warmup must be >= 0");
    }
    if (options.bufferCount <= 1) {
        fail("--buffer-count must be > 1");
    }
    if (options.sampleCount < 0) {
        fail("--sample-count must be >= 0");
    }
    return options;
}

void configureLogging() {
    utils::LoggerConfig config = utils::LoggerConfig::defaultConfig();
    config.async = false;
    config.global_level = utils::LogLevel::INFO;
    utils::LoggerV2::init(config);
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseArgs(argc, argv);
        ensureDirectory(options.outputDir);
        configureLogging();

        if (options.useDmabuf) {
            DrmDev::fd_ptr = DeviceController::create();
            if (!DrmDev::fd_ptr) {
                fail("Failed to initialize DRM device for DMABUF capture");
            }
        }

        CameraController::Config config;
        config.device = options.device;
        config.width = options.width;
        config.height = options.height;
        config.format = options.pixelFormat;
        config.buffer_count = options.bufferCount;
        config.use_dmabuf = options.useDmabuf;
        config.plane_count = options.planeCount;

        RunState state(options);
        state.metrics.reserve(static_cast<size_t>(options.frames));
        state.sampleIndices = buildSampleIndices(options.frames, options.sampleCount);

        CameraController camera(config);
        camera.setFrameCallback([&state, &camera](FramePtr frame) {
            if (!frame) {
                return;
            }

            const uint64_t callbackUs = monotonicRawUs();
            FrameEvent event;
            bool shouldQueue = false;
            bool shouldSample = false;

            {
                std::lock_guard<std::mutex> lock(state.mutex);
                if (!state.stopRequested) {
                    ++state.observedFrames;
                    if (state.observedFrames > static_cast<uint64_t>(state.options.warmup) &&
                        state.queuedFrames < static_cast<uint64_t>(state.options.frames)) {
                        ++state.queuedFrames;
                        event.frameIndex = state.queuedFrames;
                        event.frameId = frame->meta.frame_id;
                        event.v4l2TimestampNs = frame->meta.timestamp_ns;
                        event.callbackTimestampUs = callbackUs;
                        event.bufferIndex = frame->index();
                        event.payloadBytes = frame->size();
                        event.width = frame->meta.w;
                        event.height = frame->meta.h;
                        shouldSample = state.sampleIndices.find(event.frameIndex) != state.sampleIndices.end();
                        shouldQueue = true;
                    }
                }
            }

            if (shouldSample) {
                event.sampleBytes = copyFrameBytes(frame, state.options.planeCount);
            }

            if (shouldQueue) {
                std::lock_guard<std::mutex> lock(state.mutex);
                if (!state.stopRequested) {
                    state.pending.push_back(std::move(event));
                }
            }

            camera.returnBuffer(frame->index());
            if (shouldQueue) {
                state.cv.notify_one();
            }
        });

        camera.start();

        const auto timeout = std::chrono::seconds(std::max(10, options.frames / 10 + options.warmup / 10 + 5));
        while (state.processedFrames < static_cast<uint64_t>(options.frames)) {
            FrameEvent event;
            {
                std::unique_lock<std::mutex> lock(state.mutex);
                if (!state.cv.wait_for(lock, timeout, [&state] { return !state.pending.empty(); })) {
                    fail("Capture timed out before collecting all requested frames");
                }
                event = std::move(state.pending.front());
                state.pending.pop_front();
            }

            if (state.actualWidth == 0 && state.actualHeight == 0) {
                state.actualWidth = event.width;
                state.actualHeight = event.height;
            }

            MetricSample metric;
            metric.frameIndex = event.frameIndex;
            metric.frameId = event.frameId;
            metric.v4l2TimestampNs = event.v4l2TimestampNs;
            metric.callbackTimestampUs = event.callbackTimestampUs;
            metric.frameIntervalUs = state.previousCallbackUs == 0 ? 0 : (event.callbackTimestampUs - state.previousCallbackUs);
            metric.bufferIndex = event.bufferIndex;
            metric.payloadBytes = event.payloadBytes;
            metric.width = event.width;
            metric.height = event.height;
            state.metrics.push_back(metric);
            state.previousCallbackUs = event.callbackTimestampUs;
            ++state.processedFrames;

            if (!event.sampleBytes.empty()) {
                PersistedFrameSample sample;
                sample.frameIndex = event.frameIndex;
                sample.bytes = std::move(event.sampleBytes);
                state.frameSamples.push_back(std::move(sample));
            }
        }

        {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.stopRequested = true;
        }
        camera.stop();

        std::vector<uint64_t> frameIntervals;
        frameIntervals.reserve(state.metrics.size());
        for (const auto& metric : state.metrics) {
            if (metric.frameIntervalUs > 0) {
                frameIntervals.push_back(metric.frameIntervalUs);
            }
        }

        const SummaryStats intervalStats = computeStats(frameIntervals);
        writeMetricsCsv(options.outputDir, state.metrics);
        writeSummary(options.outputDir,
                     options,
                     intervalStats,
                     state.metrics.size(),
                     state.frameSamples.size(),
                     state.actualWidth,
                     state.actualHeight);
        persistFrameSamples(options.outputDir, options, state.frameSamples);

        std::cout << "camera_perf completed\n"
                  << "  output_dir=" << options.outputDir << "\n"
                  << "  measured_frames=" << state.processedFrames << "\n"
                  << "  metric_rows=" << state.metrics.size() << "\n"
                  << "  sample_frames=" << state.frameSamples.size() << "\n"
                  << "  actual_resolution=" << state.actualWidth << "x" << state.actualHeight << "\n"
                  << "  frame_interval_mean_us=" << std::fixed << std::setprecision(3) << intervalStats.mean << "\n";

        utils::LoggerV2::shutdown();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "camera_perf failed: " << ex.what() << '\n';
        utils::LoggerV2::shutdown();
        return 1;
    }
}
