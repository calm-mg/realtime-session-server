# Qt 클라이언트 모듈 경계 재구성 구현 계획

**목표:** Qt 클라이언트의 application, network, ui를 독립 CMake
라이브러리와 `rss/...` 공개 헤더 구조로 분리한다.

**아키텍처:** `SessionTransport` 계약과 `ClientController`는 application이
소유하고, network와 ui가 application에만 의존한다. 실행 파일은 network와
ui의 구체 객체를 조립하며 기존 동작과 테스트 이름을 유지한다.

**기술 스택:** C++20, Qt 6 Core/Network/Widgets/Test, CMake, Ninja, Qt Test

## 전역 제약

- Qt 최소 버전은 기존과 동일한 6.5다.
- 지원 플랫폼은 Linux, macOS, Windows다.
- `ClientController`는 Widget과 Qt Network 구체 클래스에 의존하지 않는다.
- 공개 include는 `rss/...` 형식을 사용한다.
- `ui_MainWindow.h`는 AUTOUIC 생성 파일이며 커밋하지 않는다.
- 상태 전이, 프로토콜, TCP, 화면 동작은 변경하지 않는다.
- 기존 Qt 테스트 세 실행 파일의 이름과 테스트 의미를 유지한다.
- 빌드와 실행 명령은 변경하지 않는다.

## 파일 구성

### 생성 후 기존 파일을 대체하는 경로

- `apps/qt-client/application/CMakeLists.txt`: application 타깃과 의존성
- `apps/qt-client/application/include/rss/qt_client/application/ClientController.h`:
  Controller 공개 API
- `apps/qt-client/application/include/rss/qt_client/application/ClientState.h`:
  상태와 로그 종류
- `apps/qt-client/application/include/rss/qt_client/application/SessionTransport.h`:
  Controller가 요구하는 통신 계약
- `apps/qt-client/application/src/ClientController.cpp`: 상태 전이 구현
- `apps/qt-client/network/CMakeLists.txt`: network 타깃과 의존성
- `apps/qt-client/network/include/rss/qt_client/network/QtSessionClient.h`:
  Qt TCP 구현 공개 API
- `apps/qt-client/network/src/QtSessionClient.cpp`: Qt TCP 구현
- `apps/qt-client/ui/CMakeLists.txt`: ui 타깃과 의존성
- `apps/qt-client/ui/include/rss/qt_client/ui/MainWindow.h`: Widget 공개 API
- `apps/qt-client/ui/src/MainWindow.cpp`: Widget 동작
- `apps/qt-client/ui/src/MainWindow.ui`: Qt Designer 화면

### 수정 파일

- `apps/qt-client/CMakeLists.txt`: 하위 모듈 추가와 실행 파일 조립
- `apps/qt-client/src/main.cpp`: 새 공개 include 경로 사용
- `tests/qt-client/CMakeLists.txt`: 테스트별 최소 타깃 링크
- `tests/qt-client/FakeSessionTransport.h`: application 공개 include 사용
- `tests/qt-client/ClientControllerTest.cpp`: application 공개 include 사용
- `tests/qt-client/QtSessionClientTest.cpp`: network 공개 include 사용
- `tests/qt-client/MainWindowTest.cpp`: application과 ui 공개 include 사용
- `CONTRIBUTING.md`: 새 화면 및 네트워크 소스 위치 안내

---

## 작업 1: 새 모듈 계약으로 테스트를 먼저 전환

**파일:**

- 수정: `tests/qt-client/CMakeLists.txt`
- 수정: `tests/qt-client/FakeSessionTransport.h`
- 수정: `tests/qt-client/ClientControllerTest.cpp`
- 수정: `tests/qt-client/QtSessionClientTest.cpp`
- 수정: `tests/qt-client/MainWindowTest.cpp`

**사용 계약:**

- application: `rss/qt_client/application/ClientController.h`,
  `rss/qt_client/application/SessionTransport.h`
- network: `rss/qt_client/network/QtSessionClient.h`
- ui: `rss/qt_client/ui/MainWindow.h`

**제공 결과:** 새 공개 include와 타깃 이름이 없어서 실패하는 빌드 검증

- [ ] **1.1 테스트 include를 새 공개 경로로 바꾼다.**

다음 치환을 적용한다.

```text
application/ClientController.h
  → rss/qt_client/application/ClientController.h
network/SessionTransport.h
  → rss/qt_client/application/SessionTransport.h
network/QtSessionClient.h
  → rss/qt_client/network/QtSessionClient.h
ui/MainWindow.h
  → rss/qt_client/ui/MainWindow.h
```

- [ ] **1.2 테스트 링크를 목표 모듈 이름으로 바꾼다.**

`tests/qt-client/CMakeLists.txt`에서 다음 링크 경계를 사용한다.

