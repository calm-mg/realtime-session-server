#include "rss/qt_client/ui/MainWindow.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <cstdint>

#include "ui_MainWindow.h"

namespace rss::qt_client {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), ui_(std::make_unique<Ui::MainWindow>()) {
  ui_->setupUi(this);
  ui_->mainSplitter->setStretchFactor(0, 0);
  ui_->mainSplitter->setStretchFactor(1, 1);
  applyState(ClientState::Disconnected);
}

MainWindow::~MainWindow() = default;

void MainWindow::bind(ClientController& controller) {
  if (controller_ != nullptr) {
    return;
  }
  controller_ = &controller;

  connect(ui_->connectButton, &QPushButton::clicked, this, [this] {
    controller_->connectToServer(
        ui_->hostEdit->text(),
        static_cast<std::uint16_t>(ui_->portSpinBox->value()));
  });
  connect(ui_->disconnectButton, &QPushButton::clicked, controller_,
          &ClientController::disconnectFromServer);
  connect(ui_->loginButton, &QPushButton::clicked, this,
          [this] { controller_->login(ui_->usernameEdit->text()); });
  connect(ui_->createRoomButton, &QPushButton::clicked, this,
          [this] { controller_->createRoom(ui_->roomEdit->text()); });
  connect(ui_->joinRoomButton, &QPushButton::clicked, this,
          [this] { controller_->joinRoom(ui_->roomEdit->text()); });
  connect(ui_->leaveRoomButton, &QPushButton::clicked, controller_,
          &ClientController::leaveRoom);
  connect(ui_->sendButton, &QPushButton::clicked, this,
          &MainWindow::submitChat);
  connect(ui_->messageEdit, &QLineEdit::returnPressed, this,
          &MainWindow::submitChat);

  connect(controller_, &ClientController::stateChanged, this,
          &MainWindow::applyState);
  connect(controller_, &ClientController::logEntryAdded, this,
          &MainWindow::appendLog);
  connect(
      controller_, &ClientController::validationFailed, this,
      [this](const QString& message) { appendLog(LogKind::Error, message); });

  applyState(controller_->state());
}

void MainWindow::applyState(ClientState state) {
  const bool disconnected = state == ClientState::Disconnected;
  const bool connected = state == ClientState::Connected;
  const bool logged_in = state == ClientState::LoggedIn;
  const bool in_room = state == ClientState::InRoom;

  ui_->hostEdit->setEnabled(disconnected);
  ui_->portSpinBox->setEnabled(disconnected);
  ui_->connectButton->setEnabled(disconnected);
  ui_->disconnectButton->setEnabled(!disconnected);
  ui_->usernameEdit->setEnabled(connected);
  ui_->loginButton->setEnabled(connected);
  ui_->roomEdit->setEnabled(logged_in);
  ui_->createRoomButton->setEnabled(logged_in);
  ui_->joinRoomButton->setEnabled(logged_in);
  ui_->leaveRoomButton->setEnabled(in_room);
  ui_->messageEdit->setEnabled(in_room);
  ui_->sendButton->setEnabled(in_room);

  switch (state) {
    case ClientState::Disconnected:
      ui_->connectionStatusLabel->setText("연결 안 됨");
      break;
    case ClientState::Connecting:
      ui_->connectionStatusLabel->setText("연결 중");
      break;
    case ClientState::Connected:
      ui_->connectionStatusLabel->setText("연결됨");
      break;
    case ClientState::LoggedIn:
      ui_->connectionStatusLabel->setText("로그인됨");
      break;
    case ClientState::InRoom:
      ui_->connectionStatusLabel->setText("방 참가 중");
      break;
  }
}

void MainWindow::appendLog(LogKind kind, const QString& text) {
  QString prefix;
  switch (kind) {
    case LogKind::System:
      prefix = "[시스템]";
      break;
    case LogKind::Chat:
      prefix = "[채팅]";
      break;
    case LogKind::Error:
      prefix = "[오류]";
      break;
  }
  ui_->logView->appendPlainText(QString("%1 %2").arg(prefix, text));
}

void MainWindow::submitChat() {
  if (controller_ == nullptr) {
    return;
  }

  const QString message = ui_->messageEdit->text();
  const bool accepted = controller_->state() == ClientState::InRoom &&
                        !message.trimmed().isEmpty();
  controller_->sendChat(message);
  if (accepted) {
    ui_->messageEdit->clear();
  }
}

}  // namespace rss::qt_client
