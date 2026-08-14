# Qt 데스크톱 클라이언트 MVP 구현 계획

**목표:** 기존 `rss_protocol`을 재사용하는 Qt 6 Widgets 클라이언트를
추가하고 Linux, macOS, Windows에서 빌드와 자동 테스트를 통과시킨다.

**아키텍처:** `MainWindow`는 Qt Designer 화면과 사용자 입력만 담당하고,
`ClientController`가 연결·로그인·방 상태를 관리한다. 실제 TCP 통신은
`SessionTransport` 계약을 구현하는 `QtSessionClient`가 맡는다.

**기술 스택:** C++20, Qt 6 Widgets, Qt Network, Qt Test, CMake, Ninja,
GitHub Actions

## 전역 제약

- Qt 클라이언트 빌드는 `RSS_BUILD_QT_CLIENT=ON`일 때만 활성화한다.
- Qt를 설치하지 않은 기존 코어와 Linux 서버 빌드는 그대로 동작해야 한다.
- 지원 플랫폼은 Linux, macOS, Windows다.
- UI는 `MainWindow.ui`로 배치하고 생성되는 `ui_MainWindow.h`는 커밋하지
  않는다.
- 네트워크는 GUI 스레드의 비동기 `QTcpSocket`을 사용한다.
- 자동 재접속, 위치 화면, PING 화면, 설치 패키징은 구현하지 않는다.
- 공개 문서, 커밋 메시지, PR 설명은 한글을 기본으로 한다.
- 각 기능은 실패하는 테스트를 먼저 확인하고 최소 구현 후 통과시킨다.

## 파일 구성

### 새 파일

- `apps/qt-client/CMakeLists.txt`: Qt 클라이언트 라이브러리와 실행 파일
- `apps/qt-client/src/main.cpp`: QObject 조립과 애플리케이션 시작
- `apps/qt-client/src/application/ClientState.h`: 상태와 로그 종류
- `apps/qt-client/src/application/ClientController.h`: UI용 작업과 signal 계약
- `apps/qt-client/src/application/ClientController.cpp`: 상태 전이와 응답 처리
- `apps/qt-client/src/network/SessionTransport.h`: 테스트 가능한 통신 계약
- `apps/qt-client/src/network/QtSessionClient.h`: Qt TCP 구현 선언
- `apps/qt-client/src/network/QtSessionClient.cpp`: 패킷 송수신 구현
- `apps/qt-client/src/ui/MainWindow.h`: UI 연결과 상태 반영 선언
- `apps/qt-client/src/ui/MainWindow.cpp`: Widget 이벤트와 로그 표시
- `apps/qt-client/src/ui/MainWindow.ui`: Qt Designer 화면 배치
- `tests/qt-client/CMakeLists.txt`: Qt Test 실행 파일 등록
- `tests/qt-client/FakeSessionTransport.h`: Controller와 Widget 테스트용 가짜 통신
- `tests/qt-client/ClientControllerTest.cpp`: 상태와 명령 테스트
- `tests/qt-client/QtSessionClientTest.cpp`: 로컬 TCP 통합 테스트
- `tests/qt-client/MainWindowTest.cpp`: Widget smoke test

### 수정 파일

- `CMakeLists.txt`: Qt 옵션, 패키지 검색, 하위 디렉터리 연결
- `CMakePresets.json`: `qt-client-dev` configure/build/test preset
- `tests/CMakeLists.txt`: Qt 테스트 하위 디렉터리 연결
- `cmake/CodeQuality.cmake`: 새 Qt C++ 파일 검사 유지
- `.github/workflows/ci.yml`: 3개 OS Qt 빌드와 테스트 행렬
- `README.md`: Qt 클라이언트 기능과 빠른 시작
- `CONTRIBUTING.md`: Qt 설치, 빌드, 테스트 방법
- `AGENTS.md`: Qt 디렉터리, Designer, signal/slot 작업 규칙

---

## 작업 1: Qt 빌드 경계와 CMake 프리셋

**파일:**

- 수정: `CMakeLists.txt`
- 수정: `CMakePresets.json`
- 생성: `apps/qt-client/CMakeLists.txt`
- 수정: `tests/CMakeLists.txt`
- 생성: `tests/qt-client/CMakeLists.txt`

**제공 결과:**

- `RSS_BUILD_QT_CLIENT` CMake 옵션
- `qt-client-dev` configure/build/test preset
- `rss_qt_client` 실행 파일 타깃

- [ ] **1.1 현재 프리셋에 Qt 구성이 없음을 확인한다.**

실행:

```bash
cmake --preset qt-client-dev
```

예상 결과: `No such preset` 오류로 실패한다.

- [ ] **1.2 이 장비에 Qt 6 개발 환경을 설치한다.**

macOS 실행:

```bash
brew install qt
```

Qt 경로 확인:

```bash
brew --prefix qt
```

예상 결과: Apple Silicon 기본 환경에서는 `/opt/homebrew/opt/qt`가 출력된다.
실제 출력 경로를 이후 `CMAKE_PREFIX_PATH`에 사용한다.

