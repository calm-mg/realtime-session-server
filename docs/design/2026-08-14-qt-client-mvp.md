# Qt 데스크톱 클라이언트 MVP 설계

## 목적

기존 실시간 세션 서버에 접속하는 Qt 6 기반 데스크톱 클라이언트를
모노레포에 추가한다. 첫 버전은 프로토콜의 핵심 흐름을 GUI에서 학습하고
검증할 수 있는 작은 업무용 메신저 형태를 목표로 한다.

지원 대상은 Linux, macOS, Windows다. 세 운영체제에서 같은 CMake 타깃을
빌드하고 자동 테스트를 실행할 수 있어야 한다.

## 범위

### 포함

- 서버 주소와 포트를 입력해 연결하고 연결을 해제한다.
- 사용자 이름으로 로그인한다.
- 방을 생성하거나 방 번호로 참가하고 현재 방에서 나간다.
- 현재 방에서 채팅 메시지를 보내고 수신 메시지를 표시한다.
- 연결 상태, 서버 응답, 프로토콜 오류를 로그로 표시한다.
- 연결이 끊어지면 상태를 초기화하고 사용자가 수동으로 다시 연결한다.
- 기존 `rss_protocol` 라이브러리를 패킷 인코딩과 디코딩에 재사용한다.
- Linux, macOS, Windows CI에서 Qt 클라이언트와 관련 테스트를 빌드한다.

### 제외

- 위치 업데이트 입력과 시각화
- PING 지연 시간 측정 화면
- 자동 재접속과 로그인 또는 방 상태 자동 복구
- 방 목록 조회 기능과 사용자 목록
- 메시지 영구 저장
- 모바일 UI와 QML
- DMG, MSI, AppImage 같은 설치 패키지 생성

제외 항목은 MVP가 안정된 뒤 별도 변경으로 추가한다.

## 기술 선택

- UI 프레임워크: Qt 6 Widgets
- 네트워크: Qt Network의 `QTcpSocket`
- 테스트: Qt Test와 기존 GoogleTest
- 빌드: CMake, CMake Presets, `AUTOMOC`, `AUTOUIC`
- 패킷 처리: 기존 `rss_protocol`

Qt Widgets는 전통적인 데스크톱 업무 프로그램의 구성과 유지보수를
학습하기에 적합하다. QML과 Qt Quick은 모바일 또는 동적인 화면이 필요한
후속 단계에서 검토한다.

## 화면 구성

메인 창은 작업 중심 분할형 레이아웃을 사용한다.

```text
+------------------------------------------------------------------+
| Realtime Session Client                          연결 상태        |
+--------------------------+---------------------------------------+
| 서버 연결                | 채팅 및 상태 로그                     |
| - 주소                   |                                       |
| - 포트                   | [system] 서버에 연결되었습니다.       |
| - 연결/해제              | [alice] 안녕하세요.                   |
|                          | [bob] Qt 클라이언트 테스트 중입니다.  |
| 사용자                   |                                       |
| - 이름                   |                                       |
| - 로그인                 |                                       |
|                          +---------------------------------------+
| 방                       | 메시지 입력                    보내기 |
| - 방 이름 또는 번호      |                                       |
| - 생성/참가/나가기       |                                       |
+--------------------------+---------------------------------------+
```

왼쪽 패널은 현재 상태에서 실행 가능한 세션 작업을 모은다. 오른쪽 패널은
채팅과 시스템 로그를 시간 순서대로 표시한다. 별도의 디버그 창을 만들지
않고 한 화면에서 서버 응답과 오류를 확인할 수 있게 한다.

창 크기가 작아져도 입력과 버튼이 겹치지 않게 최소 크기를 지정한다. 첫
버전은 데스크톱 창을 대상으로 하며 모바일 반응형 레이아웃은 다루지 않는다.

## 구조

