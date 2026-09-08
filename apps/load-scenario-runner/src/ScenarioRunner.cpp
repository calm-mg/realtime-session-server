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
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "EmbeddedServer.h"
#include "ScenarioClient.h"
#include "rss/net/ClientIoError.h"
#include "rss/net/ServerConfig.h"
#include "rss/protocol/PacketCodec.h"
#include "rss/protocol/PacketTypes.h"
#include "rss/protocol/ProtocolError.h"
#include "rss/protocol/StructuredPayload.h"

namespace rss::tools {
namespace {

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

std::string externalNamePrefix() {
  std::random_device random;
  const auto nonce = (static_cast<std::uint64_t>(random()) << 32U) | random();
  return "load-" + std::to_string(nonce) + "-";
}

void recordClientFailure(ClientFailureCounts& counts,
                         const std::exception_ptr& failure) {
  if (!failure) {
    return;
  }
  try {
    std::rethrow_exception(failure);
  } catch (const rss::net::ClientIoError& error) {
    switch (error.cause()) {
      case rss::net::ClientIoFailure::PeerClosed:
        ++counts.peer_closed;
        break;
      case rss::net::ClientIoFailure::SocketError:
        ++counts.socket_error;
        break;
      case rss::net::ClientIoFailure::Timeout:
        ++counts.timeout;
        break;
    }
  } catch (const rss::protocol::ProtocolError&) {
    ++counts.protocol;
  } catch (...) {
    ++counts.other;
  }
}

constexpr auto kSetupTimeout = std::chrono::seconds(30);
constexpr auto kReceiverPollInterval = std::chrono::milliseconds(10);

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

struct RoomSendProgress {
  explicit RoomSendProgress(const std::vector<std::size_t>& room_sizes)
      : successful_sends_by_room(room_sizes.size()),
        completed_senders_by_room(room_sizes.size()),
        sender_count_by_room(room_sizes) {}

  std::mutex mutex;
  std::vector<std::uint64_t> successful_sends_by_room;
  std::vector<std::size_t> completed_senders_by_room;
  std::vector<std::size_t> sender_count_by_room;
};

struct RoomSendSnapshot {
  std::uint64_t successful_sends{};
  bool all_senders_completed{};
};

struct ReceiveProgress {
  ReceiveProgress(std::size_t receiver_count,
                  std::size_t max_outstanding_messages)
      : received_by_client(receiver_count),
        max_outstanding_messages(max_outstanding_messages) {}