- [ ] **1.3 루트 CMake에 Qt 선택 빌드 경계를 추가한다.**

`CMakeLists.txt`의 option 구역에 추가한다.

```cmake
option(RSS_BUILD_QT_CLIENT "Build the Qt desktop client" OFF)
```

플랫폼 독립 라이브러리 다음에 추가한다.

```cmake
if(RSS_BUILD_QT_CLIENT)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)
    if(RSS_BUILD_TESTS)
        find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    endif()
    qt_standard_project_setup()
    add_subdirectory(apps/qt-client)
endif()
```

`tests/CMakeLists.txt` 끝에는 다음 조건을 추가한다.

```cmake
if(RSS_BUILD_QT_CLIENT)
    add_subdirectory(qt-client)
endif()
```

- [ ] **1.4 Qt 타깃 CMake 파일을 만든다.**

`apps/qt-client/CMakeLists.txt`의 초기 내용:

```cmake
qt_add_executable(rss_qt_client src/main.cpp)
target_link_libraries(rss_qt_client
    PRIVATE
        rss_protocol
        Qt6::Network
        Qt6::Widgets
)
rss_enable_project_warnings(rss_qt_client)

set_target_properties(rss_qt_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)
```

`tests/qt-client/CMakeLists.txt`는 빈 테스트 디렉터리로 인해 configure가
실패하지 않도록 다음 내용으로 시작한다.

```cmake
# Qt 클라이언트 테스트 타깃은 기능별 작업에서 추가한다.
```

- [ ] **1.5 Qt 프리셋을 추가한다.**

`CMakePresets.json`에 다음 configure preset을 추가한다.

```json
{
  "name": "qt-client-dev",
  "generator": "Ninja",
  "binaryDir": "${sourceDir}/build/qt-client-dev",
  "cacheVariables": {
    "CMAKE_BUILD_TYPE": "Debug",
    "RSS_BUILD_NETWORK_TARGETS": "OFF",
    "RSS_BUILD_QT_CLIENT": "ON"
  }
}
```

같은 이름의 build preset과 test preset을 추가한다. test preset은
`outputOnFailure`를 `true`로 설정한다.

- [ ] **1.6 최소 main을 추가하고 Qt configure를 통과시킨다.**

`apps/qt-client/src/main.cpp`:

```cpp
#include <QApplication>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  return 0;
}
```

실행:

```bash
cmake --preset qt-client-dev -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build --preset qt-client-dev --parallel
```

예상 결과: `rss_qt_client`가 빌드된다.

- [ ] **1.7 Qt를 끈 기존 코어 빌드가 유지되는지 확인한다.**

```bash
cmake --preset core-dev
cmake --build --preset core-dev --parallel
ctest --preset core-dev
```

예상 결과: 기존 52개 테스트가 통과하고 Qt 패키지를 검색하지 않는다.

- [ ] **1.8 빌드 경계를 커밋한다.**

```bash
git add CMakeLists.txt CMakePresets.json apps/qt-client tests/CMakeLists.txt tests/qt-client
git commit -m "빌드: 선택형 Qt 클라이언트 타깃 추가"
```

---

## 작업 2: Controller 상태 모델과 통신 계약

**파일:**

- 생성: `apps/qt-client/src/application/ClientState.h`
- 생성: `apps/qt-client/src/application/ClientController.h`
- 생성: `apps/qt-client/src/application/ClientController.cpp`
- 생성: `apps/qt-client/src/network/SessionTransport.h`
- 생성: `tests/qt-client/FakeSessionTransport.h`
- 생성: `tests/qt-client/ClientControllerTest.cpp`
- 수정: `apps/qt-client/CMakeLists.txt`
- 수정: `tests/qt-client/CMakeLists.txt`

**인터페이스:**

- 입력: `rss::protocol::PacketType`, `rss::protocol::Packet`
- 제공: `ClientState`, `LogKind`, `SessionTransport`, `ClientController`

- [ ] **2.1 상태와 통신 계약을 사용하는 실패 테스트를 작성한다.**

`tests/qt-client/ClientControllerTest.cpp`의 핵심 테스트:

```cpp
#include <QtTest>

#include "application/ClientController.h"
#include "FakeSessionTransport.h"

using rss::protocol::Packet;
using rss::protocol::PacketType;
using rss::qt_client::ClientController;
using rss::qt_client::ClientState;

class ClientControllerTest final : public QObject {
  Q_OBJECT

 private slots:
  void waitsForLoginResponseBeforeChangingState() {
    FakeSessionTransport transport;
    ClientController controller(transport);

    controller.connectToServer("127.0.0.1", 7777);
    transport.completeConnection();
    QCOMPARE(controller.state(), ClientState::Connected);

    controller.login("alice");
    QCOMPARE(controller.state(), ClientState::Connected);
    QCOMPARE(transport.lastType(), PacketType::LoginReq);
    QCOMPARE(transport.lastPayload(), std::string("alice"));

    transport.receive(Packet{PacketType::LoginRes,
                             {'O', 'K', '|', 'u', 's', 'e', 'r'}});
    QCOMPARE(controller.state(), ClientState::LoggedIn);
  }

  void resetsStateWhenTransportDisconnects() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    controller.connectToServer("127.0.0.1", 7777);
    transport.completeConnection();

    transport.completeDisconnection();

    QCOMPARE(controller.state(), ClientState::Disconnected);
  }
};

QTEST_GUILESS_MAIN(ClientControllerTest)
#include "ClientControllerTest.moc"
```