```cmake
target_link_libraries(rss_qt_client_controller_tests
    PRIVATE rss_qt_client_application Qt6::Test)

target_link_libraries(rss_qt_session_client_tests
    PRIVATE rss_qt_client_network Qt6::Test)

target_link_libraries(rss_qt_main_window_tests
    PRIVATE
        rss_qt_client_application
        rss_qt_client_ui
        Qt6::Test
)
```

- [ ] **1.3 테스트 타깃 빌드 실패를 확인한다.**

실행:

```bash
cmake --preset qt-client-dev -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build/qt-client-dev --target rss_qt_client_controller_tests
```

예상 결과: 아직 생성되지 않은 `rss/qt_client/application/ClientController.h`
공개 헤더를 찾지 못해 컴파일이 실패한다. 실패 원인이 새 모듈 계약의
부재인지 확인한다.

---

## 작업 2: 세 모듈 분리와 실행 파일 조립

**파일:**

- 생성: `apps/qt-client/application/CMakeLists.txt`
- 이동: `apps/qt-client/src/application/ClientController.h` →
  `apps/qt-client/application/include/rss/qt_client/application/ClientController.h`
- 이동: `apps/qt-client/src/application/ClientState.h` →
  `apps/qt-client/application/include/rss/qt_client/application/ClientState.h`
- 이동: `apps/qt-client/src/network/SessionTransport.h` →
  `apps/qt-client/application/include/rss/qt_client/application/SessionTransport.h`
- 이동: `apps/qt-client/src/application/ClientController.cpp` →
  `apps/qt-client/application/src/ClientController.cpp`
- 생성: `apps/qt-client/network/CMakeLists.txt`
- 이동: `apps/qt-client/src/network/QtSessionClient.h` →
  `apps/qt-client/network/include/rss/qt_client/network/QtSessionClient.h`
- 이동: `apps/qt-client/src/network/QtSessionClient.cpp` →
  `apps/qt-client/network/src/QtSessionClient.cpp`
- 생성: `apps/qt-client/ui/CMakeLists.txt`
- 이동: `apps/qt-client/src/ui/MainWindow.h` →
  `apps/qt-client/ui/include/rss/qt_client/ui/MainWindow.h`
- 이동: `apps/qt-client/src/ui/MainWindow.cpp` →
  `apps/qt-client/ui/src/MainWindow.cpp`
- 이동: `apps/qt-client/src/ui/MainWindow.ui` →
  `apps/qt-client/ui/src/MainWindow.ui`
- 수정: `apps/qt-client/CMakeLists.txt`
- 수정: `apps/qt-client/src/main.cpp`

**사용 계약:** `rss_protocol`, Qt Core, Qt Network, Qt Widgets와 기존
`rss::qt_client` 공개 타입

**제공 결과:** `rss_qt_client_application`, `rss_qt_client_network`,
`rss_qt_client_ui`와 기존 `rss_qt_client` 실행 파일

- [ ] **2.1 application 파일을 새 공개/구현 경로로 이동한다.**

파일 내용은 바꾸지 않고 경로만 옮긴다. `SessionTransport.h`는 Controller가
요구하는 추상 계약이므로 application 공개 헤더에 둔다.

- [ ] **2.2 application 내부 include를 공개 경로로 바꾼다.**

```cpp
#include "rss/qt_client/application/ClientController.h"
#include "rss/qt_client/application/ClientState.h"
#include "rss/qt_client/application/SessionTransport.h"
```

- [ ] **2.3 application CMake 타깃을 정의한다.**

```cmake
add_library(rss_qt_client_application STATIC
    src/ClientController.cpp
    include/rss/qt_client/application/ClientController.h
    include/rss/qt_client/application/ClientState.h
    include/rss/qt_client/application/SessionTransport.h
)

target_include_directories(rss_qt_client_application
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(rss_qt_client_application
    PUBLIC Qt6::Core rss_protocol)

target_compile_features(rss_qt_client_application PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_qt_client_application)
```

- [ ] **2.4 network 파일을 이동하고 include를 갱신한다.**

```cpp
#include "rss/qt_client/application/SessionTransport.h"
#include "rss/qt_client/network/QtSessionClient.h"
```

- [ ] **2.5 network CMake 타깃을 정의한다.**

```cmake
add_library(rss_qt_client_network STATIC
    src/QtSessionClient.cpp
    include/rss/qt_client/network/QtSessionClient.h
)

target_include_directories(rss_qt_client_network
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(rss_qt_client_network
    PUBLIC
        rss_qt_client_application
        Qt6::Network
        rss_protocol
)

target_compile_features(rss_qt_client_network PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_qt_client_network)
```

- [ ] **2.6 ui 파일을 이동하고 include를 갱신한다.**

```cpp
#include "rss/qt_client/application/ClientController.h"
#include "rss/qt_client/ui/MainWindow.h"
```

`MainWindow.cpp`의 `#include "ui_MainWindow.h"`는 AUTOUIC 생성 헤더이므로
그대로 유지한다.

