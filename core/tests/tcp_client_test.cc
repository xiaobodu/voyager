#include "core/tcp_client.h"
#include "core/eventloop.h"
#include "core/sockaddr.h"

namespace mirants {
}  // namespace mirants

int main(int argc, char** argv) {
  mirants::EventLoop ev;
  mirants::SockAddr serveraddr("127.0.0.1", 5666);
  mirants::TcpClient client(&ev, serveraddr);
  client.TcpConnect();
  ev.Loop();
  return 0;
}
