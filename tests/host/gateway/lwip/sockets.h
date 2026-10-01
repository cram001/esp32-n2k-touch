#pragma once
#include <cstddef>
#include <cstdint>
// Socket simulation exercises the production client's state machine, not a real network.
struct sockaddr {};
struct in_addr { uint32_t s_addr; };
struct sockaddr_in { int sin_family; uint16_t sin_port; in_addr sin_addr; };
using socklen_t = unsigned;
struct gateway_fd_set { bool set; };
struct gateway_timeval { long tv_sec, tv_usec; };
#define fd_set gateway_fd_set
#define timeval gateway_timeval
#undef FD_ZERO
#undef FD_SET
#define FD_ZERO(p) ((p)->set=false)
#define FD_SET(fd,p) ((void)(fd), (p)->set=true)
constexpr int AF_INET=2, SOCK_STREAM=1, IPPROTO_TCP=6, SOL_SOCKET=1, SO_ERROR=4;
#define socket gateway_socket
#define connect gateway_connect
#define select gateway_select
#define getsockopt gateway_getsockopt
#define recv gateway_recv
#define close gateway_close
#define fcntl gateway_fcntl
int gateway_socket(int,int,int);
int gateway_connect(int,const sockaddr *,size_t);
int gateway_select(int,void *,gateway_fd_set *,gateway_fd_set *,gateway_timeval *);
int gateway_getsockopt(int,int,int,int *,socklen_t *);
int gateway_recv(int,char *,size_t,int);
int gateway_close(int);
int gateway_fcntl(int,int,int);