```text
apps/qt-client/
├── CMakeLists.txt
└── src/
    ├── main.cpp
    ├── application/
    │   ├── ClientController.cpp
    │   └── ClientController.h
    ├── network/
    │   ├── SessionTransport.h
    │   ├── QtSessionClient.cpp
    │   └── QtSessionClient.h
    └── ui/
        ├── MainWindow.cpp
        ├── MainWindow.h
        └── MainWindow.ui

tests/qt-client/
├── CMakeLists.txt
├── ClientControllerTest.cpp
├── QtSessionClientTest.cpp
└── MainWindowTest.cpp
```

### `MainWindow`

- `MainWindow.ui`를 로드하고 Controller 상태를 Widget에 반영한다.
- 사용자 입력을 검증 가능한 형태로 Controller에 전달한다.
- Controller가 제공하는 상태에 맞춰 입력과 버튼을 활성화한다.
- 채팅, 서버 응답, 오류를 로그에 추가한다.
- 소켓이나 패킷 코덱을 직접 사용하지 않는다.

화면 배치는 Qt Designer 형식의 `MainWindow.ui`에 작성하고, signal 연결과
화면 갱신 로직은 `MainWindow.cpp`에 둔다. 생성되는 `ui_MainWindow.h`는
빌드 산출물이므로 저장소에 커밋하지 않는다. Widget의 `objectName`은 역할이
드러나는 영어 이름을 사용해 자동 연결과 UI 테스트에서 안정적으로 찾을 수
있게 한다.

### `ClientController`

- 연결, 로그인, 방 참가 상태를 소유한다.
- 현재 상태에서 허용되지 않는 명령을 차단한다.
- UI가 사용할 상태와 안내 메시지를 signal로 전달한다.
- 서버 응답 종류와 payload를 해석해 성공한 작업만 상태에 반영한다.
- 구체적인 `QTcpSocket` 구현이 아니라 `SessionTransport` 계약에 의존한다.

### `SessionTransport`

- Controller가 사용하는 최소 통신 계약이다.
- 연결, 연결 해제, 패킷 전송 연산을 제공한다.
- 연결 성공, 연결 종료, 패킷 수신, 통신 오류를 signal로 알린다.
- 테스트에서는 가짜 구현을 주입해 Controller 상태 전이를 소켓 없이
  검증한다.

### `QtSessionClient`

- `SessionTransport`의 실제 Qt Network 구현이다.
- `QTcpSocket`의 비동기 signal을 사용하고 별도 수신 스레드를 만들지 않는다.
- 수신 바이트를 `PacketCodec`에 공급하고 완성된 패킷만 Controller에
  전달한다.
- 송신 데이터가 한 번에 모두 접수되지 않으면 남은 바이트를 보관했다가
  이어서 기록한다.
- 연결 종료 시 코덱과 송신 대기 상태를 초기화한다.

### `rss_protocol`

- 패킷 종류와 크기 제한을 정의한다.
- 문자열 payload와 위치 payload를 인코딩한다.
- TCP 수신 조각을 완성된 패킷으로 디코딩한다.
- Qt 타입에 의존하지 않는다.

의존성 방향은 다음과 같다.

```text
MainWindow → ClientController → SessionTransport
                                  ↑
                           QtSessionClient → Qt Network
                                  ↓
                             rss_protocol
```

UI와 Controller는 Qt Network를 직접 사용하지 않는다. 네트워크 계층은
Widget을 알지 못한다.

## 상태 모델

Controller는 다음 상태 중 하나를 가진다.

```text
Disconnected → Connecting → Connected → LoggedIn → InRoom
```

어느 연결 상태에서든 연결 종료나 복구할 수 없는 오류가 발생하면
`Disconnected`로 돌아간다.

| 상태 | 가능한 작업 |
| --- | --- |
| `Disconnected` | 주소와 포트 수정, 연결 |
| `Connecting` | 연결 취소 또는 연결 결과 대기 |
| `Connected` | 로그인, 연결 해제 |
| `LoggedIn` | 방 생성, 방 참가, 연결 해제 |
| `InRoom` | 채팅, 방 나가기, 연결 해제 |

서버의 성공 응답을 받기 전에는 다음 상태로 이동하지 않는다.
응답 payload가 `OK` 또는 `OK|`로 시작할 때만 성공으로 판정한다.

