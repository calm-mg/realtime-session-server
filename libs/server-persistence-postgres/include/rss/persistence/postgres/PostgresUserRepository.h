#pragma once

#include "rss/persistence/UserRepository.h"
#include "rss/persistence/postgres/PostgresExecutor.h"

namespace rss::persistence::postgres {

class PostgresUserRepository final : public UserRepository {
 public:
  explicit PostgresUserRepository(PostgresExecutor& executor);

  void findOrCreateByNormalizedName(FindOrCreateUser request,
                                    UserCallback callback) override;

 private:
  PostgresExecutor& executor_;
};

}  // namespace rss::persistence::postgres
