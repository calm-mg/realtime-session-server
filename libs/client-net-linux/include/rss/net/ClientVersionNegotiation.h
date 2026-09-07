#pragma once

#include <chrono>
#include <cstdint>

namespace rss::net {

// 연결된 소켓의 버전을 협상한다. 실패하면 예외를 던지며 소켓은 호출자가 닫는다.
// deadline과 협상 시작 후 5초 중 먼저 도달한 시각을 전체 송수신에 적용한다.
// 성공 시 선택된 버전을 반환하고 뒤따르는 frame은 소켓에 남겨 둔다.
std::uint16_t negotiateClientVersion(
    int fd, std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::time_point::max());

}  // namespace rss::net
