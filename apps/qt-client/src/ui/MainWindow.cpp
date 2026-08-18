#include "ui/MainWindow.h"

#include <QFile>
#include <QIcon>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <cstdint>

#include "ui/ChatBubbleDelegate.h"
#include "ui/ChatLogModel.h"
#include "ui_MainWindow.h"

void initializeClientResources() { Q_INIT_RESOURCE(client); }

namespace rss::qt_client {

namespace {

void applyTheme(QWidget& window) {
  QFile stylesheet(":/client/styles/dark.qss");
  if (stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
    window.setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
  }
}

void refreshStyle(QWidget& widget) {
  widget.style()->unpolish(&widget);
  widget.style()->polish(&widget);
  widget.update();
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), ui_(std::make_unique<Ui::MainWindow>()) {
  initializeClientResources();
  ui_->setupUi(this);
  log_model_ = std::make_unique<ChatLogModel>();
  log_delegate_ = std::make_unique<ChatBubbleDelegate>();
  ui_->logView->setModel(log_model_.get());
  ui_->logView->setItemDelegate(log_delegate_.get());
  ui_->emptyStateIconLabel->setPixmap(
      QIcon(":/client/icons/chat.svg").pixmap(48, 48));
  connect(log_model_.get(), &QAbstractItemModel::rowsInserted, this, [this] {
    ui_->logStack->setCurrentWidget(ui_->logPage);
    QTimer::singleShot(0, ui_->logView, &QListView::scrollToBottom);
  });
  applyTheme(*this);
  ui_->connectButton->setIcon(QIcon(":/client/icons/link.svg"));
  ui_->disconnectButton->setIcon(QIcon(":/client/icons/unlink.svg"));
  ui_->loginButton->setIcon(QIcon(":/client/icons/user.svg"));
  ui_->createRoomButton->setIcon(QIcon(":/client/icons/plus.svg"));
  ui_->joinRoomButton->setIcon(QIcon(":/client/icons/enter.svg"));
  ui_->leaveRoomButton->setIcon(QIcon(":/client/icons/exit.svg"));
  ui_->sendButton->setIcon(QIcon(":/client/icons/send.svg"));
  ui_->mainSplitter->setStretchFactor(0, 0);
  ui_->mainSplitter->setStretchFactor(1, 1);
  ui_->mainSplitter->setSizes({300, 680});
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
  connect(controller_, &ClientController::validationFailed, this,
          [this](const QString& message) {
            appendLog({
                .kind = LogKind::Error,
                .text = message,
                .received_at = QDateTime::currentDateTime(),
            });
          });

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
      ui_->connectionStatusLabel->setText("●  오프라인");
      ui_->connectionStatusLabel->setProperty("status", "offline");
      break;
    case ClientState::Connecting:
      ui_->connectionStatusLabel->setText("●  연결 중");
      ui_->connectionStatusLabel->setProperty("status", "connecting");
      break;
    case ClientState::Connected:
      ui_->connectionStatusLabel->setText("●  서버 연결됨");
      ui_->connectionStatusLabel->setProperty("status", "online");
      break;
    case ClientState::LoggedIn:
      ui_->connectionStatusLabel->setText("●  로그인됨");
      ui_->connectionStatusLabel->setProperty("status", "online");
      break;
    case ClientState::InRoom:
      ui_->connectionStatusLabel->setText("●  방 참가 중");
      ui_->connectionStatusLabel->setProperty("status", "online");
      break;
  }
  refreshStyle(*ui_->connectionStatusLabel);
}

void MainWindow::appendLog(const ChatLogEntry& entry) {
  log_model_->append(entry);
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
