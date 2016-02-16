#include "core/connector.h"

#include <assert.h>
#include <string.h>
#include <errno.h>

#include "core/dispatch.h"
#include "core/eventloop.h"
#include "core/socket_util.h"
#include "util/stringprintf.h"
#include "util/logging.h"

namespace mirants {

const double Connector::kMaxRetryTime = 30.0;
const double Connector::kInitRetryTime = 1.0;

Connector::Connector(EventLoop* ev, const SockAddr& addr) 
    : ev_(CHECK_NOTNULL(ev)),
      addr_(addr),
      state_(kDisConnected),
      retry_time_(kInitRetryTime),
      connect_(false) {
}

void Connector::Start() {
  connect_ = true;
  ev_->RunInLoop(std::bind(&Connector::StartInLoop, this));
}

void Connector::StartInLoop() {
  ev_->AssertThreadSafe();
  assert(state_ == kDisConnected);
  if (connect_) {
    Connect();
  }
}

void Connector::ReStart() {
  ev_->AssertThreadSafe();
  state_ = kDisConnected;
  retry_time_ = kInitRetryTime;
  connect_ = true;
  StartInLoop();
}

void Connector::Stop() {
  connect_ = false;
  ev_->QueueInLoop(std::bind(&Connector::StartInLoop, this));
}

void Connector::StopInLoop() {
  ev_->AssertThreadSafe();
  if (state_ == kConnecting) {
    state_ = kDisConnected;
    int socketfd = DeleteOldDispatch();
    Retry(socketfd);
  }
}

void Connector::Connect() {
  const struct  addrinfo* p;
  for (p = addr_.AddrInfo(); p != NULL; p = p->ai_next) {
    int socketfd = sockets::CreateSocketAndSetNonBlock(p->ai_family);
    int ret = sockets::Connect(socketfd, p->ai_addr, p->ai_addrlen);
    int err = (ret == 0) ? 0 : errno;
    switch (err) {
      case 0:
      case EINPROGRESS:
      case EINTR:
      case EISCONN:
        Connecting(socketfd);
        break;

      case EAGAIN:
      case EADDRINUSE:
      case EADDRNOTAVAIL:
      case ECONNREFUSED:
      case ENETUNREACH:
        Retry(socketfd);
        break;

      case EACCES:
      case EPERM:
      case EAFNOSUPPORT:
      case EALREADY:
      case EBADF:
      case EFAULT:
      case ENOTSOCK:
        MIRANTS_LOG(ERROR) << "connect error: " << strerror(err);
        sockets::CloseFd(socketfd);
        break;

      default:
        MIRANTS_LOG(ERROR) << "unexpected connect error: " << strerror(err);
        sockets::CloseFd(socketfd);
        break;
    }
    break;
  }
}

void Connector::Connecting(int socketfd) {
  state_ = kConnecting; 
  assert(!dispatch_.get());
  dispatch_.reset(new Dispatch(ev_, socketfd));
  dispatch_->SetWriteCallback(std::bind(&Connector::OnConnect, this));
  dispatch_->SetErrorCallback(std::bind(&Connector::HandleError, this));
  dispatch_->EnableWrite();
}

void Connector::Retry(int socketfd) {
  sockets::CloseFd(socketfd);
  state_ = kDisConnected;
  if (connect_) {
    MIRANTS_LOG(INFO) << "Connector::Retry - Retry connecting to "
                      << addr_.IP() << " in " << retry_time_ << " seconds.";
    ev_->RunAfter(retry_time_, 
                  std::bind(&Connector::StartInLoop, shared_from_this()));
    retry_time_ = 
        (retry_time_*2) < kMaxRetryTime ? retry_time_*2 : kMaxRetryTime;
  }
}

void Connector::OnConnect() {
  MIRANTS_LOG(TRACE) << "Connector::OnConnect - " << StateToString();

  if (state_ == kConnecting) {
    int socketfd = DeleteOldDispatch();
    Status st = sockets::CheckSocketError(socketfd);
    if (!st.ok()) {
      MIRANTS_LOG(WARN) << st;
      Retry(socketfd);
    } else if (sockets::IsSelfConnect(socketfd) == 0) {
      MIRANTS_LOG(WARN) << "Connector::OnConnect - Self connect";
      Retry(socketfd);
    } else {
      state_ = kConnected;
      if (connect_) {
        if (newconnection_cb_) {
          newconnection_cb_(socketfd);
        }
      } else {
        sockets::CloseFd(socketfd);
      }
    }
  } else {
    assert(state_ == kDisConnected);
  }
}

void Connector::HandleError() {
  MIRANTS_LOG(ERROR) << "Connector::HandleError - state_=" << StateToString();
  if (state_ == kConnecting) {
    int socketfd = DeleteOldDispatch();
    Status st = sockets::CheckSocketError(socketfd);
    if (!st.ok()) {
      MIRANTS_LOG(ERROR) << st.ToString();
    }
    Retry(socketfd);
  }
}

int Connector::DeleteOldDispatch() {
  dispatch_->DisableAll();
  dispatch_->RemoveEvents();
  int socketfd = dispatch_->Fd();
  ev_->QueueInLoop(std::bind(&Connector::ResetDispatch, this));
  return socketfd;
}

void Connector::ResetDispatch() {
  dispatch_.reset();
}

std::string Connector::StateToString() const {
  const char* type;
  switch (state_) {
    case kDisConnected:
      type = "DisConnected";
      break;
    case kConnected:
      type = "Connected";
      break;
    case kConnecting:
      type = "Connecting";
      break;
    default:
      type = "Unknown State";
      break;
  }
  std::string result(type);
  return result;
}

}  // namespace mirants
