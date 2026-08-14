#include <QApplication>

#include "application/ClientController.h"
#include "network/QtSessionClient.h"
#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
  QApplication application(argc, argv);

  rss::qt_client::MainWindow window;
  auto* transport = new rss::qt_client::QtSessionClient(&window);
  auto* controller = new rss::qt_client::ClientController(*transport, &window);
  window.bind(*controller);
  window.show();

  return application.exec();
}