- [ ] **2.2 Controller 테스트 타깃을 등록하고 실패를 확인한다.**

`tests/qt-client/CMakeLists.txt`에 추가:

```cmake
qt_add_executable(rss_qt_client_controller_tests
    ClientControllerTest.cpp
)
target_include_directories(rss_qt_client_controller_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/apps/qt-client/src
)
target_link_libraries(rss_qt_client_controller_tests
    PRIVATE
        rss_protocol
        Qt6::Core
        Qt6::Test
)
add_test(NAME rss_qt_client_controller_tests
         COMMAND rss_qt_client_controller_tests)
```

실행:

```bash
cmake --build --preset qt-client-dev --target rss_qt_client_controller_tests
```

예상 결과: `ClientController.h`와 `FakeSessionTransport.h`가 없어 실패한다.

- [ ] **2.3 상태와 로그 종류를 정의한다.**

`ClientState.h`:

```cpp
#pragma once

#include <QMetaType>

namespace rss::qt_client {

enum class ClientState {
  Disconnected,
  Connecting,
  Connected,
  LoggedIn,
  InRoom,
};

enum class LogKind {
  System,
  Chat,
  Error,
};

}  // namespace rss::qt_client

Q_DECLARE_METATYPE(rss::qt_client::ClientState)
Q_DECLARE_METATYPE(rss::qt_client::LogKind)
```

- [ ] **2.4 `SessionTransport` 계약을 정의한다.**

`SessionTransport.h`:

```cpp
#pragma once

#include <QObject>
#include <QString>

#include <cstdint>
#include <string_view>

#include "rss/protocol/Packet.h"

namespace rss::qt_client {

class SessionTransport : public QObject {
  Q_OBJECT

 public:
  using QObject::QObject;
  ~SessionTransport() override = default;

  virtual void connectToHost(const QString& host, std::uint16_t port) = 0;
  virtual void disconnectFromHost() = 0;
  virtual bool sendPacket(protocol::PacketType type,
                          std::string_view payload) = 0;

 signals:
  void connected();
  void disconnected();
  void packetReceived(rss::protocol::Packet packet);
  void transportError(QString message);
};

}  // namespace rss::qt_client

Q_DECLARE_METATYPE(rss::protocol::Packet)
```

- [ ] **2.5 가짜 Transport를 작성한다.**

`FakeSessionTransport.h`는 마지막 요청을 기록하고 테스트가 signal을 발생시킬
수 있게 한다.

```cpp
#pragma once

#include "network/SessionTransport.h"

class FakeSessionTransport final : public rss::qt_client::SessionTransport {
 public:
  void connectToHost(const QString& host, std::uint16_t port) override {
    host_ = host;
    port_ = port;
  }

  void disconnectFromHost() override { emit disconnected(); }

  bool sendPacket(rss::protocol::PacketType type,
                  std::string_view payload) override {
    last_type_ = type;
    last_payload_.assign(payload);
    return send_succeeds_;
  }

  void completeConnection() { emit connected(); }
  void completeDisconnection() { emit disconnected(); }
  void receive(rss::protocol::Packet packet) {
    emit packetReceived(std::move(packet));
  }

  rss::protocol::PacketType lastType() const { return last_type_; }
  const std::string& lastPayload() const { return last_payload_; }

 private:
  QString host_;
  std::uint16_t port_{};
  rss::protocol::PacketType last_type_{};
  std::string last_payload_;
  bool send_succeeds_{true};
};
```

- [ ] **2.6 테스트 가능한 클라이언트 라이브러리 타깃을 만든다.**

`apps/qt-client/CMakeLists.txt`에서 실행 파일보다 먼저 다음 타깃을 추가한다.

```cmake
add_library(rss_qt_client_lib STATIC
    src/application/ClientController.cpp
    src/application/ClientController.h
    src/application/ClientState.h
    src/network/SessionTransport.h
)

target_include_directories(rss_qt_client_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(rss_qt_client_lib
    PUBLIC
        rss_protocol
        Qt6::Core
)

target_compile_features(rss_qt_client_lib PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_qt_client_lib)
```

`rss_qt_client`의 link 항목은 다음으로 교체한다.

```cmake
target_link_libraries(rss_qt_client
    PRIVATE
        rss_qt_client_lib
        Qt6::Network
        Qt6::Widgets
)
```

Controller 테스트 타깃도 직접 나열한 include와 library 대신
`rss_qt_client_lib`를 링크하도록 교체한다.

