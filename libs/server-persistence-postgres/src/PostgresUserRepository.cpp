#include "rss/persistence/postgres/PostgresUserRepository.h"

#include <libpq-fe.h>

#include <atomic>
#include <memory>
#include <string_view>
#include <utility>

namespace rss::persistence::postgres {
namespace {

constexpr auto kFindOrCreateSql = R"sql(
WITH inserted AS (
  INSERT INTO users(normalized_name, display_name)
  VALUES ($1, $2)
  ON CONFLICT (normalized_name) DO NOTHING
  RETURNING user_id::text, normalized_name, display_name
)
SELECT user_id, normalized_name, display_name FROM inserted
UNION ALL
SELECT user_id::text, normalized_name, display_name
FROM users
WHERE normalized_name = $1
LIMIT 1
)sql";

constexpr auto kFindSql = R"sql(
SELECT user_id::text, normalized_name, display_name
FROM users
WHERE normalized_name = $1
LIMIT 1
)sql";

class Result final {
 public:
  explicit Result(PGresult* result) : result_(result) {}
  ~Result() {
    if (result_ != nullptr) {
      PQclear(result_);
    }
  }

  Result(const Result&) = delete;
  Result& operator=(const Result&) = delete;

  [[nodiscard]] PGresult* get() const noexcept { return result_; }

 private:
  PGresult* result_{};
};

struct CallbackState {
  explicit CallbackState(UserCallback callback_value)
      : callback(std::move(callback_value)) {}

  std::atomic<bool> completed{false};
  UserCallback callback;
};

void deliver(const std::shared_ptr<CallbackState>& state,
             UserResult result) noexcept {
  bool expected = false;
  if (!state->completed.compare_exchange_strong(expected, true)) {
    return;
  }
  try {
    state->callback(std::move(result));
  } catch (...) {
  }
}

[[nodiscard]] UserResult failure(PersistenceErrorKind kind,
                                 std::string message) {
  return {std::nullopt, PersistenceError{kind, std::move(message)}};
}

[[nodiscard]] PersistenceErrorKind resultErrorKind(PGconn* connection,
                                                   PGresult* result) {
  if (PQstatus(connection) != CONNECTION_OK) {
    return PersistenceErrorKind::Unavailable;
  }
  const auto* sql_state = result == nullptr
                              ? nullptr
                              : PQresultErrorField(result, PG_DIAG_SQLSTATE);
  if (sql_state != nullptr && std::string_view{sql_state} == "57014") {
    return PersistenceErrorKind::Timeout;
  }
  if (sql_state != nullptr && sql_state[0] == '2' && sql_state[1] == '3') {
    return PersistenceErrorKind::Constraint;
  }
  return PersistenceErrorKind::Unavailable;
}

[[nodiscard]] UserResult decodeUser(PGresult* result) {
  if (result == nullptr || PQntuples(result) != 1 || PQnfields(result) < 3 ||
      PQgetisnull(result, 0, 0) != 0 || PQgetisnull(result, 0, 1) != 0 ||
      PQgetisnull(result, 0, 2) != 0) {
    return failure(PersistenceErrorKind::InvalidData,
                   "invalid user repository result");
  }
  const auto id = domain::UserId::parse(PQgetvalue(result, 0, 0));
  if (!id.has_value()) {
    return failure(PersistenceErrorKind::InvalidData,
                   "invalid user identifier");
  }
  return {UserRecord{*id, PQgetvalue(result, 0, 1), PQgetvalue(result, 0, 2)},
          std::nullopt};
}

[[nodiscard]] Result execute(PGconn* connection, const char* sql,
                             const FindOrCreateUser& request,
                             int parameter_count) {
  const char* values[] = {request.normalized_name.c_str(),
                          request.display_name.c_str()};
  return Result{PQexecParams(connection, sql, parameter_count, nullptr, values,
                             nullptr, nullptr, 0)};
}

}  // namespace

PostgresUserRepository::PostgresUserRepository(PostgresExecutor& executor)
    : executor_(executor) {}

void PostgresUserRepository::findOrCreateByNormalizedName(
    FindOrCreateUser request, UserCallback callback) {
  auto state = std::make_shared<CallbackState>(std::move(callback));
  bool accepted = false;
  try {
    accepted = executor_.submit(
        [request = std::move(request), state](pg_conn* opaque_connection) {
          try {
            auto* connection = static_cast<PGconn*>(opaque_connection);
            auto result = execute(connection, kFindOrCreateSql, request, 2);
            if (result.get() == nullptr ||
                PQresultStatus(result.get()) != PGRES_TUPLES_OK) {
              deliver(state, failure(resultErrorKind(connection, result.get()),
                                     "user persistence query failed"));
              return;
            }
            if (PQntuples(result.get()) == 1) {
              deliver(state, decodeUser(result.get()));
              return;
            }

            auto retry = execute(connection, kFindSql, request, 1);
            if (retry.get() == nullptr ||
                PQresultStatus(retry.get()) != PGRES_TUPLES_OK) {
              deliver(state, failure(resultErrorKind(connection, retry.get()),
                                     "user persistence query failed"));
              return;
            }
            if (PQntuples(retry.get()) != 1) {
              deliver(state, failure(PersistenceErrorKind::Unavailable,
                                     "user persistence result unavailable"));
              return;
            }
            deliver(state, decodeUser(retry.get()));
          } catch (...) {
            deliver(state, failure(PersistenceErrorKind::Unavailable,
                                   "user persistence task failed"));
          }
        });
  } catch (...) {
    deliver(state, failure(PersistenceErrorKind::Unavailable,
                           "user persistence submission failed"));
    return;
  }

  if (!accepted) {
    const auto kind = executor_.isRunning() ? PersistenceErrorKind::Busy
                                            : PersistenceErrorKind::Stopping;
    deliver(state, failure(kind, kind == PersistenceErrorKind::Busy
                                     ? "user persistence queue is full"
                                     : "user persistence is stopping"));
  }
}

}  // namespace rss::persistence::postgres
