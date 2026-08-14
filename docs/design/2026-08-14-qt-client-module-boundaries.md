# Qt 클라이언트 모듈 경계 재구성 설계

## 목적

Qt 클라이언트의 `application`, `network`, `ui` 디렉터리를 각각 독립적인
CMake 라이브러리로 만든다. 현재처럼 하나의 `rss_qt_client_lib`가 Qt Core,
Network, Widgets와 `rss_protocol`을 모두 공개 전파하지 않도록 하고, 각
모듈이 필요한 의존성만 노출하게 한다.

이번 변경은 빌드 구조와 헤더 배치만 다룬다. 클라이언트의 상태 전이,
프로토콜 처리, TCP 동작, 화면 동작은 변경하지 않는다.

## 디렉터리 구조

```text
apps/qt-client/
├── CMakeLists.txt
├── src/main.cpp
├── application/
│   ├── CMakeLists.txt
│   ├── include/rss/qt_client/application/
│   │   ├── ClientController.h
│   │   ├── ClientState.h
│   │   └── SessionTransport.h
│   └── src/ClientController.cpp
├── network/
│   ├── CMakeLists.txt
│   ├── include/rss/qt_client/network/QtSessionClient.h
│   └── src/QtSessionClient.cpp
└── ui/
    ├── CMakeLists.txt
    ├── include/rss/qt_client/ui/MainWindow.h
    └── src/
        ├── MainWindow.cpp
        └── MainWindow.ui
```

공개 헤더는 저장소의 기존 규칙에 맞춰 `rss/...` 경로로 include한다.
namespace는 기존 `rss::qt_client`를 유지한다.

## 모듈과 의존성

### `rss_qt_client_application`

- `ClientController`, `ClientState`, `LogKind`, `SessionTransport`를 제공한다.
- `Qt6::Core`와 `rss_protocol`에만 의존한다.
- Widget과 Qt Network의 구체 클래스를 사용하지 않는다.
- `SessionTransport`는 네트워크 구현이 아니라 Controller가 요구하는 통신
  계약이므로 application 모듈에 둔다.

### `rss_qt_client_network`

- `QtSessionClient`를 제공하고 `SessionTransport`를 구현한다.
- `rss_qt_client_application`, `Qt6::Network`, `rss_protocol`에 의존한다.
- Widget과 UI 모듈을 알지 못한다.

### `rss_qt_client_ui`

- `MainWindow`와 Qt Designer 화면을 제공한다.
- `rss_qt_client_application`과 `Qt6::Widgets`에 의존한다.
- `QtSessionClient`, `QTcpSocket`, 패킷 코덱을 직접 사용하지 않는다.

### `rss_qt_client`

- `main.cpp`에서 network 구현, application Controller, UI를 조립한다.
- `rss_qt_client_network`와 `rss_qt_client_ui`를 링크한다.
- 기존과 동일하게 macOS bundle과 Windows GUI 실행 파일 속성을 유지한다.

의존 방향은 다음과 같다.

```text
rss_qt_client_ui ───────→ rss_qt_client_application
                                   ↑
rss_qt_client_network ─────────────┘
          │
          └────────────→ Qt6::Network, rss_protocol

rss_qt_client ─────────→ rss_qt_client_ui + rss_qt_client_network
```

application은 network와 ui를 알지 못하고, network와 ui도 서로 직접
의존하지 않는다.

## CMake 공개 범위

각 라이브러리는 자신의 공개 헤더가 요구하는 include 경로와 직접 의존성을
`PUBLIC`으로 제공한다. 구현 파일에서만 사용하는 설정은 `PRIVATE`으로
제한한다. 세 모듈이 하나의 타깃에 합쳐지지 않으므로 application 테스트에
Qt Network와 Widgets가 불필요하게 전파되지 않는다.

최상위 `apps/qt-client/CMakeLists.txt`는 모듈 subdirectory를 의존 순서대로
추가하고 실행 파일만 정의한다. 각 모듈의 소스와 링크 설정은 해당 모듈의
`CMakeLists.txt`가 소유한다.

## 테스트

기존 테스트의 의미와 테스트 이름은 유지한다.

- `rss_qt_client_controller_tests`는 `rss_qt_client_application`만 사용한다.
- `rss_qt_session_client_tests`는 `rss_qt_client_network`를 사용한다.
- `rss_qt_main_window_tests`는 `rss_qt_client_ui`와 테스트용
  `SessionTransport` 계약을 사용한다.

include 경로와 링크 타깃을 먼저 새 구조로 바꿔 configure 또는 compile이
실패하는 것을 확인한 다음 모듈을 생성한다. 구현 후에는 다음을 검증한다.

```bash
cmake --preset qt-client-dev -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build --preset qt-client-dev --parallel
ctest --preset qt-client-dev -R '^rss_qt_'
cmake --build build/qt-client-dev --target format-check
cmake --build build/qt-client-dev --target tidy-check
```

`tidy-check`가 환경에 제공되지 않으면 CMake configure 결과로 그 사실을
확인하고 나머지 검증을 수행한다.

## 문서 영향

빌드 preset과 실행 방법은 바뀌지 않는다. 정확한 소스 위치를 안내하는
`CONTRIBUTING.md`만 새 디렉터리 구조에 맞춘다. 기존 MVP 설계와 구현 계획은
당시 구조를 기록한 이력 문서이므로 수정하지 않는다.

## 제외 범위

- 클라이언트 동작과 화면 디자인 변경
- 프로토콜 또는 서버 변경
- namespace와 CMake 타깃의 `rss` 접두사 변경
- Qt 타입을 제거하는 프레임워크 독립 인터페이스 재설계
- 라이브러리 설치 또는 외부 패키지 export 지원