- `LOGIN_RES`의 성공 응답: `Connected`에서 `LoggedIn`으로 이동
- `CREATE_ROOM_RES` 또는 `JOIN_ROOM_RES`의 성공 응답: `LoggedIn`에서
  `InRoom`으로 이동
- `LEAVE_ROOM_RES`의 성공 응답: `InRoom`에서 `LoggedIn`으로 이동

`ERROR` 패킷과 실패 응답은 현재 상태를 유지하고 로그만 추가한다. 소켓이
끊기거나 복구할 수 없는 프로토콜 오류가 발생하면 `Disconnected`로
초기화한다. 채팅 로그는 원인 확인을 위해 지우지 않는다.

## 데이터 흐름

### 연결과 로그인

1. 사용자가 주소와 포트를 입력하고 연결을 누른다.
2. MainWindow가 Controller에 연결을 요청한다.
3. Controller가 상태를 `Connecting`으로 바꾸고 Transport를 호출한다.
4. `QTcpSocket::connected`가 발생하면 `Connected`로 바뀐다.
5. 사용자가 로그인하면 Controller가 `LOGIN_REQ` 전송을 요청한다.
6. 성공한 `LOGIN_RES`가 수신된 뒤 `LoggedIn`으로 바뀐다.

### 패킷 수신

1. `QTcpSocket::readyRead`에서 현재 수신 가능한 바이트를 모두 읽는다.
2. 바이트를 `PacketCodec::feed`에 전달한다.
3. 완성된 패킷을 순서대로 Controller에 전달한다.
4. Controller가 응답 종류와 payload를 해석해 상태 또는 로그를 갱신한다.
5. MainWindow가 signal을 받아 화면을 다시 그린다.

`ROOM_BROADCAST` 중 `event=CHAT`은 채팅 항목으로 표시하고, 방 입장과 퇴장
같은 다른 이벤트는 시스템 항목으로 표시한다. MVP 범위 밖인 위치 이벤트는
수신 사실만 시스템 로그에 남긴다.

TCP의 읽기 경계와 패킷 경계가 다르다는 전제는 기존 `PacketCodec`으로
처리한다.

### 채팅 송신

1. `InRoom` 상태에서만 메시지 입력과 보내기를 활성화한다.
2. 앞뒤 공백을 제거한 결과가 비어 있으면 전송하지 않는다.
3. UTF-8 문자열을 `CHAT_REQ`로 인코딩한다.
4. Transport가 소켓에 기록한다.
5. 서버의 `ROOM_BROADCAST`가 돌아오면 채팅 로그에 표시한다.

클라이언트가 보낸 메시지를 즉시 로컬 로그에 중복 추가하지 않는다. 서버가
실제로 broadcast한 결과를 화면의 기준으로 사용한다.

## 입력 검증과 오류 처리

- 포트는 `1..65535` 범위의 정수만 허용한다.
- 사용자 이름, 방 이름, 방 번호, 채팅은 상태와 명령에 맞게 비어 있지
  않은지 확인한다.
- 방 참가 입력은 양의 10진수 방 번호만 허용한다.
- 패킷 최대 크기 제한은 `rss_protocol`의 검증을 그대로 사용한다.
- 사용자 입력 오류는 소켓에 전송하지 않고 화면 가까이에 안내한다.
- 연결 실패, 원격 종료, 소켓 오류는 상태 로그에 원인을 표시한다.
- 프로토콜 오류는 연결을 종료하고 `Disconnected`로 초기화한다.
- 자동 재접속, 자동 로그인, 자동 방 재참가는 수행하지 않는다.

오류 메시지는 사용자가 다음 행동을 알 수 있게 작성한다. 예를 들어
`Connection refused`만 표시하지 않고 `서버에 연결하지 못했습니다: 연결이
거부되었습니다.`처럼 작업 맥락을 포함한다.

## 객체 수명과 스레드

- `main.cpp`가 MainWindow, 실제 `QtSessionClient`, Controller를 조립한다.
- `QtSessionClient`와 Controller는 MainWindow를 QObject 부모로 사용하고,
  MainWindow는 Controller를 참조해 signal과 UI 동작을 연결한다.
