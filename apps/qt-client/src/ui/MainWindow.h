#pragma once

#include <QMainWindow>
#include <memory>

#include "application/ClientController.h"

namespace Ui {
class MainWindow;
}

namespace rss::qt_client {

class ChatBubbleDelegate;
class ChatLogModel;

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  void bind(ClientController& controller);

 private:
  void applyState(ClientState state);
  void appendLog(const ChatLogEntry& entry);
  void submitChat();

  std::unique_ptr<Ui::MainWindow> ui_;
  std::unique_ptr<ChatLogModel> log_model_;
  std::unique_ptr<ChatBubbleDelegate> log_delegate_;
  ClientController* controller_{};
};

}  // namespace rss::qt_client
