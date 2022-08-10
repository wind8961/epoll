/*@file psn-epoll.h
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
#pragma once
#ifdef _WIN32
#include <winsock2.h>
#include <stdint.h>

#define AFD_POLL_RECEIVE           1
#define AFD_POLL_RECEIVE_EXPEDITED 2
#define AFD_POLL_SEND              4
#define AFD_POLL_DISCONNECT        8
#define AFD_POLL_ABORT             16
#define AFD_POLL_LOCAL_CLOSE       32
#define AFD_POLL_ACCEPT            128
#define AFD_POLL_CONNECT_FAIL      256

#define EPOLLIN      AFD_POLL_RECEIVE
#define EPOLLPRI     AFD_POLL_RECEIVE_EXPEDITED
#define EPOLLOUT     AFD_POLL_SEND
#define EPOLLERR     (AFD_POLL_ABORT | AFD_POLL_CONNECT_FAIL)
#define EPOLLHUP     (AFD_POLL_DISCONNECT | AFD_POLL_LOCAL_CLOSE)
#define EPOLLRDNORM  AFD_POLL_ACCEPT
#define EPOLLRDBAND  2048
#define EPOLLWRNORM  (AFD_POLL_SEND | AFD_POLL_RECEIVE)
#define EPOLLWRBAND  4096
#define EPOLLRDHUP   (AFD_POLL_RECEIVE | AFD_POLL_DISCONNECT | AFD_POLL_LOCAL_CLOSE)
#define EPOLLONESHOT 512
#define EPOLLET 1024

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_MOD 2
#define EPOLL_CTL_DEL 3

#define socket_t SOCKET

typedef union epoll_data {
	void* ptr;
	int      fd;
	uint32_t u32;
	uint64_t u64;
} epoll_data_t;

struct epoll_event {
	uint32_t     events;    /* Epoll events */
	epoll_data_t data;      /* User data variable */
};

int epoll_create(int size);
int epoll_create1(int flags); 
int epoll_ctl(int epfd, int op, int fd, struct epoll_event* event);
int epoll_wait(int epfd, struct epoll_event* events,
	int maxevents, int timeout);
/*epoll cleanup*/
void close(int epfd);
#else
#define socket_t int
#endif

/*portable helper functions*/
int epoll_sock2fd(socket_t s);
socket_t epoll_fd2sock(int fd);
void epoll_postqueued(int epfd);