- QObject 부모-자식 소유권을 사용하며 동일 객체를 여러 계층이 삭제하지
  않는다.
- `QTcpSocket`, Controller, MainWindow는 GUI 스레드에서 동작한다.
- 첫 버전에서는 네트워크 전용 스레드를 만들지 않는다. Qt의 비동기 I/O로
  GUI 멈춤을 피한다.

## 빌드

기존 서버와 코어 빌드가 Qt 설치 여부에 영향을 받지 않게 다음 옵션을
추가한다.

```text
RSS_BUILD_QT_CLIENT=OFF  기존 빌드만 구성
RSS_BUILD_QT_CLIENT=ON   Qt 클라이언트와 관련 테스트 구성
```

`RSS_BUILD_QT_CLIENT`의 기본값은 `OFF`다. 옵션이 `ON`이면 Qt 6의 Widgets,
Network, Test 구성 요소를 필수로 찾고 누락 시 구성 단계에서 명확히
실패한다. 조용히 타깃을 생략하지 않는다. Qt 클라이언트 타깃은 CMake의
`AUTOMOC`와 `AUTOUIC`를 사용해 QObject 메타 코드와 `.ui` 헤더를 생성한다.

개발용 `qt-client-dev` configure, build, test preset을 추가한다. 기존
`core-dev`, `linux-dev`, `benchmark` preset의 동작은 유지한다.

## 테스트

### Controller 테스트

가짜 `SessionTransport`를 주입해 다음을 검증한다.

- 연결 성공과 실패에 따른 상태 전이
- 성공 응답 전에는 로그인 또는 방 상태를 앞당겨 변경하지 않음
- 현재 상태에서 허용되지 않은 명령 차단
- 로그인, 방 생성, 방 참가, 방 나가기 요청의 패킷 종류와 payload
- `ERROR` 응답 시 상태 유지
- 연결 종료 시 `Disconnected` 초기화

### 네트워크 테스트

테스트 프로세스 안에서 `QTcpServer`를 열어 다음을 검증한다.

- 서버 연결과 연결 해제 signal
- 한 패킷을 여러 조각으로 수신
- 여러 패킷을 한 번에 수신
- 원격 연결 종료
- 잘못된 패킷 수신 시 프로토콜 오류와 연결 초기화
- 여러 패킷을 연속 전송할 때 순서 유지

### Widget smoke test

- 상태별 주소, 포트, 로그인, 방, 채팅 입력 활성 상태
- 연결 또는 작업 버튼 클릭 시 Controller 호출
- Enter 키를 이용한 채팅 전송
- 상태와 오류 로그 표시
- `.ui`에 정의한 주요 Widget의 `objectName`이 유지되고 MainWindow에서
  정상적으로 연결되는지 확인

Linux CI에서는 화면 서버 없이 Widget 테스트를 실행할 수 있는 Qt 플랫폼
설정을 사용한다.

## CI

Linux, macOS, Windows 행렬에서 다음을 수행한다.

1. Qt 6과 빌드 도구를 준비한다.
2. Qt 클라이언트 옵션을 켜고 CMake를 구성한다.
3. 클라이언트와 테스트를 빌드한다.
4. Qt 관련 테스트와 기존 플랫폼 독립 테스트를 실행한다.

Linux 전용 서버 CI는 기존대로 유지한다. Qt 행렬의 실패가 플랫폼별 코드
차이를 조기에 드러내도록 세 운영체제를 모두 필수 검증 대상으로 둔다.

## 완료 기준

- Linux, macOS, Windows에서 Qt 클라이언트 타깃이 빌드된다.
- 연결, 로그인, 방 생성, 방 참가, 방 나가기, 채팅 송수신을 GUI에서 수행할
  수 있다.
- 연결 종료와 오류 후 UI가 `Disconnected` 상태로 돌아간다.
- 자동 재접속 없이 사용자가 다시 연결할 수 있다.
- Controller, 네트워크, Widget 테스트가 통과한다.
- 기존 코어, 서버, 테스트와 벤치마크 빌드가 영향을 받지 않는다.
- README와 CONTRIBUTING에 Qt 설치, 빌드, 테스트 방법이 한글로 정리된다.
