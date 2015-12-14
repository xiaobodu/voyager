#include "core/socket_util.h"

#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>

#include "util/logging.h"
#include "util/stringprintf.h"

namespace mirants {
namespace sockets{

int CreateSocket(int domain) {
  int socketfd = ::socket(domain, SOCK_STREAM, IPPROTO_TCP);
  if (socketfd == -1) {
    MIRANTS_LOG(FATAL) << "socket: " << strerror(errno);
  }
  return socketfd;
}

void CloseFd(int socketfd) {
  if (::close(socketfd) == -1) {
    MIRANTS_LOG(ERROR) << "close: " << strerror(errno);
  }
}

void BindAddress(int socketfd, const struct sockaddr_in* sa) {
  int ret = ::bind(socketfd,
                   static_cast<const struct sockaddr*>(sa),
                   static_cast<socklen_t>(sizeof(*sa)));
  if (ret == -1) {
    MIRANTS_LOG(FATAL) << "bind: " << strerror(errno);
  }
}

void Listen(int socketfd) {
  int ret = ::listen(socketfd, SOMAXCONN);
  if (ret == -1) {
    MIRANTS_LOG(FATAL) << "listen: " << strerror(errno);
  }
}

void Accept(int socketfd, struct sockaddr_in* sa) {
  int connectfd = ::accept(socketfd, 
                           static_cast<const struct sockaddr*>(sa),
                           static_cast<socklen_t>(sizeof(*sa)));
  if (connectfd == -1) {
    int err = errno;
    switch (err) {
      case EAGAIN:
      case ECONNABORTED:
      case EINTR:
      case EPROTO: 
      case EPERM:
      case EMFILE: 
        MIRANTS_LOG(ERROR) << "Accept: " << strerror(err);
        break;
      case EBADF:
      case EFAULT:
      case EINVAL:
      case ENFILE:
      case ENOBUFS:
      case ENOMEM:
      case ENOTSOCK:
      case EOPNOTSUPP:
        MIRANTS_LOG(FATAL) << "Accept unexpected error: " << strerror(err);
        break;
      default:
        MIRANTS_LOG(FATAL) << "Accept unkown error " << strerror(err);      
  }

  Status status = SetBlocking(connectfd, false);
  if (!status.ok()) {
    MIRANTS_LOG(ERROR) << status;
  }
}

int Connect(int socketfd, const struct sockaddr_in* sa) {
  int ret = ::connect(socketfd, 
                      static_cast<const struct sockaddr* >(sa), 
                      static_cast<socklen_t>(sizeof (*sa)));
  return ret;
}


Status SetBlocking(int socketfd, bool blocking) {
  int flags = ::fcntl(socketfd, F_GETFL, 0);
  if (flags == -1) {
    std::string str;
    StringAppendF(&str, "fcntl(F_GETFL): %s", strerror(errno));
    return Status::IOError(str);
  }

  if (blocking) {
    flags &= ~O_NONBLOCK;
  } else {
    flags |= O_NONBLOCK;
  }

  if (::fcntl(socketfd, F_SETFL, flags) == -1) {
    std::string str;
    StringAppendF(&str, "fcntl(F_SETFL, O_NONBLOCK): %s", strerror(errno));
    return Status::IOError(str);
  }
  return Status::OK();
}

Status SetReuseAddr(int socketfd, bool reuse) {
  int on = reuse ? 1 : 0;
  if (::setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, 
                   &on, static_cast<socklen_t>(sizeof(on))) == -1) {
    std::string str;
    StringAppendF(&str, "setsockopt SO_REUSEADDR: %s", strerror(errno));
    return Status::IOError(str);
  }
  return Status::OK();
}


Status SetReusePort(int socketfd, bool reuse) {
#ifdef SO_REUSEPORT
  int on = reuse ? 1 : 0;
  if (::setsockopt(socketfd, SOL_SOCKET, SO_REUSEPORT, 
                   &on, static_cast<socklen_t>(sizeof(on))) == -1) {
    std::string str;
    StringAppendF(&str, "setsockopt SO_REUSEPORT: %s", strerror(errno));
    return Status::IOError(str);
  }
  return Status::OK();
#endif
}

Status SetKeepAlive(int socketfd, bool alive) {
  int on = alive ? 1 : 0;
  if (::setsockopt(socketfd, SOL_SOCKET, SO_KEEPALIVE, 
                   &on, static_cast<socklen_t>(sizeof(on))) == -1) {
    std::string str;
    StringAppendF(&str, "setsockopt SO_KEEPALIVE: %s", strerror(errno));
    return Status::IOError(str);
  }
  return Status::OK();
}


Status SetTcpNotDelay(int socketfd, bool notdelay) {
  int on = notdelay ? 1 : 0;
  if (::setsockopt(socketfd, IPPROTO_TCP, TCP_NODELAY, 
                   &on, static_cast<socklen_t>(sizeof(on))) == -1) {
    std::string str;
    StringAppendF(&str, "setsockopt TCP_NODELAY: %s", strerror(errno));
    return Status::IOError(str);
  }
  return Status::OK();
}

Status CheckSocketError(int socketfd) {
  int err = 0;
  socklen_t errlen = static_cast<socklen_t>(sizeof(err));

  if (::getsockopt(socketfd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1) {
    std::string str;
    StringAppendF(&str, "getsockopt (SO_ERROR): %s", strerror(errno));
    return Status::IOError(str);
  }
  if (err) {
    errno = err;
    char buf[30];
    strerror_r(errno, buf, sizeof(buf));
    return Status::IOError(buf);
  }
  return Status::OK();
}

static Status GenericResolve(const char* hostname, char* ipbuf, size_t ipbuf_size, 
                             bool ip_only) {
  struct addrinfo hints, *result;
  int error;

  memset(&hints, 0, sizeof(hints));
  if (ip_only) {
    hints.ai_flags = AI_NUMERICHOST;
  }
  hints.ai_family = AF_UNSPEC;
  hints/ai_socktype = SOCK_STREAM;

  error = ::getaddrinfo(hostname, NULL, &hints, &result);
  if (err != 0) {
    std::string str;
    StringAppendF(&str, "getaddrinfo: %s", gai_strerror(error));
    return Status::IOError(str);
  }
  if (result->ai_family == AF_INET) {
    assert(ipbuf_size >= INET_ADDRSTRLEN);
    struct sockaddr_in* sa4 = 
        static_cast<struct sockaddr_in*>(result->ai_addr);
    ::inet_ntop(AF_INET, &(sa4->sin_addr), ipbuf, ipbuf_size);
  } else if (result->ai_family == AF_INET6){
    assert(ipbuf_size >= INET6_ADDRSTRLEN);
    struct sockaddr_in6* sa6 =
        static_cast<struct sockaddr_in6*>(result->ai_addr);
    ::inet_ntop(AF_INET6, &(sa6->sin6_addr), ipbuf, ipbuf_size);
  }

  ::freeaddrinfo(result);
  return Status::OK();
}

Status Resolve(const char* hostname, char* ipbuf, size_t ipbuf_size) {
  return GenericResolve(hostname, ipbuf, ipbuf_size, false);
}

Status ResolveIP(const char* host, char* ipbuf, size_t ipbuf_size) {
  return GenericResolve(hostname, ipbuf, ipbuf_sizem true);
}

int FormatAddr(const char* ip, int port, char* buf, size_t buf_size) {
  return snprintf(buf, buf_size, strchr(ip, ':') ?
                  "[%s]:%d" : "%s:%d", ip, port);
}

static void PeerToString(int socketfd, char* ip, size_t ip_size, int* port) {
  struct sockaddr_storage sa = std::move(PeerSockAddr(socketfd));

  if (sa.ss_family == AF_INET) {
    struct sockaddr_in* sa4 = static_cast<struct sockaddr_in*>(&sa);
    if (ip) inet_ntop(AF_INET, &sa4->sin_addr, ip, ip_size);
    if (port) *port = ::ntohs(sa4->sin_port);
  } else if (sa.ss_family == AF_INET6) {
    struct sockaddr_in6* sa6 = static_cast<struct sockaddr_in6*>(&sa);
    if (ip) inet_ntop(AF_INET6, &sa6->sin6_addr, ip, ip_size);
    if (port) *port = ::ntohs(sa6->sin6_port);
  }
}

int FormatPeer(int socketfd, char* buf, char buf_size) {
  char  ip[INET6_ADDRSTRLEN];
  int   port;
  PeerToString(socketfd, ip, sizeof(ip), &port);
  return FormatAddr(buf, buf_size, ip, port);
}

void SockAddrToIP(const struct sockaddr* sa, char* ipbuf, size_t ipbuf_size {
  if (sa->ai_family == AF_INET) {
    assert(ipbuf_size >= INET_ADDRSTRLEN);
    struct sockaddr_in* sa4 = 
        static_cast<struct sockaddr_in*>(sa);
    ::inet_ntop(AF_INET, &sa4->sin_addr, ipbuf, ipbuf_size);
  } else if (sa->ai_family == AF_INET6) {
    assert(sa->ai_family >= INET6_ADDRSTRLEN);
    struct sockaddr_in6* sa6 = 
        static_cast<struct sockaddr_in6*>(sa);
    ::inet_ntop(AF_INET6, &sa6->sin6_addr, ipbuf, ipbuf_size);
  }
}

int SockAddrToIPPort(const struct sockaddr* sa, char* buf, size_t buf_size) {
  char ip[INET6_ADDRSTRLEN];
  SockAddrToIP(sa, ip, size_t(ip));
  const struct sockaddr_in* sa4 = static_cast<struct sockaddr_in*>(sa);
  uint16_t port = ::ntohs(sa4->sin_port);
  return FormatAddr(ip, static_cast<int>(port), buf, buf_size);
}

struct sockaddr_storage PeerSockAddr(int socketfd) {
  struct sockaddr_storage sa;
  socklen_t salen = sizeof(sa);

  if (::getpeername(socketfd, 
                    static_cast<struct sockaddr*>(&sa), &salen) == -1) {
    MIRANTS_LOG(ERROR) << "getpeername failed";
  }
  return sa;
}

struct sockaddr_storage LocalSockAddr(int socketfd) {
  struct sockaddr_storage sa;
  socklen_t salen = sizeof(sa);
  if (::getsockname(socketfd, 
                    static_cast<struct sockaddr*>(&sa), &salen) == -1) {
    MIRANTS_LOG(ERROR) << "getsockname failed";
  }
  return sa;
}

}  // namespace sockets
}  // namespace mirants
