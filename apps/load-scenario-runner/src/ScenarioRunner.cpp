#include "ScenarioRunner.h"

#include <algorithm>
#include <barrier>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "EmbeddedServer.h"
#include "ScenarioClient.h"
#include "rss/net/ServerConfig.h"
#include "rss/protocol/PacketCodec.h"
#include "rss/protocol/PacketTypes.h"

namespace rss::tools {
namespace {

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

constexpr auto kSetupTimeout = std::chrono::seconds(30);

struct ParsedIdentity {
  std::size_t run{};
  std::size_t sender{};
  std::size_t sequence{};
  std::uint64_t sent_us{};
};

struct MessageKey {
  std::size_t sender{};
  std::size_t sequence{};

  bool operator==(const MessageKey&) const = default;
};

struct MessageKeyHash {
  std::size_t operator()(const MessageKey& key) const noexcept {
    const auto left = std::hash<std::size_t>{}(key.sender);
    const auto right = std::hash<std::size_t>{}(key.sequence);
    return left ^ (right + 0x9e3779b9U + (left << 6U) + (left >> 2U));
  }
};

struct ReceiveState {
  std::uint64_t received{};
  std::uint64_t duplicates{};
  std::uint64_t unexpected{};
  std::vector<std::chrono::microseconds> latencies;
  std::unordered_set<MessageKey, MessageKeyHash> identities;
  std::exception_ptr failure;
};

struct SendState {
  std::uint64_t sent{};
  std::exception_ptr failure;
};

struct ReceiveProgress {
  explicit ReceiveProgress(std::size_t receiver_count)
      : received_by_client(receiver_count) {}

  std::mutex mutex;
  std::condition_variable changed;
  std::vector<std::uint64_t> received_by_client;
  bool receiver_failed{};
};

std::uint64_t nowMicroseconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          Clock::now().time_since_epoch())
          .count());
}

std::chrono::milliseconds remainingTimeout(Deadline deadline) {
  const auto now = Clock::now();
  if (now >= deadline) {
    return std::chrono::milliseconds::zero();
  }

  const auto remaining = deadline - now;
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
  return milliseconds + (milliseconds < remaining
                             ? std::chrono::milliseconds(1)
                             : std::chrono::milliseconds::zero());
}

template <typename Integer>
Integer parseValue(std::string_view payload, std::string_view marker) {
  const auto begin_position = payload.find(marker);
  if (begin_position == std::string_view::npos) {
    throw std::invalid_argument("scenario payload is missing a field");
  }

  const auto value_begin = begin_position + marker.size();
  const auto value_end = payload.find(';', value_begin);
  if (value_end == std::string_view::npos || value_begin == value_end) {
    throw std::invalid_argument("scenario payload has an invalid field");
  }

  Integer value{};
  const auto* first = payload.data() + value_begin;
  const auto* last = payload.data() + value_end;
  const auto [parsed_end, error] = std::from_chars(first, last, value);
  if (error != std::errc{} || parsed_end != last) {
    throw std::invalid_argument("scenario payload has an invalid number");
  }
  return value;
}

ParsedIdentity parsePayload(std::string_view payload) {
  return ParsedIdentity{
      .run = parseValue<std::size_t>(payload, "run="),
      .sender = parseValue<std::size_t>(payload, "sender="),
      .sequence = parseValue<std::size_t>(payload, "seq="),
      .sent_us = parseValue<std::uint64_t>(payload, "sent_us="),
  };
}

std::string makePayload(std::size_t run, std::size_t sender,
                        std::size_t sequence, std::uint64_t sent_us,
                        std::size_t payload_bytes) {
  auto payload = "run=" + std::to_string(run) +
                 ";sender=" + std::to_string(sender) +
                 ";seq=" + std::to_string(sequence) +
                 ";sent_us=" + std::to_string(sent_us) + ";";
  if (payload.size() > payload_bytes) {
    throw std::invalid_argument("scenario payload size is too small");
  }
  payload.resize(payload_bytes, 'x');
  return payload;
}

