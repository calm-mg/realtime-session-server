#include "rss/observability/OperationalLogFormatter.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace rss::observability {
namespace {

std::string escapeJson(std::string_view value) {
  std::ostringstream output;
  output << std::hex << std::uppercase << std::setfill('0');
  for (const unsigned char character : value) {
    switch (character) {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\b':
        output << "\\b";
        break;
      case '\f':
        output << "\\f";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (character < 0x20) {
          output << "\\u" << std::setw(4) << static_cast<unsigned>(character);
        } else {
          output << character;
        }
    }
  }
  return output.str();
}

std::string_view phaseName(SnapshotPhase phase) {
  switch (phase) {
    case SnapshotPhase::Periodic:
      return "periodic";
    case SnapshotPhase::Final:
      return "final";
  }
  return "unknown";
}

}  // namespace

std::int64_t currentUnixTimeMilliseconds() noexcept {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string formatServerStarted(std::int64_t timestamp_unix_ms,
                                std::string_view host, std::uint16_t port,
                                std::size_t worker_count) {
  std::ostringstream output;
  output << "{\"timestamp_unix_ms\":" << timestamp_unix_ms
         << ",\"level\":\"info\",\"event\":\"server_started\",\"host\":\""
         << escapeJson(host) << "\",\"port\":" << port
         << ",\"worker_count\":" << worker_count << "}\n";
  return output.str();
}

std::string formatOverloadSnapshot(std::int64_t timestamp_unix_ms,
                                   SnapshotPhase phase,
                                   const net::OverloadSnapshot& snapshot) {
  std::ostringstream output;
  output << "{\"timestamp_unix_ms\":" << timestamp_unix_ms
         << ",\"level\":\"info\",\"event\":\"overload_snapshot\",\"phase\":\""
         << phaseName(phase) << "\",\"read_pauses\":" << snapshot.read_pauses
         << ",\"read_resumes\":" << snapshot.read_resumes
         << ",\"inbound_queue_full\":" << snapshot.inbound_queue_full
         << ",\"outbound_budget_rejections\":"
         << snapshot.outbound_budget_rejections
         << ",\"handler_exceptions\":" << snapshot.handler_exceptions
         << ",\"slow_client_disconnects\":" << snapshot.slow_client_disconnects
         << ",\"rejected_connections\":" << snapshot.rejected_connections
         << ",\"max_inbound_queue_size\":" << snapshot.max_inbound_queue_size
         << ",\"max_outbound_queue_size\":" << snapshot.max_outbound_queue_size
         << ",\"max_session_pending_write_bytes\":"
         << snapshot.max_session_pending_write_bytes
         << ",\"current_inbound_queue_size\":"
         << snapshot.current_inbound_queue_size
         << ",\"current_outbound_queue_size\":"
         << snapshot.current_outbound_queue_size
         << ",\"outbound_queue_waiting_producers\":"
         << snapshot.outbound_queue_waiting_producers
         << ",\"current_sessions\":" << snapshot.current_sessions
         << ",\"outbound_queue_closed\":"
         << (snapshot.outbound_queue_closed ? "true" : "false") << "}\n";
  return output.str();
}

std::string formatServerStopped(std::int64_t timestamp_unix_ms) {
  return "{\"timestamp_unix_ms\":" + std::to_string(timestamp_unix_ms) +
         ",\"level\":\"info\",\"event\":\"server_stopped\"}\n";
}

std::string formatServerFailed(std::int64_t timestamp_unix_ms,
                               std::string_view message) {
  return "{\"timestamp_unix_ms\":" + std::to_string(timestamp_unix_ms) +
         ",\"level\":\"error\",\"event\":\"server_failed\",\"message\":\"" +
         escapeJson(message) + "\"}\n";
}

}  // namespace rss::observability
