# 프로토콜 버전 협상 구현 계획

목표: 승인된 [버전 협상 설계](../design/2026-09-07-protocol-version-negotiation.md)를 서버와 모든 클라이언트에 적용한다.

## 공용 인터페이스

`rss/protocol/ProtocolVersion.h`에 다음 인터페이스를 둔다.

```cpp
inline constexpr std::uint16_t kMinProtocolVersion = 1;
inline constexpr std::uint16_t kMaxProtocolVersion = 1;
inline constexpr std::chrono::milliseconds kVersionNegotiationTimeout{5000};
struct VersionRange {
  std::uint16_t min_version;
  std::uint16_t max_version;
};
std::string encodeVersionRequest(VersionRange range);
VersionRange decodeVersionRequest(std::string_view payload);
std::string encodeVersionResponse(std::uint16_t version);
std::uint16_t decodeVersionResponse(std::string_view payload);
std::optional<std::uint16_t> negotiateVersion(VersionRange range);
```

구문 오류는 `ProtocolError`로 표현한다. `PacketType::VersionReq`는 3,
`PacketType::VersionRes`는 4로 고정한다.

## 작업 순서

- [x] protocol: 요청·응답 parsing과 교집합 테스트를 먼저 추가한 뒤 codec 구현.
- [x] Qt: 연결 후 자동 협상과 5초 timeout, 실패 시 종료, 성공 후 로그인 활성화.
  기존 fake transport 준비 절차와 실제 소켓 테스트도 갱신.
- [x] 서버: router의 연결별 협상 상태를 동기화하고 단절 시 정리. 미협상·실패
  연결의 업무 요청 차단. ERROR 송신 후 종료를 I/O 스레드에서 처리.
- [x] 콘솔·부하 도구: 연결 뒤 협상 성공을 검증한 후 입력·로그인·측정 시작.
  실패와 timeout을 기존 오류 경로로 전달.
- [x] 테스트: 서버의 정상 연결 준비에 협상을 명시하고 구버전 거절 테스트 유지.
  byte fragmentation, 중복 요청, 연결별 격리, drain 후 종료 회귀 확인.
- [x] 문서: protocol.md, README.md, CONTRIBUTING.md, project-status.md와 roadmap.md 갱신.
- [x] 검증: core-dev 및 qt-client-dev build/test, format-check, 실제 소스 대상
  tidy-check. Linux 전체 검증은 사용 가능한 Linux 실행 환경에서 수행.
- [x] 변경 전체를 리뷰하고 필수 지적을 해결한 뒤 완료 여부 보고.

## 검증 결과

- Linux 전체 226개 테스트와 macOS 코어 149개 테스트 통과.
- macOS Qt 포함 162개, Clang 18 `-O1` ASan·UBSan Linux 전체 226개 통과.
- Qt의 자동 협상·오류·실제 timeout·재접속과 실제 소켓 분할 응답 검증.
- 콘솔 및 PING 실행 파일의 정상 연결·버전 불일치 종료 smoke 검증.
- 송신 예산이 협상 응답보다 작을 때 연결이 남는 경계 사례를 재현하고 수정.
- 정적 분석과 형식 검사에서 프로젝트 소스를 실제 대상으로 검사.