std::string_view messagePayload(std::string_view broadcast) {
  constexpr std::string_view marker = "|message=";
  const auto position = broadcast.find(marker);
  if (position == std::string_view::npos) {
    throw std::invalid_argument("chat broadcast has no message");
  }
  return broadcast.substr(position + marker.size());
}

OverloadReport makeOverloadReport(const rss::net::OverloadSnapshot& snapshot) {
  return OverloadReport{
      .read_pauses = snapshot.read_pauses,
      .read_resumes = snapshot.read_resumes,
      .inbound_queue_full = snapshot.inbound_queue_full,
      .outbound_budget_rejections = snapshot.outbound_budget_rejections,
      .slow_client_disconnects = snapshot.slow_client_disconnects,
      .rejected_connections = snapshot.rejected_connections,
      .max_inbound_queue_size = snapshot.max_inbound_queue_size,
      .max_outbound_queue_size = snapshot.max_outbound_queue_size,
      .max_session_pending_write_bytes =
          snapshot.max_session_pending_write_bytes,
  };
}

std::size_t checkedProduct(std::size_t left, std::size_t right) {
  if (right != 0 && left > std::numeric_limits<std::size_t>::max() / right) {
    throw std::overflow_error("scenario receiver count overflow");
  }
  return left * right;
}

void receiveBroadcasts(ScenarioClient& client, std::size_t run_id,
                       std::size_t client_count,
                       std::size_t receiver_room_index, std::size_t room_count,
                       std::size_t room_size, std::size_t messages_per_sender,
                       Deadline deadline, ReceiveState& state,
                       ReceiveProgress* progress, std::size_t receiver_index) {
  try {
    const auto expected = checkedProduct(room_size, messages_per_sender);
    state.latencies.reserve(expected);
    state.identities.reserve(expected);

    while (state.identities.size() < expected) {
      const auto packet = client.receivePacket(remainingTimeout(deadline));
      if (packet.type != rss::protocol::PacketType::RoomBroadcast) {
        continue;
      }

      const auto broadcast = rss::protocol::payloadToString(packet);
      if (!broadcast.starts_with("event=CHAT|")) {
        continue;
      }

      try {
        const auto identity = parsePayload(messagePayload(broadcast));
        if (identity.run != run_id || identity.sender >= client_count ||
            identity.sender % room_count != receiver_room_index ||
            identity.sequence >= messages_per_sender) {
          ++state.unexpected;
          continue;
        }

        if (!state.identities
                 .insert(MessageKey{identity.sender, identity.sequence})
                 .second) {
          ++state.duplicates;
          continue;
        }

        ++state.received;
        if (progress != nullptr) {
          {
            std::lock_guard lock(progress->mutex);
            progress->received_by_client[receiver_index] = state.received;
          }
          progress->changed.notify_all();
        }
        const auto received_us = nowMicroseconds();
        const auto latency_us = received_us >= identity.sent_us
                                    ? received_us - identity.sent_us
                                    : 0;
        state.latencies.emplace_back(latency_us);
      } catch (const std::invalid_argument&) {
        ++state.unexpected;
      }
    }
  } catch (...) {
    state.failure = std::current_exception();
    if (progress != nullptr) {
      {
        std::lock_guard lock(progress->mutex);
        progress->receiver_failed = true;
      }
      progress->changed.notify_all();
    }
  }
}

void waitForFastReceivers(ReceiveProgress& progress,
                          std::uint64_t expected_per_receiver,
                          Deadline deadline) {
  std::unique_lock lock(progress.mutex);
  const auto completed = progress.changed.wait_until(lock, deadline, [&] {
    return progress.receiver_failed ||
           std::all_of(progress.received_by_client.begin(),
                       progress.received_by_client.end(),
                       [expected_per_receiver](std::uint64_t received) {
                         return received >= expected_per_receiver;
                       });
  });
  if (!completed) {
    throw std::runtime_error("fast receiver progress timed out");
  }
  if (progress.receiver_failed) {
    throw std::runtime_error("fast receiver failed");
  }
}