- [ ] **2.7 Controller 공개 API를 구현한다.**

`ClientController.h`의 공개 계약:

```cpp
class ClientController final : public QObject {
  Q_OBJECT

 public:
  explicit ClientController(SessionTransport& transport,
                            QObject* parent = nullptr);

  [[nodiscard]] ClientState state() const noexcept;

  void connectToServer(const QString& host, std::uint16_t port);
  void disconnectFromServer();
  void login(const QString& username);
  void createRoom(const QString& room_name);
  void joinRoom(const QString& room_id);
  void leaveRoom();
  void sendChat(const QString& message);

 signals:
  void stateChanged(rss::qt_client::ClientState state);
  void logEntryAdded(rss::qt_client::LogKind kind, QString text);
  void validationFailed(QString message);
};
```

구현은 생성자에서 Transport signal을 연결하고, 명령별 허용 상태와 빈 입력을
검증한다. 문자열은 `QString::toUtf8()`로 변환한 뒤 `std::string_view`로
동기 호출한다.

- [ ] **2.8 응답 기반 상태 전이를 구현한다.**

`ClientController.cpp`의 패킷 처리 규칙:

```cpp
const auto payload = QString::fromUtf8(
    reinterpret_cast<const char*>(packet.payload.data()),
    static_cast<qsizetype>(packet.payload.size()));
const bool ok = payload == "OK" || payload.startsWith("OK|");

switch (packet.type) {
  case protocol::PacketType::LoginRes:
    if (ok && state_ == ClientState::Connected) {
      setState(ClientState::LoggedIn);
    }
    break;
  case protocol::PacketType::CreateRoomRes:
  case protocol::PacketType::JoinRoomRes:
    if (ok && state_ == ClientState::LoggedIn) {
      setState(ClientState::InRoom);
    }
    break;
  case protocol::PacketType::LeaveRoomRes:
    if (ok && state_ == ClientState::InRoom) {
      setState(ClientState::LoggedIn);
    }
    break;
  case protocol::PacketType::Error:
    emit logEntryAdded(LogKind::Error, payload);
    return;
  default:
    break;
}
```

`ROOM_BROADCAST`는 `event=CHAT|`으로 시작하면 `LogKind::Chat`, 나머지는
`LogKind::System`으로 보낸다.

- [ ] **2.9 상태, 검증, 방 전이 테스트를 보강한다.**

다음 테스트 함수를 추가한다.

```cpp
void rejectsLoginBeforeConnection();
void entersRoomOnlyAfterSuccessfulCreateResponse();
void returnsToLoggedInAfterSuccessfulLeaveResponse();
void keepsStateWhenErrorPacketArrives();
void rejectsNonNumericRoomId();
void rejectsBlankChatMessage();
void classifiesChatBroadcastAsChatLog();
```

실행:

```bash
cmake --build --preset qt-client-dev --target rss_qt_client_controller_tests
ctest --test-dir build/qt-client-dev -R rss_qt_client_controller_tests --output-on-failure
```

예상 결과: Controller 테스트가 모두 통과한다.

- [ ] **2.10 Controller 계층을 커밋한다.**

```bash
git add apps/qt-client/src/application apps/qt-client/src/network/SessionTransport.h \
  apps/qt-client/CMakeLists.txt tests/qt-client
git commit -m "기능: Qt 클라이언트 상태 제어 계층 추가"
```

---

## 작업 3: 비동기 Qt TCP 통신

**파일:**

- 생성: `apps/qt-client/src/network/QtSessionClient.h`
- 생성: `apps/qt-client/src/network/QtSessionClient.cpp`
- 생성: `tests/qt-client/QtSessionClientTest.cpp`
- 수정: `apps/qt-client/CMakeLists.txt`
- 수정: `tests/qt-client/CMakeLists.txt`

**인터페이스:**

- 구현: `SessionTransport::connectToHost`, `disconnectFromHost`, `sendPacket`
- 발생: `connected`, `disconnected`, `packetReceived`, `transportError`

- [ ] **3.1 로컬 TCP 서버를 사용하는 실패 테스트를 작성한다.**

`QtSessionClientTest.cpp`의 핵심 테스트:

```cpp
class QtSessionClientTest final : public QObject {
  Q_OBJECT

 private slots:
  void receivesPacketSplitAcrossWrites() {
    qRegisterMetaType<rss::protocol::Packet>();
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    rss::qt_client::QtSessionClient client;
    QSignalSpy packet_spy(&client,
                          &rss::qt_client::SessionTransport::packetReceived);
    client.connectToHost("127.0.0.1", server.serverPort());
    QVERIFY(server.waitForNewConnection(1000));
    auto* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);

    const auto bytes = rss::protocol::PacketCodec::encode(
        rss::protocol::PacketType::LoginRes, "OK|user_id=1");
    peer->write(reinterpret_cast<const char*>(bytes.data()), 2);
    peer->flush();
    QTest::qWait(10);
    QCOMPARE(packet_spy.count(), 0);

    peer->write(reinterpret_cast<const char*>(bytes.data() + 2),
                static_cast<qint64>(bytes.size() - 2));
    QTRY_COMPARE_WITH_TIMEOUT(packet_spy.count(), 1, 1000);
  }
};

QTEST_GUILESS_MAIN(QtSessionClientTest)
#include "QtSessionClientTest.moc"
```

