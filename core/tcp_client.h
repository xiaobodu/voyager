#ifndef MIRANTS_CORE_TCP_CLIENT_H_
#define MIRANTS_CORE_TCP_CLIENT_H_

#include <memory>
#include <netdb.h>

namespace mirants {

class EventLoop;
class Connector;
class SockAddr;
typedef std::shared_ptr<Connector> ConnectorPtr;

class TcpClient {
 public:
  TcpClient(EventLoop* ev, const SockAddr& addr);
  ~TcpClient();

  void TcpConnect();

 private:
  EventLoop* ev_;
  ConnectorPtr connector_ptr_;

  // No copying allow
  TcpClient(const TcpClient&);
  void operator=(const TcpClient&);
};

}  // namespace mirants

#endif  // MIRANTS_CORE_TCP_CLIENT_H_
