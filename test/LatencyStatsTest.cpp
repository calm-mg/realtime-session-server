#include "TestSupport.h"

#include "rss/tools/LatencyStats.h"

#include <chrono>
#include <vector>

int main()
{
    using rss::tools::LatencyReport;
    using rss::tools::latencyReport;
    using Sample = std::chrono::microseconds;

    const std::vector<Sample> samples{
        Sample{3000},
        Sample{1000},
        Sample{5000},
        Sample{2000},
        Sample{4000},
    };

    const LatencyReport report = latencyReport(samples);
    RSS_EXPECT(report.sample_count == 5);
    RSS_EXPECT(report.min_us == 1000);
    RSS_EXPECT(report.p50_us == 3000);
    RSS_EXPECT(report.p95_us == 5000);
    RSS_EXPECT(report.p99_us == 5000);
    RSS_EXPECT(report.max_us == 5000);

    const LatencyReport empty = latencyReport({});
    RSS_EXPECT(empty.sample_count == 0);
    RSS_EXPECT(empty.min_us == 0);
    RSS_EXPECT(empty.p50_us == 0);
    RSS_EXPECT(empty.p95_us == 0);
    RSS_EXPECT(empty.p99_us == 0);
    RSS_EXPECT(empty.max_us == 0);

    testPassed("LatencyStatsTest");
    return 0;
}