- [ ] **3.2 네트워크 테스트 타깃을 등록하고 실패를 확인한다.**

```cmake
qt_add_executable(rss_qt_session_client_tests QtSessionClientTest.cpp)
target_link_libraries(rss_qt_session_client_tests
    PRIVATE rss_qt_client_lib Qt6::Test Qt6::Network)
add_test(NAME rss_qt_session_client_tests COMMAND rss_qt_session_client_tests)
```

예상 결과: `QtSessionClient.h`가 없어 컴파일에 실패한다.

- [ ] **3.3 `QtSessionClient` 선언을 구현한다.**

먼저 `apps/qt-client/CMakeLists.txt`에서 라이브러리에 소스와 Qt Network를
연결한다.

```cmake
target_sources(rss_qt_client_lib PRIVATE
    src/network/QtSessionClient.cpp
    src/network/QtSessionClient.h
)
target_link_libraries(rss_qt_client_lib PUBLIC Qt6::Network)
```

```cpp
class QtSessionClient final : public SessionTransport {
  Q_OBJECT

 public:
  explicit QtSessionClient(QObject* parent = nullptr);

  void connectToHost(const QString& host, std::uint16_t port) override;
  void disconnectFromHost() override;
  bool sendPacket(protocol::PacketType type,
                  std::string_view payload) override;

 private:
  void readAvailableBytes();
  void flushPendingWrites();
  void resetBuffers();

  QTcpSocket socket_;
  protocol::PacketCodec codec_;
  QByteArray pending_bytes_;
  qsizetype pending_offset_{};
};
```

- [ ] **3.4 소켓 signal과 수신 코덱을 연결한다.**

생성자에서 다음 signal을 연결한다.

```cpp
connect(&socket_, &QTcpSocket::connected, this,
        &SessionTransport::connected);
connect(&socket_, &QTcpSocket::disconnected, this, [this] {
  resetBuffers();
  emit disconnected();
});
connect(&socket_, &QTcpSocket::readyRead, this,
        &QtSessionClient::readAvailableBytes);
connect(&socket_, &QTcpSocket::bytesWritten, this,
        [this](qint64) { flushPendingWrites(); });
connect(&socket_, &QTcpSocket::errorOccurred, this,
        [this](QAbstractSocket::SocketError) {
          emit transportError(socket_.errorString());
        });
```

`readAvailableBytes()`는 `readAll()` 결과를 `PacketCodec::feed`에 전달하고
`drainPackets()` 결과를 순서대로 emit한다. `ProtocolError`는
`transportError`로 전달한 뒤 `abort()`한다.

- [ ] **3.5 송신 큐를 구현한다.**

`sendPacket()`은 연결 상태를 확인하고 `PacketCodec::encode` 결과를
`pending_bytes_` 뒤에 추가한다. `flushPendingWrites()`는 아직 기록하지 않은
범위를 `QTcpSocket::write`에 전달하고 반환값만큼 offset을 이동한다.
반환값이 `-1`이면 오류 signal을 발생시키고 `false`를 반환한다. 모든
바이트가 접수되면 QByteArray와 offset을 초기화한다.

- [ ] **3.6 네트워크 경계 테스트를 추가한다.**

다음 테스트를 추가한다.

```cpp
void emitsConnectedAndDisconnected();
void receivesMultiplePacketsFromOneWrite();
void emitsProtocolErrorForInvalidHeader();
void sendsEncodedPacketsInOrder();
void rejectsSendWhileDisconnected();
```

실행:

```bash
cmake --build --preset qt-client-dev --target rss_qt_session_client_tests
ctest --test-dir build/qt-client-dev -R rss_qt_session_client_tests --output-on-failure
```

예상 결과: 모든 네트워크 테스트가 통과한다.

- [ ] **3.7 네트워크 계층을 커밋한다.**

```bash
git add apps/qt-client/src/network apps/qt-client/CMakeLists.txt \
  tests/qt-client/QtSessionClientTest.cpp tests/qt-client/CMakeLists.txt
git commit -m "기능: Qt 비동기 세션 통신 추가"
```

---

## 작업 4: Qt Designer 화면과 MainWindow

**파일:**

- 생성: `apps/qt-client/src/ui/MainWindow.ui`
- 생성: `apps/qt-client/src/ui/MainWindow.h`
- 생성: `apps/qt-client/src/ui/MainWindow.cpp`
- 생성: `tests/qt-client/MainWindowTest.cpp`
- 수정: `apps/qt-client/CMakeLists.txt`
- 수정: `tests/qt-client/CMakeLists.txt`

**Widget objectName:**