  std::mutex mutex;
  std::condition_variable changed;
  std::vector<std::uint64_t> received_by_client;
  std::uint64_t next_ticket{};
  std::size_t max_outstanding_messages{};
  bool failed{};
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

OverloadReport makeOverloadReport(const rss::net::OverloadSnapshot& snapshot) {
  return OverloadReport{
      .read_pauses = snapshot.read_pauses,
      .read_resumes = snapshot.read_resumes,
      .inbound_queue_full = snapshot.inbound_queue_full,
      .outbound_budget_rejections = snapshot.outbound_budget_rejections,
      .handler_exceptions = snapshot.handler_exceptions,
      .slow_client_disconnects = snapshot.slow_client_disconnects,
      .rejected_connections = snapshot.rejected_connections,
      .max_inbound_queue_size = snapshot.max_inbound_queue_size,
      .max_outbound_queue_size = snapshot.max_outbound_queue_size,
      .max_session_pending_write_bytes =
          snapshot.max_session_pending_write_bytes,
      .disconnect_peer_closed = snapshot.disconnect_peer_closed,
      .disconnect_socket_error = snapshot.disconnect_socket_error,
      .disconnect_protocol_error = snapshot.disconnect_protocol_error,
      .disconnect_idle_timeout = snapshot.disconnect_idle_timeout,
      .disconnect_worker_requested = snapshot.disconnect_worker_requested,
      .disconnect_pending_write_limit = snapshot.disconnect_pending_write_limit,
      .disconnect_close_after_flush = snapshot.disconnect_close_after_flush,
      .disconnect_shutdown = snapshot.disconnect_shutdown,
      .worker_parked_limit_failures = snapshot.worker_parked_limit_failures,
      .worker_invalid_sequence_failures =
          snapshot.worker_invalid_sequence_failures,
      .worker_deferred_failures = snapshot.worker_deferred_failures,
  };
}

std::size_t checkedProduct(std::size_t left, std::size_t right) {
  if (right != 0 && left > std::numeric_limits<std::size_t>::max() / right) {
    throw std::overflow_error("scenario receiver count overflow");
  }
  return left * right;
}

void recordReceiveProgress(ReceiveProgress& progress,
                           std::size_t receiver_index, std::uint64_t received) {
  {
    std::lock_guard lock(progress.mutex);
    progress.received_by_client[receiver_index] = received;
  }
  progress.changed.notify_all();
}

void recordProgressFailure(ReceiveProgress& progress) {
  {
    std::lock_guard lock(progress.mutex);
    progress.failed = true;
  }
  progress.changed.notify_all();
}

void recordSuccessfulSend(RoomSendProgress& progress, std::size_t room_index) {
  std::lock_guard lock(progress.mutex);
  auto& successful_sends = progress.successful_sends_by_room[room_index];
  if (successful_sends == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error("scenario successful send count overflow");
  }
  ++successful_sends;
}

void recordSenderCompleted(RoomSendProgress& progress, std::size_t room_index) {
  std::lock_guard lock(progress.mutex);
  ++progress.completed_senders_by_room[room_index];
}

RoomSendSnapshot roomSendSnapshot(RoomSendProgress& progress,
                                  std::size_t room_index) {
  std::lock_guard lock(progress.mutex);
  return RoomSendSnapshot{
      .successful_sends = progress.successful_sends_by_room[room_index],
      .all_senders_completed = progress.completed_senders_by_room[room_index] ==
                               progress.sender_count_by_room[room_index],
  };
}

std::vector<std::uint64_t> successfulSendsByRoom(RoomSendProgress& progress) {
  std::lock_guard lock(progress.mutex);
  return progress.successful_sends_by_room;
}

bool runTaskHook(const std::function<void()>& hook, std::exception_ptr& failure,
                 ReceiveProgress* progress) {
  try {
    if (hook) {
      hook();
    }
    return true;
  } catch (...) {
    failure = std::current_exception();
    if (progress != nullptr) {
      recordProgressFailure(*progress);
    }
    return false;
  }
}

void acquireSendPermit(ReceiveProgress& progress, std::size_t sender,
                       std::size_t sequence, std::size_t sender_count,
                       Deadline deadline) {
  const auto ticket_base = checkedProduct(sequence, sender_count);
  if (sender > std::numeric_limits<std::size_t>::max() - ticket_base) {
    throw std::overflow_error("scenario send ticket overflow");
  }
  const auto ticket = ticket_base + sender;

  std::unique_lock lock(progress.mutex);
  const auto acquired = progress.changed.wait_until(lock, deadline, [&] {
    if (progress.failed) {
      return true;
    }
    const auto minimum_received = *std::min_element(
        progress.received_by_client.begin(), progress.received_by_client.end());
    return ticket == progress.next_ticket &&
           (progress.next_ticket <= minimum_received ||
            progress.next_ticket - minimum_received <
                progress.max_outstanding_messages);
  });
  if (!acquired) {
    throw rss::net::ClientIoError(rss::net::ClientIoFailure::Timeout,
                                  "fast receiver progress timed out");
  }
  if (progress.failed) {
    throw std::runtime_error("scenario progress failed");
  }
  ++progress.next_ticket;
  lock.unlock();
  progress.changed.notify_all();
}

void receiveBroadcasts(ScenarioClient& client, std::size_t run_id,
                       std::size_t client_count,
                       std::size_t receiver_room_index, std::size_t room_count,
                       std::size_t room_size, std::size_t messages_per_sender,
                       Deadline deadline, ReceiveState& state,
                       ReceiveProgress* progress, std::size_t receiver_index,
                       RoomSendProgress& send_progress) {
  try {
    const auto maximum_expected =
        checkedProduct(room_size, messages_per_sender);
    state.latencies.reserve(maximum_expected);
    state.identities.reserve(maximum_expected);

    while (true) {
      const auto send_snapshot =
          roomSendSnapshot(send_progress, receiver_room_index);
      if (send_snapshot.all_senders_completed &&
          state.identities.size() >= send_snapshot.successful_sends) {
        break;
      }
      if (Clock::now() >= deadline) {
        throw rss::net::ClientIoError(rss::net::ClientIoFailure::Timeout,
                                      "scenario receive timed out");
      }

      const auto packet = client.tryReceivePacket(
          std::min(remainingTimeout(deadline), kReceiverPollInterval));
      if (!packet.has_value()) {
        continue;
      }
      if (packet->type != rss::protocol::PacketType::RoomBroadcast) {
        continue;
      }

      try {
        const auto broadcast = rss::protocol::StructuredPayload::parse(
            rss::protocol::payloadToString(*packet));
        if (broadcast.requireField("event") != "CHAT") {
          continue;
        }

        const auto identity = parsePayload(broadcast.requireField("message"));
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
          recordReceiveProgress(*progress, receiver_index, state.received);
        }
        const auto received_us = nowMicroseconds();
        const auto latency_us = received_us >= identity.sent_us
                                    ? received_us - identity.sent_us
                                    : 0;
        state.latencies.emplace_back(latency_us);
      } catch (const std::invalid_argument&) {
        ++state.unexpected;
      } catch (const rss::protocol::ProtocolError&) {
        ++state.unexpected;
      }
    }
  } catch (...) {
    state.failure = std::current_exception();
    if (progress != nullptr) {
      recordProgressFailure(*progress);
    }
  }
}

void waitForSendTime(ReceiveProgress& progress, Deadline next_send,
                     Deadline deadline) {
  std::unique_lock lock(progress.mutex);
  progress.changed.wait_until(lock, std::min(next_send, deadline),
                              [&] { return progress.failed; });
  if (Clock::now() >= deadline) {
    throw rss::net::ClientIoError(rss::net::ClientIoFailure::Timeout,
                                  "scenario send pacing timed out");
  }
  if (progress.failed) {
    throw std::runtime_error("scenario progress failed");
  }
}

void sendMessages(
    ScenarioClient& client, std::size_t run_id, std::size_t sender,
    std::size_t messages_per_sender, std::size_t payload_bytes,
    Deadline deadline, SendState& state, ReceiveProgress* progress,
    std::size_t sender_count,
    const std::function<void(std::size_t, std::size_t)>& before_send,
    RoomSendProgress& send_progress, std::size_t room_index,
    std::size_t local_sender, std::size_t rate_per_client) {
  try {
    const auto interval =
        rate_per_client == 0
            ? std::chrono::nanoseconds::zero()
            : std::chrono::nanoseconds((1000000000ULL + rate_per_client - 1) /
                                       rate_per_client);
    auto next_send = Clock::now();
    for (std::size_t sequence = 0; sequence < messages_per_sender; ++sequence) {
      if (progress != nullptr) {
        waitForSendTime(*progress, next_send, deadline);
        if (progress->max_outstanding_messages != 0) {
          acquireSendPermit(*progress, local_sender, sequence, sender_count,
                            deadline);
        }
      }
      if (before_send) {
        before_send(sender, sequence);
      }
      const auto payload = makePayload(run_id, sender, sequence,
                                       nowMicroseconds(), payload_bytes);
      client.sendChat(payload, remainingTimeout(deadline));
      next_send = Clock::now() + interval;
      ++state.sent;
      recordSuccessfulSend(send_progress, room_index);
    }
  } catch (...) {
    state.failure = std::current_exception();
    if (progress != nullptr) {
      recordProgressFailure(*progress);
    }
  }
  recordSenderCompleted(send_progress, room_index);
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
  validateScenarioOptions(options);

  const auto fast_client_count = options.scenario == ScenarioKind::SlowClient
                                     ? options.clients - options.slow_clients
                                     : options.clients;
  const auto room_count = options.scenario == ScenarioKind::MultiRoom
                              ? options.rooms
                              : std::size_t{1};

  ScenarioRunResult result;
  result.requested = options;
  result.server_stats_available = options.host.empty();
  result.effective_rooms = room_count;
  const auto pending_write_limit =
      options.scenario == ScenarioKind::SlowClient
          ? tuning_.slow_client_max_pending_write_bytes
          : tuning_.max_pending_write_bytes;

  const auto scenario_timeout = tuning_.scenario_timeout.value_or(
      std::chrono::seconds(options.timeout_seconds));
  if (scenario_timeout <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("scenario timeout must be positive");
  }
  result.effective_timeout_ms =
      static_cast<std::uint64_t>(scenario_timeout.count());
  result.effective_max_in_flight = options.max_in_flight;
  if (options.scenario == ScenarioKind::SlowClient) {
    const auto safe_window = std::max<std::size_t>(
        1, pending_write_limit / rss::protocol::kMaxPacketSize);
    if (options.max_in_flight > safe_window) {
      throw std::invalid_argument(
          "slow-client window exceeds pending write capacity");
    }
    if (options.max_in_flight == 0) {
      result.effective_max_in_flight = safe_window;
    }
  }

  rss::net::ServerConfig config;
  config.host = "127.0.0.1";
  config.port = 0;
  config.worker_count = options.worker_count;
  config.max_pending_write_bytes = pending_write_limit;
  config.max_sessions = tuning_.max_sessions;

  std::unique_ptr<EmbeddedServer> server;
  auto port = options.port;
  const auto host =
      options.host.empty() ? std::string{"127.0.0.1"} : options.host;
  if (options.host.empty()) {
    server = std::make_unique<EmbeddedServer>(config);
    server->start(kSetupTimeout);
    port = server->port();
  }
  const auto stop_server = [&] {
    if (server) {
      server->stop();
    }
  };

  try {
    const auto name_prefix = server ? std::string{} : externalNamePrefix();
    std::vector<ScenarioClient> clients;
    clients.reserve(options.clients);
    std::size_t completed_setup_clients{};
    std::vector<bool> setup_completed(options.clients);
    const auto markSetupCompleted = [&](std::size_t index) {
      if (!setup_completed[index]) {
        setup_completed[index] = true;
        ++completed_setup_clients;
      }
    };
    const auto setupFailureResult = [&] {
      result.failed_clients = options.clients - completed_setup_clients;
      recordClientFailure(result.client_failures.setup,
                          std::current_exception());
      if (server) {
        result.overload = makeOverloadReport(server->snapshot());
      }
      stop_server();
      return result;
    };

    for (std::size_t index = 0; index < options.clients; ++index) {
      auto& client = clients.emplace_back();
      try {
        client.connect(host, port, kSetupTimeout);
        client.login(
            (server ? "scenario-client-" : name_prefix) + std::to_string(index),
            kSetupTimeout);
      } catch (...) {
        return setupFailureResult();
      }
    }

    std::vector<std::size_t> room_sizes(room_count);
    std::vector<std::uint32_t> room_ids(room_count);
    for (std::size_t index = 0; index < clients.size(); ++index) {
      const auto room_index = index % room_count;
      ++room_sizes[room_index];
      try {
        if (index < room_count) {
          room_ids[room_index] = clients[index].createRoom(
              (server ? "scenario-room-" : name_prefix) +
                  std::to_string(room_index),
              kSetupTimeout);
        } else {
          clients[index].joinRoom(room_ids[room_index], kSetupTimeout);
        }
      } catch (...) {
        return setupFailureResult();
      }
      if (options.scenario != ScenarioKind::SlowClient ||
          index < fast_client_count) {
        markSetupCompleted(index);
      }
    }

    if (options.scenario == ScenarioKind::SlowClient) {
      for (std::size_t index = fast_client_count; index < clients.size();
           ++index) {
        try {
          clients[index].setReceiveBufferBytes(
              tuning_.socket_receive_buffer_bytes);
        } catch (...) {
          return setupFailureResult();
        }
        markSetupCompleted(index);
      }
      room_sizes.front() = fast_client_count;
    }

    std::vector<ReceiveState> receive_states(fast_client_count);
    std::vector<SendState> send_states(fast_client_count);
    RoomSendProgress send_progress(room_sizes);
    std::vector<std::unique_ptr<ReceiveProgress>> receive_progress;
    receive_progress.reserve(room_count);
    for (const auto room_size : room_sizes) {
      receive_progress.push_back(
          result.effective_max_in_flight != 0 || options.rate_per_client != 0
              ? std::make_unique<ReceiveProgress>(
                    room_size, result.effective_max_in_flight)
              : nullptr);
    }
    const auto task_count = checkedProduct(fast_client_count, 2);
    if (task_count == std::numeric_limits<std::size_t>::max() ||
        task_count + 1 > static_cast<std::size_t>(
                             std::numeric_limits<std::ptrdiff_t>::max())) {
      throw std::overflow_error("scenario barrier participant overflow");
    }
    const auto participant_count = task_count + 1;
    Deadline started_at;
    Deadline scenario_deadline;
    const auto start_measurement = [&]() noexcept {
      started_at = Clock::now();
      scenario_deadline = started_at + scenario_timeout;
    };
    std::barrier start_barrier(static_cast<std::ptrdiff_t>(participant_count),
                               start_measurement);
    std::vector<std::jthread> tasks;
    tasks.reserve(task_count);
    std::size_t launched{};

    try {
      for (std::size_t index = 0; index < fast_client_count; ++index) {
        auto* progress = receive_progress[index % room_count].get();
        tasks.emplace_back([&, index, progress] {
          const auto start_ready =
              runTaskHook(tuning_.before_measurement_start,
                          receive_states[index].failure, progress);
          start_barrier.arrive_and_wait();
          if (!start_ready) {
            return;
          }
          if (!runTaskHook(tuning_.before_receive,
                           receive_states[index].failure, progress)) {
            return;
          }
          const auto room_index = index % room_count;
          receiveBroadcasts(clients[index], run_id, fast_client_count,
                            room_index, room_count, room_sizes[room_index],
                            options.messages_per_sender, scenario_deadline,
                            receive_states[index], progress, index / room_count,
                            send_progress);
        });
        ++launched;

        tasks.emplace_back([&, index, progress] {
          const auto start_ready =
              runTaskHook(tuning_.before_measurement_start,
                          send_states[index].failure, progress);
          start_barrier.arrive_and_wait();
          if (!start_ready) {
            recordSenderCompleted(send_progress, index % room_count);
            return;
          }
          sendMessages(clients[index], run_id, index,
                       options.messages_per_sender, options.payload_bytes,
                       scenario_deadline, send_states[index], progress,
                       room_sizes[index % room_count], tuning_.before_send,
                       send_progress, index % room_count, index / room_count,
                       options.rate_per_client);
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

    start_barrier.arrive_and_wait();

    for (auto& task : tasks) {
      task.join();
    }
    const auto finished_at = Clock::now();

    const auto successful_sends = successfulSendsByRoom(send_progress);
    result.expected_broadcasts =
        expectedBroadcasts(room_sizes, successful_sends);
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
      recordClientFailure(result.client_failures.send,
                          send_states[index].failure);
      recordClientFailure(result.client_failures.receive,
                          receive_states[index].failure);
      if (send_states[index].failure != nullptr ||
          receive_states[index].failure != nullptr) {
        ++result.failed_clients;
      }
    }

    result.missing_broadcasts =
        result.received_broadcasts < result.expected_broadcasts
            ? result.expected_broadcasts - result.received_broadcasts
            : 0;
    if (server) {
      auto snapshot = server->snapshot();
      while (options.scenario == ScenarioKind::SlowClient &&
             snapshot.slow_client_disconnects < options.slow_clients &&
             Clock::now() < scenario_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
        snapshot = server->snapshot();
      }
      result.overload = makeOverloadReport(snapshot);
    }
    stop_server();
    return result;
  } catch (...) {
    const auto failure = std::current_exception();
    try {
      stop_server();
    } catch (...) {
    }
    std::rethrow_exception(failure);
  }
}

}  // namespace rss::tools
