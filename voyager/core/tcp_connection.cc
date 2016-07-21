#include "voyager/core/tcp_connection.h"
#include "voyager/core/dispatch.h"
#include "voyager/core/eventloop.h"
#include "voyager/core/online_connections.h"
#include "voyager/core/socket_util.h"
#include "voyager/util/logging.h"
#include "voyager/util/slice.h"

#include <errno.h>

namespace voyager {

TcpConnection::TcpConnection(const std::string& name, EventLoop* ev, int fd)
    : name_(name), 
      eventloop_(CHECK_NOTNULL(ev)),
      socket_(fd),
      state_(kConnecting),
      dispatch_(new Dispatch(ev, fd)) {
  dispatch_->SetReadCallback(std::bind(&TcpConnection::HandleRead, this));
  dispatch_->SetWriteCallback(std::bind(&TcpConnection::HandleWrite, this));
  dispatch_->SetCloseCallback(std::bind(&TcpConnection::HandleClose, this));
  dispatch_->SetErrorCallback(std::bind(&TcpConnection::HandleError, this));
  socket_.SetKeepAlive(true);
  socket_.SetTcpNoDelay(true);
  VOYAGER_LOG(DEBUG) << "TcpConnection::TcpConnection [" << name_ << "] at "
                     << this << " fd=" << fd;
}
 
TcpConnection::~TcpConnection() {
  VOYAGER_LOG(DEBUG) << "TcpConnection::~TcpConnection [" << name_ 
                     << "] at " << this << " fd=" << dispatch_->Fd()
                     << " ConnectState=" << StateToString();
  assert(state_ == kDisconnected);
}

void TcpConnection::EstablishConnection() {
  eventloop_->AssertThreadSafe();
  assert(state_ == kConnecting);
  state_ = kConnected;
  dispatch_->Tie(shared_from_this());
  dispatch_->EnableRead();
  if (connection_cb_) {
    connection_cb_(shared_from_this());
  }
}

void TcpConnection::StartRead() {
  eventloop_->RunInLoop(std::bind(&TcpConnection::StartReadInLoop, this));
}

void TcpConnection::StartReadInLoop() {
  eventloop_->AssertThreadSafe();
  if (!dispatch_->IsReading()) {
    dispatch_->EnableRead();
  }
}

void TcpConnection::StopRead() {
  eventloop_->RunInLoop(std::bind(&TcpConnection::StopReadInLoop, this));
}

void TcpConnection::StopReadInLoop() {
  eventloop_->AssertThreadSafe();
  if (dispatch_->IsReading()) {
    dispatch_->DisableRead();
  }
}

void TcpConnection::ShutDown() {
  if (state_ == kConnected) {
    state_ = kDisconnecting;
    eventloop_->RunInLoop(std::bind(&TcpConnection::ShutDownInLoop, this));
  }
}

void TcpConnection::ShutDownInLoop() {
  eventloop_->AssertThreadSafe();
  if (!dispatch_->IsWriting()) {
    socket_.ShutDownWrite();
  }
}

void TcpConnection::ForceClose() {
  if (state_ == kConnected || state_ == kDisconnecting) {
    state_ = kDisconnecting;
    eventloop_->QueueInLoop(
        std::bind(&TcpConnection::ForceCloseInLoop, shared_from_this()));
  }
}

void TcpConnection::ForceCloseInLoop() {
  eventloop_->AssertThreadSafe();
  if (state_ == kConnected || state_ == kDisconnecting) {
    HandleClose();
  }
}

void TcpConnection::HandleRead() {
  eventloop_->AssertThreadSafe();
  int err;
  ssize_t n = readbuf_.ReadV(dispatch_->Fd(), err);
  if (n > 0) {
    if (message_cb_) {
      message_cb_(shared_from_this(), &readbuf_);
    }
  } else if (n == 0) {
    HandleClose();
  } else {
    errno = err;
    VOYAGER_LOG(ERROR) << "TcpConnection::HandleRead [" << name_ 
                       <<"] - readv: " << strerror(errno);
  }
}

void TcpConnection::HandleWrite() {
  eventloop_->AssertThreadSafe();
  if (dispatch_->IsWriting()) {
    ssize_t n = sockets::Write(dispatch_->Fd(), 
                               writebuf_.Peek(), 
                               writebuf_.ReadableSize());
    int err = errno;
    if (n > 0) {
      writebuf_.Retrieve(static_cast<size_t>(n));
      if (writebuf_.ReadableSize() == 0) {
        dispatch_->DisableWrite();
        if (writecomplete_cb_) {
          eventloop_->QueueInLoop(
              std::bind(writecomplete_cb_, shared_from_this()));
        }
        if (state_ == kDisconnecting) {
          ShutDownInLoop();
        }
      }
    } else {
      VOYAGER_LOG(ERROR) << "TcpConnection::HandleWrite [" << name_ 
                         << "] - write: " << strerror(err);
    }
  } else {
    VOYAGER_LOG(INFO) << "TcpConnection::HandleWrite [" << name_ 
                      << "] - fd=" << dispatch_->Fd() 
                      << " is down, no more writing";
  }
}

void TcpConnection::HandleClose() {
  eventloop_->AssertThreadSafe();
  assert(state_ == kConnected || state_ == kDisconnecting);
  TcpConnectionPtr guard(shared_from_this());
  state_ = kDisconnected;
  dispatch_->DisableAll();
  if (close_cb_) {
    close_cb_(guard);
  }
  dispatch_->RemoveEvents();
  
  port::Singleton<OnlineConnections>::Instance().EraseCnnection(guard);
}

void TcpConnection::HandleError() {
  Status st = sockets::CheckSocketError(dispatch_->Fd());
  if (!st.ok()) {
    VOYAGER_LOG(ERROR) << "TcpConnection::HandleError [" << name_
                       << "] - " << st.ToString();
  }
}

void TcpConnection::SendMessage(std::string&& message) {
  if (state_ == kConnected) {
    if (eventloop_->IsInCreatedThread()) {
      SendInLoop(&*message.begin(), message.size());
    } else {
      eventloop_->RunInLoop(std::bind(&TcpConnection::SendInLoop, this,
                                      &*message.begin(), message.size())); 
    }
  }
}

void TcpConnection::SendMessage(const Slice& message) {
  if (state_ == kConnected) {
    if (eventloop_->IsInCreatedThread()) {
      SendInLoop(message.data(), message.size());
    } else {
      std::string s(message.ToString());
      eventloop_->RunInLoop(std::bind(&TcpConnection::SendInLoop, this, 
                                      &*s.begin(), s.size()));
    }
  }
}

void TcpConnection::SendMessage(Buffer* message) {
  CHECK_NOTNULL(message);
  if (state_ == kConnected) {
    if (eventloop_->IsInCreatedThread()) {
      SendInLoop(message->Peek(), message->ReadableSize());
      message->RetrieveAll();
    } else {
      std::string s(message->RetrieveAllAsString());
      eventloop_->RunInLoop(std::bind(&TcpConnection::SendInLoop, this, 
                                      &*s.begin(), s.size()));
    }
  }
}

void TcpConnection::SendInLoop(const void* data, size_t size) {
  eventloop_->AssertThreadSafe();
  if (state_ == kDisconnected) {
    VOYAGER_LOG(WARN) << "TcpConnection::SendInLoop[" << name_ << "]"
                      << "has disconnected, give up writing.";
    return;
  }

  ssize_t nwrote = 0;
  size_t  remaining = size;
  bool fault = false;

  if (!dispatch_->IsWriting() && writebuf_.ReadableSize() == 0) {
    nwrote = sockets::Write(dispatch_->Fd(), data, size);
    int err = errno;
    if (nwrote >= 0) {
      remaining = size - static_cast<size_t>(nwrote);
      if (remaining == 0 && writecomplete_cb_) {
        eventloop_->QueueInLoop(
            std::bind(writecomplete_cb_, shared_from_this()));
      }
    } else {
      nwrote = 0;
      errno = err;
      if (errno != EWOULDBLOCK) {
        VOYAGER_LOG(ERROR) << "TcpConnection::SendInLoop [" << name_ 
                           << "] - write: " << strerror(errno);
        if (errno == EPIPE || errno == ECONNRESET) {
          fault = true;
        }
      }
    }
  }

  assert(remaining <= size);
  if (!fault && remaining > 0) {
    writebuf_.Append(static_cast<const char*>(data)+nwrote, remaining);
    if (!dispatch_->IsWriting()) {
      dispatch_->EnableWrite();
    }
  }
}

std::string TcpConnection::StateToString() const {
  const char *type;
  switch (state_) {
    case kDisconnected:
      type = "Disconnected";
      break;
    case kDisconnecting:
      type = "Disconnecting";
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

}  // namespace voyager