- `hostEdit`, `portSpinBox`, `connectButton`, `disconnectButton`
- `usernameEdit`, `loginButton`
- `roomEdit`, `createRoomButton`, `joinRoomButton`, `leaveRoomButton`
- `connectionStatusLabel`, `logView`, `messageEdit`, `sendButton`

- [ ] **4.1 Widget 상태를 검증하는 실패 테스트를 작성한다.**

```cpp
#include <cstdint>
#include <string_view>
#include <vector>

rss::protocol::Packet packet(rss::protocol::PacketType type,
                             std::string_view payload) {
  return rss::protocol::Packet{
      type, std::vector<std::uint8_t>(payload.begin(), payload.end())};
}

class MainWindowTest final : public QObject {
  Q_OBJECT

 private slots:
  void enablesOnlyConnectionControlsWhenDisconnected() {
    FakeSessionTransport transport;
    rss::qt_client::ClientController controller(transport);
    rss::qt_client::MainWindow window;
    window.bind(controller);

    QVERIFY(window.findChild<QLineEdit*>("hostEdit")->isEnabled());
    QVERIFY(window.findChild<QPushButton*>("connectButton")->isEnabled());
    QVERIFY(!window.findChild<QPushButton*>("loginButton")->isEnabled());
    QVERIFY(!window.findChild<QLineEdit*>("messageEdit")->isEnabled());
  }

  void sendsChatWhenReturnIsPressed() {
    FakeSessionTransport transport;
    rss::qt_client::ClientController controller(transport);
    rss::qt_client::MainWindow window;
    window.bind(controller);

    controller.connectToServer("127.0.0.1", 7777);
    transport.completeConnection();
    controller.login("alice");
    transport.receive(packet(rss::protocol::PacketType::LoginRes,
                             "OK|user_id=1"));
    controller.createRoom("study");
    transport.receive(packet(rss::protocol::PacketType::CreateRoomRes,
                             "OK|event=CREATE_ROOM|room_id=1"));

    auto* edit = window.findChild<QLineEdit*>("messageEdit");
    edit->setText("hello");
    QTest::keyClick(edit, Qt::Key_Return);

    QCOMPARE(transport.lastType(), rss::protocol::PacketType::ChatReq);
    QCOMPARE(transport.lastPayload(), std::string("hello"));
    QVERIFY(edit->text().isEmpty());
  }
};

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"
```

- [ ] **4.2 Widget 테스트 타깃을 등록하고 실패를 확인한다.**

```cmake
qt_add_executable(rss_qt_main_window_tests MainWindowTest.cpp)
target_link_libraries(rss_qt_main_window_tests
    PRIVATE rss_qt_client_lib Qt6::Test Qt6::Widgets)
add_test(NAME rss_qt_main_window_tests COMMAND rss_qt_main_window_tests)
set_tests_properties(rss_qt_main_window_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

예상 결과: `MainWindow.h`가 없어 컴파일에 실패한다.

- [ ] **4.3 Qt Designer 화면을 만든다.**

`apps/qt-client/CMakeLists.txt`에서 Designer 파일과 MainWindow 소스를
라이브러리에 추가하고 Qt Widgets를 연결한다.

```cmake
target_sources(rss_qt_client_lib PRIVATE
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.ui
)
target_link_libraries(rss_qt_client_lib PUBLIC Qt6::Widgets)
```

`MainWindow.ui`는 `QMainWindow` 아래에 가로 `QSplitter`를 둔다. 왼쪽 Widget은
`QFormLayout`과 세 개의 `QGroupBox`로 서버, 사용자, 방 입력을 배치한다.
오른쪽 Widget은 세로 `QVBoxLayout`으로 `connectionStatusLabel`, 읽기 전용
`QPlainTextEdit`인 `logView`, 메시지 입력 행을 배치한다.

정확한 기본값:

```text
hostEdit.text = 127.0.0.1
portSpinBox.minimum = 1
portSpinBox.maximum = 65535
portSpinBox.value = 7777
connectionStatusLabel.text = 연결 안 됨
logView.readOnly = true
messageEdit.placeholderText = 메시지를 입력하세요
sendButton.text = 보내기
```

버튼 표시 문자열은 `연결`, `연결 해제`, `로그인`, `방 생성`, `방 참가`,
`방 나가기`, `보내기`로 고정한다.

- [ ] **4.4 `MainWindow` 계약과 바인딩을 구현한다.**

`MainWindow.h`:

```cpp
class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  void bind(ClientController& controller);

 private:
  void applyState(ClientState state);
  void appendLog(LogKind kind, const QString& text);

  std::unique_ptr<Ui::MainWindow> ui_;
  ClientController* controller_{};
};
```

생성자는 `ui_->setupUi(this)`를 호출한다. `bind()`는 중복 호출을 허용하지
않고 버튼과 입력 signal을 Controller 메서드에 연결한다. Controller의
`stateChanged`, `logEntryAdded`, `validationFailed`를 UI 갱신과 로그 표시에
연결한다.

- [ ] **4.5 상태별 Widget 활성화를 구현한다.**

`applyState()` 규칙:

```text
Disconnected: 주소, 포트, 연결 활성
Connecting: 연결 해제만 활성
Connected: 로그인, 연결 해제 활성
LoggedIn: 방 생성, 방 참가, 연결 해제 활성
InRoom: 방 나가기, 메시지, 보내기, 연결 해제 활성
```

`connectionStatusLabel`은 각 상태를 `연결 안 됨`, `연결 중`, `연결됨`,
`로그인됨`, `방 참가 중`으로 표시한다.

- [ ] **4.6 UI 로그와 입력 동작을 구현한다.**

- `System`은 `[시스템]`, `Chat`은 `[채팅]`, `Error`는 `[오류]` 접두사를
  사용한다.
- 보내기 성공 요청 후에만 `messageEdit`을 비운다.
- `messageEdit::returnPressed`와 `sendButton::clicked`는 같은 함수를 호출한다.
- MainWindow는 로컬 채팅을 먼저 출력하지 않고 broadcast만 표시한다.

- [ ] **4.7 Widget 테스트를 보강하고 통과시킨다.**

추가 테스트:

```cpp
void enablesLoginAfterConnection();
void enablesRoomActionsAfterLogin();
void enablesChatOnlyInRoom();
void invokesCreateJoinAndLeaveActions();
void appendsSystemChatAndErrorLogs();
void keepsMessageWhenControllerRejectsIt();
```

실행:

```bash
cmake --build --preset qt-client-dev --target rss_qt_main_window_tests
QT_QPA_PLATFORM=offscreen ctest --test-dir build/qt-client-dev \
  -R rss_qt_main_window_tests --output-on-failure
