#pragma once

#include <QMainWindow>
#include <memory>

#include "rss/qt_client/application/ClientController.h"

namespace Ui {
class MainWindow;
}

namespace rss::qt_client {

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  void bind(ClientController& controller);

 private:
  void applyState(ClientState state);
  void appendLog(LogKind kind, const QString& text);
  void submitChat();

  std::unique_ptr<Ui::MainWindow> ui_;
  ClientController* controller_{};
};

}  // namespace rss::qt_client
