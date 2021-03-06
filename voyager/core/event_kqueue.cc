// Copyright (c) 2016 Mirants Lu. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "voyager/core/event_kqueue.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <string.h>

#include "voyager/core/dispatch.h"
#include "voyager/util/logging.h"

namespace voyager {

EventKqueue::EventKqueue(EventLoop* ev)
    : EventPoller(ev), kq_(::kqueue()), events_(kInitKqueueFdSize) {
  if (kq_ == -1) {
    VOYAGER_LOG(FATAL) << "kqueue: " << strerror(errno);
  }
}

EventKqueue::~EventKqueue() {}

void EventKqueue::Poll(int timeout, std::vector<Dispatch*>* dispatches) {
  struct timespec out;
  out.tv_sec = timeout / 1000;
  out.tv_nsec = (timeout % 1000) * 1000 * 1000;
  int nfds = ::kevent(kq_, nullptr, 0, &*events_.begin(),
                      static_cast<int>(events_.size()), &out);
  if (nfds == -1) {
    VOYAGER_LOG(ERROR) << "kevent: " << strerror(errno);
  }

  for (int i = 0; i < nfds; ++i) {
    Dispatch* dispatch = reinterpret_cast<Dispatch*>(events_[i].udata);
    int revents = 0;
    if (events_[i].flags == EV_ERROR) {
      revents = POLLERR;
    } else if (events_[i].filter == EVFILT_READ) {
      revents = POLLIN;
    } else if (events_[i].filter == EVFILT_WRITE) {
      revents = POLLOUT;
    }

    dispatch->SetRevents(revents);
    dispatches->push_back(dispatch);
  }
  if (nfds == static_cast<int>(events_.size())) {
    events_.resize(events_.size() * 2);
  }
}

void EventKqueue::RemoveDispatch(Dispatch* dispatch) {
  eventloop_->AssertInMyLoop();
  int fd = dispatch->Fd();
  assert(dispatch_map_.find(fd) != dispatch_map_.end());
  assert(dispatch_map_[fd] == dispatch);
  assert(dispatch->IsNoneEvent());
  dispatch_map_.erase(fd);
  dispatch->SetIndex(-1);
}

void EventKqueue::UpdateDispatch(Dispatch* dispatch) {
  eventloop_->AssertInMyLoop();
  int fd = dispatch->Fd();
  int modify = dispatch->Modify();

  struct kevent ev[2];
  int n = 0;
  switch (modify) {
    case Dispatch::kAddRead:
      EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0,
             reinterpret_cast<void*>(dispatch));
      break;

    case Dispatch::kAddWrite:
      EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0,
             reinterpret_cast<void*>(dispatch));
      break;

    case Dispatch::kDeleteRead:
      EV_SET(&ev[n++], fd, EVFILT_READ, EV_DELETE, 0, 0,
             reinterpret_cast<void*>(dispatch));
      break;

    case Dispatch::kDeleteWrite:
      EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0,
             reinterpret_cast<void*>(dispatch));
      break;

    case Dispatch::kEnableWrite:
      EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ENABLE, 0, 0,
             reinterpret_cast<void*>(dispatch));
      break;

    case Dispatch::kDisableWrite:
      EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_DISABLE, 0, 0,
             reinterpret_cast<void*>(dispatch));
      break;

    case Dispatch::kDeleteAll:
      EV_SET(&ev[n++], fd, EVFILT_READ, EV_DELETE, 0, 0,
             reinterpret_cast<void*>(dispatch));
      EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0,
             reinterpret_cast<void*>(dispatch));
      break;

    case Dispatch::kNoModify:
      break;
  }

  if (::kevent(kq_, ev, n, nullptr, 0, nullptr) == -1) {
    VOYAGER_LOG(ERROR) << "kevent: " << strerror(errno);
  }
  if (dispatch->Index() == -1) {
    assert(dispatch_map_.find(fd) == dispatch_map_.end());
    dispatch_map_[fd] = dispatch;
    dispatch->SetIndex(0);
  } else {
    assert(dispatch_map_.find(fd) != dispatch_map_.end());
  }
}

}  // namespace voyager