void sendMessages(ScenarioClient& client, std::size_t run_id,
                  std::size_t sender, std::size_t messages_per_sender,
                  std::size_t payload_bytes, Deadline deadline,
                  SendState& state, ReceiveProgress* progress,
                  std::size_t sender_count,
                  std::size_t progress_window_per_sender) {
  try {
    for (std::size_t sequence = 0; sequence < messages_per_sender; ++sequence) {
      const auto payload = makePayload(run_id, sender, sequence,
                                       nowMicroseconds(), payload_bytes);
      client.sendChat(payload, remainingTimeout(deadline));
      ++state.sent;
      if (progress != nullptr && sequence + 1 >= progress_window_per_sender) {
        const auto completed_rounds = sequence - progress_window_per_sender + 2;
        waitForFastReceivers(*progress,
                             checkedProduct(completed_rounds, sender_count),
                             deadline);
      }
    }
  } catch (...) {
    state.failure = std::current_exception();
  }
}

}  // namespace

std::string makeScenarioPayload(std::size_t run, std::size_t sender,
                                std::size_t sequence, std::uint64_t sent_us,
                                std::size_t payload_bytes) {
  return makePayload(run, sender, sequence, sent_us, payload_bytes);
}

MessageIdentity parseScenarioPayload(std::string_view payload) {
  const auto identity = parsePayload(payload);
  return MessageIdentity{
      .run = identity.run,
      .sender = identity.sender,
      .sequence = identity.sequence,
      .sent_us = identity.sent_us,
  };
}

ScenarioRunner::ScenarioRunner(ScenarioTuning tuning)
    : tuning_(std::move(tuning)) {}

