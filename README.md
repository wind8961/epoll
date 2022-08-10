# EPOLL
 EPOLL for Windows with DeviceIoControl and GetQueuedCompletionStatusEx api in C++, it closely resembles Linux EPOLL so only minor touches is needed to port your Linux code for Windows, epoll_wait is thread safe so you can call it in multiple threads at the same time as worker threads.
 
# Remarks
Linux fd is int type so to get an int fd value from socket use the portable function epoll_sock2fd and to get the socket from fd use epoll_fd2sock.
Requires Windows Vista and up (GetQueuedCompletionStatusEx).
Edge trigger socket notification is not supported.
