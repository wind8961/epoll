/*@file psn-epoll.cpp
 *
 * MIT License
 *
 * Copyright (c) 2022 phit666
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "epoll.h"
#include <bcrypt.h>
#include <map>
#include <mutex>
#include <errno.h>
#include <assert.h>
#include <mswsock.h>

#define EPOLL_MAX_FD 2000000
#define IOCTL_AFD_POLL 0x00012024

static std::recursive_mutex m1;

#ifdef _WIN32
enum class epoll_status {
    EPOLL_IDLE,
    EPOLL_PENDING,
    EPOLL_CANCELLED
};

typedef struct _AFD_POLL_HANDLE_INFO {
    HANDLE Handle;
    ULONG Events;
    NTSTATUS Status;
} AFD_POLL_HANDLE_INFO, * PAFD_POLL_HANDLE_INFO;

typedef struct _AFD_POLL_INFO {
    LARGE_INTEGER Timeout;
    ULONG NumberOfHandles;
    ULONG Exclusive;
    AFD_POLL_HANDLE_INFO Handles[1];
} AFD_POLL_INFO, * PAFD_POLL_INFO;

typedef struct _epoll_info {
    OVERLAPPED ol;
    AFD_POLL_INFO pollinfo;
    epoll_status pollstatus;
    uint32_t pendingevents;
    char pendingdelete;
    epoll_event epollevent;
    HANDLE phwnd;
    SOCKET socket;
    SOCKET peer_socket;
}epoll_info, *pepoll_info;

static std::map<int, HANDLE> mfd2hwnd;
static std::map<int, socket_t> mfd2sock;
static std::map<socket_t, int> msock2fd;
static std::map<int, pepoll_info> mevents;
static std::map<int, pepoll_info> mevents_copy;
static int epfdctr = 0;
static int fdctr = 0;

inline static int afdpoll(HANDLE pafddevhwnd, AFD_POLL_INFO* poll_info, LPOVERLAPPED ol) {
    DWORD bytes;
    BOOL success = DeviceIoControl(pafddevhwnd, IOCTL_AFD_POLL, poll_info, sizeof * poll_info, poll_info, sizeof * poll_info, &bytes, ol);
    if (success == FALSE) {
        errno = GetLastError();
        return -1;
    }
    return 0;
}

inline static int afdcancelpoll(HANDLE pafddevhwnd, LPOVERLAPPED ol) {
    BOOL success = CancelIoEx(pafddevhwnd, ol);
    if (success == FALSE) {
        errno = GetLastError();
        return -1;
    }
    return 0;
}

static const GUID msafd_provider_ids[3] = {
  {0xe70f1aa0, 0xab8b, 0x11cf,
      {0x8c, 0xa3, 0x00, 0x80, 0x5f, 0x48, 0xa1, 0x92}},
  {0xf9eab0c0, 0x26d4, 0x11d0,
      {0xbb, 0xbf, 0x00, 0xaa, 0x00, 0x6c, 0x34, 0xe4}},
  {0x9fc48064, 0x7298, 0x43e4,
      {0xb7, 0xbd, 0x18, 0x1f, 0x20, 0x89, 0x79, 0x2a}}
};

inline static SOCKET create_peer_socket(HANDLE iocp,
    WSAPROTOCOL_INFOW* protocol_info) {
    SOCKET sock = 0;

    sock = WSASocketW(protocol_info->iAddressFamily,
        protocol_info->iSocketType,
        protocol_info->iProtocol,
        protocol_info,
        0,
        WSA_FLAG_OVERLAPPED);
    if (sock == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    if (!SetHandleInformation((HANDLE)sock, HANDLE_FLAG_INHERIT, 0)) {
        goto error;
    };

    if (CreateIoCompletionPort((HANDLE)sock,
        iocp,
        (ULONG_PTR)sock,
        0) == NULL) {
        goto error;
    }

    return sock;

error:
    closesocket(sock);
    return INVALID_SOCKET;
}

inline static SOCKET get_peer_socket(HANDLE ephwnd, WSAPROTOCOL_INFOW* protocol_info) {
    int index, i;

    index = -1;
    for (i = 0; (size_t)i < 3; i++) {
        if (memcmp((void*)&protocol_info->ProviderId,
            (void*)&msafd_provider_ids[i],
            sizeof protocol_info->ProviderId) == 0) {
            index = i;
        }
    }

    if (index < 0) {
        return INVALID_SOCKET;
    }

    return create_peer_socket(ephwnd, protocol_info);
}
#endif

static int closed = 0;

int epoll_sock2fd(socket_t s) {
#ifdef _WIN32
    std::map<socket_t, int>::iterator iter;
    std::map<int, socket_t>::iterator iter2;

    std::lock_guard<std::recursive_mutex> lock1(m1);
    
    iter = msock2fd.find(s);
    if (iter != msock2fd.end())
        return iter->second;
   
    for (;;) {
        ++fdctr;
        if (fdctr > EPOLL_MAX_FD)
            fdctr = 1;
        iter2 = mfd2sock.find(fdctr);
        if (iter2 == mfd2sock.end())
            break;
    }

    msock2fd.insert(std::pair<socket_t, int>(s, fdctr));
    mfd2sock.insert(std::pair<int, socket_t>(fdctr, s));

    return fdctr;
#else
    return s;
#endif
}

socket_t epoll_fd2sock(int fd) {
#ifdef _WIN32
    std::map<int, socket_t>::iterator iter;
    std::lock_guard<std::recursive_mutex> lock1(m1);
    iter = mfd2sock.find(fd);
    if (iter != mfd2sock.end())
        return iter->second;
    return INVALID_SOCKET;
#else
    return fd;
#endif
}

void epoll_postqueued(int epfd) {
#ifdef _WIN32
    std::lock_guard<std::recursive_mutex> lock1(m1);
    closed = 1;
    PostQueuedCompletionStatus(mfd2hwnd[epfd], 0, 0, NULL);
#endif
}

#ifdef _WIN32

static void _delefd(int fd) {
    std::map<int, pepoll_info>::iterator iter;
    iter = mevents.find(fd);
    if (iter != mevents.end()) {
        free(iter->second);
        mevents.erase(iter);
    }
}

static int _existefd(int fd) {
    std::map<int, pepoll_info>::iterator iter;
    iter = mevents.find(fd);
    if (iter != mevents.end())
        return 1;
    return 0;
}

static int _existepfd(int epfd) {
    std::map<int, HANDLE>::iterator iter;
    iter = mfd2hwnd.find(epfd);
    if (iter != mfd2hwnd.end())
        return 1;
    return 0;
}

void close(int epfd) {
    std::lock_guard<std::recursive_mutex> lock1(m1);
    if (mfd2hwnd[epfd] == NULL)
        return;
    CloseHandle(mfd2hwnd[epfd]);
    mfd2hwnd.clear();
    mfd2sock.clear();
    msock2fd.clear();
    closed = 1;
    std::map<int, pepoll_info>::iterator iter;
    for (iter = mevents.begin(); iter != mevents.end(); iter++) {
        free(iter->second);
    }
    mevents.clear();
}

static int _epollreqpoll(int fd, pepoll_info epoll_info) {

    assert(epoll_info != NULL);
    epoll_info->pollinfo.Exclusive = FALSE;
    epoll_info->pollinfo.NumberOfHandles = 1;
    epoll_info->pollinfo.Timeout.QuadPart = INT64_MAX;
    epoll_info->pollinfo.Handles[0].Handle = (HANDLE)epoll_info->socket;
    epoll_info->pollinfo.Handles[0].Status = 0;
    epoll_info->pollinfo.Handles[0].Events = epoll_info->epollevent.events;

    if (afdpoll((HANDLE)epoll_info->peer_socket, &epoll_info->pollinfo, &epoll_info->ol) < 0) {
        switch (errno) {
        case ERROR_IO_PENDING:
            break;
        case ERROR_INVALID_HANDLE:
            if (afdcancelpoll((HANDLE)epoll_info->peer_socket,
                &epoll_info->ol) < 0)
                return -1;
            epoll_info->pollstatus = epoll_status::EPOLL_CANCELLED;
            epoll_info->pendingevents = 0;
            epoll_info->pendingdelete = 1;
            return -1;
        default:
            return -1;
        }
    }
    epoll_info->pollstatus = epoll_status::EPOLL_PENDING;
    epoll_info->pendingevents = epoll_info->pollinfo.Handles[0].Events;
    return 0;
}

static int _epoll_update_events() {
    pepoll_info _epoll_info = NULL;
    int fd = -1;

    std::map<int, pepoll_info>::iterator iter;

    std::lock_guard<std::recursive_mutex> lock1(m1);

    iter = mevents_copy.begin();

    while(iter != mevents_copy.end()){

        fd = iter->first;
        _epoll_info = iter->second;

        if (!mevents[fd]) {
            iter = mevents_copy.erase(iter);
            continue;
        }

        if (_epoll_info->pollstatus == epoll_status::EPOLL_PENDING &&
            _epoll_info->pendingevents == 0) {
            if (afdcancelpoll((HANDLE)_epoll_info->peer_socket,
                &_epoll_info->ol) < 0) {
                return -1;
            }

            _epoll_info->pollstatus = epoll_status::EPOLL_CANCELLED;
            _epoll_info->pendingevents = 0;
        }
        else if (_epoll_info->pollstatus == epoll_status::EPOLL_CANCELLED) {
        }
        else if (_epoll_info->pollstatus == epoll_status::EPOLL_IDLE) {
            if (_epollreqpoll(fd, _epoll_info) < 0) {
                return -1;
            }
            iter = mevents_copy.erase(iter);
            continue;
        }

        iter++;
    }

    return 0;
}


int epoll_create(int size) {
	if (!size)
		return -1;
	HANDLE phwnd = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    std::lock_guard<std::recursive_mutex> lock1(m1);
    mfd2hwnd.insert(std::pair<int, HANDLE>(++epfdctr, phwnd));
    return epfdctr;
}

int epoll_create1(int flags) {
	return epoll_create(1);
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event* event) {

    SOCKET s = epoll_fd2sock(fd);
    SOCKET peer_socket = INVALID_SOCKET;
    pepoll_info _epoll_info = NULL;
    WSAPROTOCOL_INFOW protocol_info;
    int len;
    SOCKET basesocket = INVALID_SOCKET;
    DWORD returnbytes;

    if (s == INVALID_SOCKET) {
        errno = EBADF;
        return -1;
    }

    if (!_existepfd(epfd) || (event == NULL && EPOLL_CTL_DEL != op)) {
        errno = EINVAL;
        return -1;
    }

    switch (op) {

    case EPOLL_CTL_DEL:
        if (!_existefd(fd)) {
            errno = ENOENT;
            return -1;
        }
        _delefd(fd);
        break;

    case EPOLL_CTL_MOD:
        if (!_existefd(fd)) {
            errno = ENOENT;
            return -1;
        }
        _epoll_info = mevents[fd];
        if (_epoll_info == NULL) {
            errno = EINVAL;
            return -1;
        }
        memcpy(&_epoll_info->epollevent, event, sizeof(_epoll_info->epollevent));
        break;

    case EPOLL_CTL_ADD:
    {
        if (_existefd(fd)) {
            errno = EEXIST;
            return -1;
        }

        _epoll_info = (pepoll_info)calloc(1, sizeof(epoll_info));

        if (_epoll_info == NULL) {
            errno = ENOMEM;
            return -1;
        }

        if (WSAIoctl(s, SIO_BASE_HANDLE, NULL, NULL, &basesocket, sizeof(basesocket), &returnbytes, NULL, NULL) == SOCKET_ERROR) {
            errno = WSAGetLastError();
            return -1;
        }

        if (s != basesocket) {
            mfd2sock[fd] = basesocket;
        }

        s = basesocket;

        len = sizeof protocol_info;
        if (getsockopt(s,
            SOL_SOCKET,
            SO_PROTOCOL_INFOW,
            (char*)&protocol_info,
            &len) != 0) {
            return -1;
        }

        peer_socket = get_peer_socket(mfd2hwnd[epfd], &protocol_info);

        if (peer_socket == INVALID_SOCKET) {
            if (!SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0)) {
                return -1;
            };
            if (CreateIoCompletionPort((HANDLE)s,
                mfd2hwnd[epfd],
                (ULONG_PTR)s,
                0) == NULL) {
                return -1;
            }
            peer_socket = s;
        }

        _epoll_info->pendingdelete = 0;
        _epoll_info->socket = s;
        _epoll_info->peer_socket = peer_socket;
        _epoll_info->pollstatus = epoll_status::EPOLL_IDLE;
        _epoll_info->pendingevents = 0;
        memcpy(&_epoll_info->epollevent, event, sizeof(_epoll_info->epollevent));
        mevents.insert(std::pair<int, pepoll_info>(fd, _epoll_info));
        mevents_copy.insert(std::pair<int, pepoll_info>(fd, _epoll_info));
        _epoll_update_events();
    }
        break;

    default:
        errno = EINVAL;
        return -1;
    }

    return 0;
}

int epoll_wait(int epfd, struct epoll_event* events,
	int maxevents, int timeout) {

    ULONG notificationCount;
    OVERLAPPED_ENTRY notification[256];
    pepoll_info _epoll_info = NULL;
    AFD_POLL_INFO* _poll_info = NULL;
    uint32_t epoll_events = 0;

    if (!mfd2hwnd[epfd]) {
        errno = EINVAL;
        return -1;
    }

    if (closed != 0) {
        return 0;
    }

    if (events == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (maxevents < 1) {
        errno = EINVAL;
        return -1;
    }

    if (maxevents > 256) {
        maxevents = 256;
    }

    if (_epoll_update_events() < 0) {
        return -1;
    }

    BOOL bsuccess = GetQueuedCompletionStatusEx(mfd2hwnd[epfd], notification, maxevents, &notificationCount, timeout, false);

    if (bsuccess != TRUE) {
        if (GetLastError() == WAIT_TIMEOUT)
            return 0;
        errno = EINVAL;
        return -1;
    }

    std::lock_guard<std::recursive_mutex> lock1(m1);

    if (closed != 0) {
        return 0;
    }

    int i = 0;

    for (int n = 0; n < notificationCount; n++) {

        pepoll_info _epoll_info = (pepoll_info)notification[i].lpOverlapped;

        _poll_info = &_epoll_info->pollinfo;
        epoll_events = 0;

        _epoll_info->pollstatus = epoll_status::EPOLL_IDLE;
        _epoll_info->pendingevents = 0;

        mevents_copy.insert(std::pair<int, pepoll_info>(_epoll_info->epollevent.data.fd, _epoll_info));

        if (_epoll_info->pendingdelete == 1) {
            epoll_events = EPOLLHUP;
        }
        else if (_poll_info->NumberOfHandles < 1) {
        }
        else {
            epoll_events = _poll_info->Handles[0].Events;
        }

        epoll_events &= _epoll_info->epollevent.events;

        if (epoll_events == 0)
            continue;

        if (_epoll_info->epollevent.events & EPOLLONESHOT)
            _epoll_info->epollevent.events = AFD_POLL_LOCAL_CLOSE;

        events[i].events = epoll_events;
        events[i].data.u32 = _epoll_info->epollevent.data.u32;
        events[i].data.u64 = _epoll_info->epollevent.data.u64;
        events[i].data.ptr = _epoll_info->epollevent.data.ptr;
        events[i++].data.fd = _epoll_info->epollevent.data.fd;
    }

    return i;
}

#endif