ScenarioRunResult ScenarioRunner::runOnce(const ScenarioOptions& options,
                                          std::size_t run_id) const {
  if (options.clients == 0) {
    throw std::invalid_argument("scenario requires at least one client");
  }
  if (options.scenario == ScenarioKind::MultiRoom &&
      (options.rooms == 0 || options.rooms > options.clients)) {
    throw std::invalid_argument(
        "multi-room scenario has an invalid room count");
  }

  const auto fast_client_count = options.scenario == ScenarioKind::SlowClient
                                     ? options.clients - options.slow_clients
                                     : options.clients;

  rss::net::ServerConfig config;
  config.host = "127.0.0.1";
  config.port = 0;
  config.worker_count = options.worker_count;
  config.max_pending_write_bytes = tuning_.max_pending_write_bytes;

  EmbeddedServer server(config);
  server.start(kSetupTimeout);

  try {
    std::vector<ScenarioClient> clients;
    clients.reserve(options.clients);
    for (std::size_t index = 0; index < options.clients; ++index) {
      auto& client = clients.emplace_back();
      client.connect("127.0.0.1", server.port(), kSetupTimeout);
      client.login("scenario-client-" + std::to_string(index), kSetupTimeout);
    }

    const auto room_count = options.scenario == ScenarioKind::MultiRoom
                                ? options.rooms
                                : std::size_t{1};
    std::vector<std::size_t> room_sizes(room_count);
    std::vector<std::uint32_t> room_ids(room_count);
    for (std::size_t index = 0; index < clients.size(); ++index) {
      const auto room_index = index % room_count;
      ++room_sizes[room_index];
      if (index < room_count) {
        room_ids[room_index] = clients[index].createRoom(
            "scenario-room-" + std::to_string(room_index), kSetupTimeout);
      } else {
        clients[index].joinRoom(room_ids[room_index], kSetupTimeout);
      }
    }

    if (options.scenario == ScenarioKind::SlowClient) {
      for (std::size_t index = fast_client_count; index < clients.size();
           ++index) {
        clients[index].setReceiveBufferBytes(
            tuning_.socket_receive_buffer_bytes);
      }
      room_sizes.front() = fast_client_count;
    }

    std::vector<ReceiveState> receive_states(fast_client_count);
    std::vector<SendState> send_states(fast_client_count);
    ReceiveProgress receive_progress(fast_client_count);
    auto* progress = options.scenario == ScenarioKind::SlowClient
                         ? &receive_progress
                         : nullptr;
    const auto pending_packet_capacity =
        tuning_.max_pending_write_bytes / rss::protocol::kMaxPacketSize;
    const auto progress_window_per_sender = std::max<std::size_t>(
        1,
        pending_packet_capacity > fast_client_count
            ? (pending_packet_capacity - fast_client_count) / fast_client_count
            : 1);
    const auto participant_count = fast_client_count * 2 + 1;
    std::barrier start_barrier(static_cast<std::ptrdiff_t>(participant_count));
    std::vector<std::jthread> tasks;
    tasks.reserve(fast_client_count * 2);
    Deadline scenario_deadline;
    std::size_t launched{};

    try {
      for (std::size_t index = 0; index < fast_client_count; ++index) {
        tasks.emplace_back([&, index] {
          start_barrier.arrive_and_wait();
          const auto room_index = index % room_count;
          receiveBroadcasts(clients[index], run_id, fast_client_count,
                            room_index, room_count, room_sizes[room_index],
                            options.messages_per_sender, scenario_deadline,
                            receive_states[index], progress, index);
        });
        ++launched;

        tasks.emplace_back([&, index] {
          start_barrier.arrive_and_wait();
          sendMessages(clients[index], run_id, index,
                       options.messages_per_sender, options.payload_bytes,
                       scenario_deadline, send_states[index], progress,
                       fast_client_count, progress_window_per_sender);
        });
        ++launched;
      }
    } catch (...) {
      const auto missing_participants = participant_count - launched;
      for (std::size_t index = 0; index < missing_participants; ++index) {
        start_barrier.arrive_and_drop();
      }
      for (auto& task : tasks) {
        task.join();
      }
      throw;
    }

    const auto started_at = Clock::now();
    scenario_deadline = started_at + tuning_.scenario_timeout;
    start_barrier.arrive_and_wait();

    for (auto& task : tasks) {
      task.join();
    }
    const auto finished_at = Clock::now();

    ScenarioRunResult result;
    result.expected_broadcasts =
        expectedBroadcasts(room_sizes, options.messages_per_sender);
    result.elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        finished_at - started_at);

    for (std::size_t index = 0; index < fast_client_count; ++index) {
      result.sent += send_states[index].sent;
      result.received_broadcasts += receive_states[index].received;
      result.duplicate_broadcasts += receive_states[index].duplicates;
      result.unexpected_broadcasts += receive_states[index].unexpected;
      result.latencies.insert(result.latencies.end(),
                              receive_states[index].latencies.begin(),
                              receive_states[index].latencies.end());
      if (send_states[index].failure != nullptr ||
          receive_states[index].failure != nullptr) {
        ++result.failed_clients;
      }
    }

    result.missing_broadcasts =
        result.received_broadcasts < result.expected_broadcasts
            ? result.expected_broadcasts - result.received_broadcasts
            : 0;
    auto snapshot = server.snapshot();
    while (options.scenario == ScenarioKind::SlowClient &&
           snapshot.slow_client_disconnects < options.slow_clients &&
           Clock::now() < scenario_deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
      snapshot = server.snapshot();
    }
    result.overload = makeOverloadReport(snapshot);
    server.stop();
    return result;
  } catch (...) {
    const auto failure = std::current_exception();
    try {
      server.stop();
    } catch (...) {
    }
    std::rethrow_exception(failure);
  }
}

}  // namespace rss::tools