- [ ] **2.7 ui CMake 타깃을 정의한다.**

```cmake
add_library(rss_qt_client_ui STATIC
    src/MainWindow.cpp
    src/MainWindow.ui
    include/rss/qt_client/ui/MainWindow.h
)

target_include_directories(rss_qt_client_ui
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(rss_qt_client_ui
    PUBLIC rss_qt_client_application Qt6::Widgets)

target_compile_features(rss_qt_client_ui PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_qt_client_ui)

if(MSVC)
    target_compile_options(rss_qt_client_ui PRIVATE /utf-8)
endif()
```

- [ ] **2.8 최상위 Qt 클라이언트 CMake를 조립 전용으로 바꾼다.**

```cmake
add_subdirectory(application)
add_subdirectory(network)
add_subdirectory(ui)

qt_add_executable(rss_qt_client src/main.cpp)
target_link_libraries(rss_qt_client
    PRIVATE
        rss_qt_client_network
        rss_qt_client_ui
        Qt6::Widgets
)
```

기존 `MACOSX_BUNDLE`, `RUNTIME_OUTPUT_DIRECTORY`, `WIN32_EXECUTABLE` 속성과
warning 설정은 유지하고 `rss_qt_client_lib` 정의는 제거한다.

- [ ] **2.9 `main.cpp`를 새 공개 include로 전환한다.**

```cpp
#include "rss/qt_client/application/ClientController.h"
#include "rss/qt_client/network/QtSessionClient.h"
#include "rss/qt_client/ui/MainWindow.h"
```

- [ ] **2.10 configure와 application 타깃 빌드를 통과시킨다.**

실행:

```bash
cmake --preset qt-client-dev -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build/qt-client-dev --target rss_qt_client_application
cmake --build --preset qt-client-dev --parallel
```

예상 결과: application 타깃이 독립적으로 컴파일되고, 새 세 라이브러리,
실행 파일, 기존 테스트가 모두 빌드된다.

---

## 작업 3: 동작 보존, 의존 경계와 문서 검증

**파일:**

- 수정: `CONTRIBUTING.md`
- 검증: `apps/qt-client/**`, `tests/qt-client/**`

**사용 계약:** 작업 1~2에서 생성한 타깃과 공개 헤더

**제공 결과:** 기존 동작을 보존하고 문서가 실제 경로와 일치하는 모듈 구조

- [ ] **3.1 Qt 테스트 세 실행 파일을 실행한다.**

```bash
ctest --preset qt-client-dev -R '^rss_qt_'
```

예상 결과: controller, network, MainWindow 테스트가 모두 통과한다. 로컬
루프백 포트가 제한된 환경에서는 동일 명령을 포트 사용이 허용된 환경에서
다시 실행한다.

- [ ] **3.2 CMake 의존 그래프에서 단방향 경계를 확인한다.**

```bash
cmake --graphviz=build/qt-client-dev/qt-client-targets.dot \
  build/qt-client-dev
rg 'rss_qt_client_(application|network|ui)' \
  build/qt-client-dev/qt-client-targets.dot
```

예상 결과: application에서 network 또는 ui로 향하는 edge가 없고, network와
ui가 서로 직접 연결되지 않는다.

- [ ] **3.3 CONTRIBUTING의 소스 위치를 갱신한다.**

화면 배치는 `apps/qt-client/ui/src/MainWindow.ui`, 상태와 사용자 동작은
`apps/qt-client/ui/src/MainWindow.cpp`, TCP 통신은
`apps/qt-client/network`에서 관리한다고 기록한다.

- [ ] **3.4 코드 품질 검사를 실행한다.**

```bash
cmake --build build/qt-client-dev --target format-check
cmake --build build/qt-client-dev --target tidy-check
```

예상 결과: `format-check`가 통과한다. `tidy-check` 타깃이 configure 단계에서
비활성화된 환경이면 해당 사실을 기록한다.

- [ ] **3.5 전체 Qt preset 테스트를 실행한다.**

```bash
ctest --preset qt-client-dev
```

예상 결과: Qt 테스트뿐 아니라 preset에 포함된 protocol, server-core,
load-test-support 테스트도 모두 통과한다.

- [ ] **3.6 변경 범위와 잔여 참조를 검사한다.**

```bash
rg -n 'rss_qt_client_lib|apps/qt-client/src/(application|network|ui)|#include "(application|network|ui)/' \
  apps tests CONTRIBUTING.md --glob '!build/**'
git diff --check
git status --short
```

예상 결과: 이전 타깃과 소스/include 경로 참조가 없고 whitespace 오류가 없다.

- [ ] **3.7 구현을 커밋한다.**

```bash
git add apps/qt-client tests/qt-client CONTRIBUTING.md
git commit -m "리팩터: Qt 클라이언트 모듈 경계 분리"
```