```

예상 결과: Widget 테스트가 모두 통과한다.

- [ ] **4.8 Designer 화면과 MainWindow를 커밋한다.**

```bash
git add apps/qt-client/src/ui apps/qt-client/CMakeLists.txt \
  tests/qt-client/MainWindowTest.cpp tests/qt-client/CMakeLists.txt
git commit -m "기능: Qt Widgets 메신저 화면 추가"
```

---

## 작업 5: 애플리케이션 조립과 전체 로컬 검증

**파일:**

- 수정: `apps/qt-client/src/main.cpp`
- 수정: `apps/qt-client/CMakeLists.txt`
- 수정: `cmake/CodeQuality.cmake`

- [ ] **5.1 실제 객체를 조립한다.**

`main.cpp`:

```cpp
#include <QApplication>

#include "application/ClientController.h"
#include "network/QtSessionClient.h"
#include "ui/MainWindow.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  rss::qt_client::MainWindow window;
  auto* transport = new rss::qt_client::QtSessionClient(&window);
  auto* controller =
      new rss::qt_client::ClientController(*transport, &window);
  window.bind(*controller);
  window.show();

  return app.exec();
}
```

두 heap 객체는 `window`를 QObject 부모로 사용하므로 MainWindow 종료 시
자동 삭제된다.

- [ ] **5.2 앱과 모든 Qt 테스트를 빌드한다.**

```bash
cmake --preset qt-client-dev -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build --preset qt-client-dev --parallel
QT_QPA_PLATFORM=offscreen ctest --preset qt-client-dev
```

예상 결과: Controller, 네트워크, Widget 테스트와 기존 플랫폼 독립 테스트가
모두 통과한다.

- [ ] **5.3 실행 파일의 기본 화면을 확인한다.**

```bash
./build/qt-client-dev/rss_qt_client.app/Contents/MacOS/rss_qt_client
```

확인 항목:

- 왼쪽에 서버, 사용자, 방 작업이 배치된다.
- 오른쪽에 연결 상태, 로그, 메시지 입력이 배치된다.
- 최초 상태에서 로그인, 방, 채팅 작업은 비활성화된다.
- 창 크기를 줄여도 입력과 버튼이 겹치지 않는다.

- [ ] **5.4 포맷 검사와 기존 회귀 테스트를 실행한다.**

```bash
cmake --build --preset qt-client-dev --target format-check
cmake --build --preset core-dev --parallel
ctest --preset core-dev
```

예상 결과: 포맷 검사와 기존 52개 테스트가 통과한다.

- [ ] **5.5 애플리케이션 조립을 커밋한다.**

```bash
git add apps/qt-client/src/main.cpp apps/qt-client/CMakeLists.txt \
  cmake/CodeQuality.cmake
