#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rss::tools {
namespace detail {

inline std::int64_t nearestRank(const std::vector<std::int64_t>& sorted, std::size_t numerator, std::size_t denominator)
{
    if (sorted.empty()) {
        return 0;
    }

    const auto rank = ((sorted.size() * numerator) + denominator - 1) / denominator;
    return sorted[std::min(rank, sorted.size()) - 1];
}

} // namespace detail

struct LatencyReport {
    std::size_t sample_count{};
    std::int64_t min_us{};
    std::int64_t p50_us{};
    std::int64_t p95_us{};
    std::int64_t p99_us{};
    std::int64_t max_us{};
};

inline LatencyReport latencyReport(const std::vector<std::chrono::microseconds>& samples)
{
    LatencyReport report{};
    report.sample_count = samples.size();
    if (samples.empty()) {
        return report;
    }

    std::vector<std::int64_t> values;
    values.reserve(samples.size());
    for (const auto sample : samples) {
        values.push_back(sample.count());
    }
    std::sort(values.begin(), values.end());

    report.min_us = values.front();
    report.p50_us = detail::nearestRank(values, 50, 100);
    report.p95_us = detail::nearestRank(values, 95, 100);
    report.p99_us = detail::nearestRank(values, 99, 100);
    report.max_us = values.back();
    return report;
}

} // namespace rss::tools
