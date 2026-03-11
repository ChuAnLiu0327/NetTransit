#ifndef __SERVER_H
#define __SERVER_H
#include <stdio.h>      // 标准输入输出函数，如printf, perror
#include <stdlib.h>     // 标准库函数，如exit, malloc
#include <string.h>     // 字符串处理函数，如strlen, memset, strcmp
#include <unistd.h>     // UNIX标准函数，如close, read, write
#include <arpa/inet.h>  // 网络地址转换函数，如inet_ntop, inet_pton
#include <sys/socket.h> // socket相关函数和数据结构
#include <netinet/in.h> // 互联网地址族定义，如sockaddr_in
#include <errno.h>      // 错误号定义，用于错误处理
#include <pthread.h>    // 多线程
#include <sys/epoll.h>  // epoll 头文件

/*====定义常量====*/
#define IP                  "127.0.0.1" // 监听的服务器IP
#define SERVER_PORT         9999       // 服务器监听的端口号
#define BUFFER_SIZE         1024       // 接收缓冲区大小
#define BACKLOG             1024         // 连接请求队列的最大长度

#define MAX_EVENTS          1024     // epoll 最大事件数
#define THREAD_POOL_SIZE    4        // 线程池大小（根据CPU核心数调整）



int start_server();         // 开启服务器

#endif