git commit -m "기능: Qt 클라이언트 애플리케이션 조립"
```

---

## 작업 6: 사용 문서와 3개 OS CI

**파일:**

- 수정: `.github/workflows/ci.yml`
- 수정: `README.md`
- 수정: `CONTRIBUTING.md`
- 수정: `AGENTS.md`

- [ ] **6.1 README에 Qt 클라이언트 사용법을 추가한다.**

추가할 명령:

macOS 예시:

```bash
cmake --preset qt-client-dev -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build --preset qt-client-dev --parallel
./build/qt-client-dev/rss_qt_client.app/Contents/MacOS/rss_qt_client
```

Windows Qt 온라인 설치 프로그램의 MSVC 2022 64-bit 기본 예시:

```powershell
cmake --preset qt-client-dev -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build --preset qt-client-dev --parallel
```

문서에는 기본 서버 주소 `127.0.0.1`, 기본 포트 `7777`, 지원 기능과 MVP 제외
기능을 명확히 적는다. macOS 예시는 `brew --prefix qt` 결과를 사용한다.

- [ ] **6.2 CONTRIBUTING에 플랫폼별 Qt 준비와 테스트를 추가한다.**

필수 항목:

```text
Linux: Qt 6 Widgets, Network, Test 개발 패키지
macOS: brew install qt
Windows: Qt 6 데스크톱 MSVC 패키지와 Ninja
공통: cmake --preset qt-client-dev, ctest --preset qt-client-dev
Linux headless: QT_QPA_PLATFORM=offscreen
```

- [ ] **6.3 AGENTS에 Qt 작업 규칙을 추가한다.**

추가할 규칙:

- `apps/qt-client`는 `rss_protocol`에만 의존하고 Linux 전용 라이브러리에
  의존하지 않는다.
- 화면 배치는 `.ui`, 화면 동작은 `MainWindow.cpp`, 통신은 network 계층에
  둔다.
- `ui_MainWindow.h`를 커밋하지 않는다.
- Widget `objectName` 변경 시 Widget 테스트를 함께 수정한다.
- 커밋 메시지와 PR 설명은 한글을 기본으로 한다.

- [ ] **6.4 Qt CI 행렬을 추가한다.**

`.github/workflows/ci.yml`에 다음 job을 추가한다.

```yaml
  qt-client:
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-24.04, macos-latest, windows-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - name: 저장소 체크아웃
        uses: actions/checkout@v7

      - name: Qt 설치
        uses: jurplel/install-qt-action@v4
        with:
          version: '6.8.*'
          cache: true

      - name: Ninja 설치
        run: python -m pip install ninja

      - name: Qt 클라이언트 구성
        run: >-
          cmake -S . -B build/qt-client -G Ninja
          -DCMAKE_BUILD_TYPE=Release
          -DRSS_BUILD_NETWORK_TARGETS=OFF
          -DRSS_BUILD_QT_CLIENT=ON

      - name: Qt 클라이언트 빌드
        run: cmake --build build/qt-client --parallel

      - name: Qt 클라이언트 테스트
        env:
          QT_QPA_PLATFORM: offscreen
        run: ctest --test-dir build/qt-client --output-on-failure --timeout 30
```

세 운영체제 모두 `offscreen` 플랫폼에서 Widget smoke test를 실행하며 이
행렬 전체를 필수 검증으로 사용한다.

- [ ] **6.5 로컬 문서와 YAML 정합성을 검사한다.**

```bash
git diff --check
ruby -e 'require "yaml"; YAML.load_file(".github/workflows/ci.yml"); puts "ci yaml ok"'
cmake --list-presets
```

예상 결과: 공백 오류가 없고 YAML 파싱과 프리셋 목록 출력이 성공한다.

- [ ] **6.6 문서와 CI를 커밋한다.**

```bash
git add .github/workflows/ci.yml README.md CONTRIBUTING.md AGENTS.md
git commit -m "문서: Qt 클라이언트 빌드와 검증 방법 추가"
```

---

## 작업 7: 최종 회귀 검증과 PR 준비

- [ ] **7.1 Qt 전체 빌드와 테스트를 새 디렉터리에서 실행한다.**

```bash
cmake -S . -B build/verify-qt -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DRSS_BUILD_NETWORK_TARGETS=OFF \
  -DRSS_BUILD_QT_CLIENT=ON
cmake --build build/verify-qt --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build/verify-qt \
  --output-on-failure --timeout 30
cmake --build build/verify-qt --target format-check
```

- [ ] **7.2 Qt를 끈 기존 구성도 새 디렉터리에서 검증한다.**

```bash
cmake -S . -B build/verify-core -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRSS_BUILD_NETWORK_TARGETS=OFF \
  -DRSS_BUILD_QT_CLIENT=OFF
cmake --build build/verify-core --parallel
ctest --test-dir build/verify-core --output-on-failure --timeout 30
```

- [ ] **7.3 공개 저장소 흔적과 변경 범위를 확인한다.**

```bash
git diff main...HEAD --check
git status --short --branch
git log --oneline main..HEAD
```

예상 결과: 작업 트리가 깨끗하고 변경 범위가 Qt 클라이언트와 관련 빌드,
테스트, 문서에 한정된다.

- [ ] **7.4 브랜치를 푸시하고 한글 초안 PR을 만든다.**

PR 본문에 다음을 포함한다.

- Qt Designer 기반 분할형 화면
- Controller와 Transport 분리
- 수동 재연결 정책
- Linux, macOS, Windows CI 결과
- 로컬 Qt 및 기존 코어 테스트 결과

CI가 모두 통과하기 전에는 `main`에 병합하지 않는다.
