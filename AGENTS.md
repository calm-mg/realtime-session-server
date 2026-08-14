# 저장소 작업 지침

## 프로젝트 개요

이 저장소는 C++20 실시간 세션 서버와 관련 클라이언트 및 도구를 함께
관리하는 모노레포다. Linux 전용 서버 네트워크 코드와 크로스플랫폼
프로토콜 및 서버 코어 코드를 명확히 분리한다.

## 디렉터리와 의존성

- `apps/`: 실행 파일 진입점과 애플리케이션별 조립 코드
- `libs/protocol`: 패킷 형식과 codec. 다른 프로젝트 라이브러리에 의존하지
  않는다.
- `libs/server-core`: 플랫폼 독립 도메인, 서비스, session, worker, queue
  코드. `protocol`에만 의존한다.
- `libs/server-net-linux`: epoll/eventfd/TCP 서버 코드. `server-core`와
  `protocol`에 의존한다.
- `libs/load-test-support`: 부하 테스트 통계 지원 코드
- `tests/`: 대상 라이브러리별 테스트
- `benchmarks/`: 마이크로벤치마크

라이브러리는 `apps/`에 의존할 수 없고, 플랫폼 독립 라이브러리는
`server-net-linux`에 의존할 수 없다.

## 플랫폼 규칙

- 전체 서버와 POSIX 콘솔 도구는 Linux에서만 빌드한다.
- macOS와 Windows에서는 플랫폼 독립 라이브러리, 테스트, 벤치마크를
  빌드한다.
- 소켓과 epoll 상태는 I/O 스레드만 변경한다.
- worker는 소켓을 직접 조작하지 않는다.

## 빌드와 검증

```bash
cmake --preset core-dev
cmake --build --preset core-dev
ctest --preset core-dev
```

Linux 전체 빌드는 `linux-dev`, 벤치마크는 `benchmark` preset을 사용한다.
변경 전 관련 테스트를 실행하고, 완료 전 build, test, `format-check`, 가능한
경우 `tidy-check`를 실행한다.

## 변경 규칙

- 프로토콜 변경 시 `docs/protocol.md`와 서버 및 클라이언트 테스트를 함께
  수정한다.
- 실행 인자나 빌드 절차 변경 시 `README.md`와 `CONTRIBUTING.md`를 함께
  수정한다.
- public include는 `rss/...` 표기를 유지한다.
- 관련 없는 리팩터링이나 formatting 변경을 같은 커밋에 섞지 않는다.

## 문서와 공개 저장소

- `AGENTS.md`와 프로젝트 문서의 본문은 한글을 기본으로 한다.
- 코드 식별자, API, 명령, 파일 및 디렉터리 이름은 영어를 유지할 수 있다.
- 비밀정보, 인증정보, 개인적 배경, 무관한 업무 맥락을 커밋하지 않는다.
- 특정 자동화 도구, 플러그인, 내부 작업 절차의 명칭이나 흔적을 공개 문서,
  경로, 커밋 메시지에 남기지 않는다. 코드베이스 유지보수에 필요한 일반적인
  기술 규칙과 결과만 기록한다.
