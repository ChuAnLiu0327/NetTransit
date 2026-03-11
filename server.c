#include <stdio.h>      // 标准输入输出：printf, perror
#include <stdlib.h>     // 标准库：exit, malloc, free
#include <string.h>     // 字符串处理：memset, strlen
#include <unistd.h>     // UNIX 标准函数：close, read, write
#include <arpa/inet.h>  // 网络地址转换：inet_ntop, inet_addr
#include <sys/socket.h> // socket 相关：socket, bind, listen, accept
#include <netinet/in.h> // 互联网地址结构：sockaddr_in
#include <pthread.h>    // 多线程：pthread_create, pthread_detach
#include <cjson/cJSON.h>          // 使用 cJSON 库处理 JSON
#include <uthash.h>             // 用于创建哈希表
#include "db_ops.h"

/* ========== 配置常量 ========== */
#define IP          "127.0.0.1"   // 服务器监听的 IP 地址（可改为 0.0.0.0 监听所有接口）
#define SERVER_PORT 9999         // 服务器监听的端口号
#define BUFFER_SIZE 1024           // 接收数据缓冲区大小
#define BACKLOG     1024           // listen 函数允许的最大等待连接数

#define ACCOUNT_SIZE 64          // 账户 JSON 数据最大长度（包括结尾 '\0'）

/* ========== 客户端信息结构体 ========== */
/**
 * 当 accept 接受一个新客户端连接后，将客户端相关信息保存在此结构体中，
 * 然后传递给线程处理函数，以便在线程中知道是哪个客户端发来的数据。
 */
typedef struct {
    int client_fd;                     // 客户端 socket 文件描述符
    struct sockaddr_in client_addr;    // 客户端的地址结构（包含 IP 和端口）
    char client_ip[INET_ADDRSTRLEN];   // 客户端的 IP 地址字符串形式
    int client_port;                   // 客户端的端口号（主机字节序）
} client_info_t;

// 定义在线用户结构体
typedef struct online_user{
    char account[32];      // 键:账户名称(例子:号码)
    int sockfd;             // 值: 对应的socket文件描述符
    UT_hash_handle hh;      // 句柄: 让这个结构体可哈希
} online_user_t;
// 定义哈希表头指针，初始化为 NULL
online_user_t *online_users = NULL;

// 为了防止多个线程同时修改哈希表,使用互斥锁
pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;  // 初始化互斥锁

// 对于本地数据库的操作也是如此
pthread_mutex_t cahtMessageDB_mutex = PTHREAD_MUTEX_INITIALIZER;  // 初始化数据库读写的互斥锁


// 插入或者更新用户
void add_user(const char* account,int sockfd){
    pthread_mutex_lock(&users_mutex);   // 加锁
    online_user_t *user;
    // 先检查这个账户是否存在
    HASH_FIND_STR(online_users,account,user);
    if(user){
        // 如果存在,更新新的连接
        printf("INFO::Account exists, update socket connection\n");
        user->sockfd = sockfd;
    }else{
        // 用户并不存在,创建节点并插入
        printf("INFO::The account does not exist. Create a node and insert the client information.\n");
        user = (online_user_t*)malloc(sizeof(online_user_t));
        strcpy(user->account,account);
        user->sockfd = sockfd;
        // 使用 HASH_ADD_STR 宏，参数：表头指针, 键字段名, 要添加的节点指针
        HASH_ADD_STR(online_users,account,user);
    }
    pthread_mutex_unlock(&users_mutex); // 解锁
}

// 根据账户查找用户，返回找到的结构体指针，没找到则返回 NULL
online_user_t* find_user(const char* account){
    online_user_t* user = NULL;
    HASH_FIND_STR(online_users,account,user);
    return user;
}

// 查找对应账户客户端的文件描述符
int find_user_sockfd(const char* account) {
    pthread_mutex_lock(&users_mutex);
    online_user_t* user = NULL;
    HASH_FIND_STR(online_users, account, user);
    int sockfd = user ? user->sockfd : -1;
    pthread_mutex_unlock(&users_mutex);
    return sockfd;
}

// 根据账户删除用户
int delet_user(const char* account) {
    pthread_mutex_lock(&users_mutex);
    online_user_t* user = NULL;
    HASH_FIND_STR(online_users, account, user);
    if (user) {
        HASH_DEL(online_users, user);
        // 注意：这里没有关闭 socket，因为调用 delet_user 的地方应该已经关闭了 socket
        free(user);
        pthread_mutex_unlock(&users_mutex);
        return 1;
    }
    pthread_mutex_unlock(&users_mutex);
    printf("INFO::The deleted user account does not exist.\n");
    return -1;
}

// 通过文件描述符删除用户
void remove_user_by_sockfd(int sockfd) {
    pthread_mutex_lock(&users_mutex);
    online_user_t *user, *tmp;
    HASH_ITER(hh, online_users, user, tmp) {
        if (user->sockfd == sockfd) {
            HASH_DEL(online_users, user);
            printf("INFO::User %s has been removed from the online list.\n", user->account);
            free(user);
            break;
        }
    }
    pthread_mutex_unlock(&users_mutex);
}

/* ========== 线程处理函数 ========== */
/**
 * 每个客户端连接创建的新线程将运行此函数。
 * 参数：指向 client_info_t 结构体的指针
 * 返回值：NULL
 * 
 * 该函数目前只接收客户端发来的数据并打印到控制台。
 * 若要实现转发，需要在此函数中连接目标服务器，
 * 并同时监听客户端和目标服务器两个 socket，双向传递数据。
 */
void *handle_client(void *arg) {

    // 初始化数据库
    sqlite3* chatMessageDB = sqliteInit_chatMessageDB();

    // 将 void 指针转换为实际的 client_info_t 指针
    client_info_t *info = (client_info_t *)arg;
    int client_fd = info->client_fd;           // 客户端 socket
    char buffer[BUFFER_SIZE];                  // 接收缓冲区
    ssize_t bytes;                              // 接收到的字节数

    // 打印客户端连接信息
    printf("INFO::Client [%s:%d] has connected.\n", info->client_ip, info->client_port);

    bytes = recv(client_fd, buffer, ACCOUNT_SIZE - 1, 0);
    if (bytes <= 0) {
        if (bytes == 0) {
            printf("INFO::Client [%s:%d] closed the connection before sending the account information.\n", info->client_ip, info->client_port);
        } else {
            printf("INFO::Account information error\n");
        }
        close(client_fd);
        free(info);
        return NULL;
    }
    buffer[bytes] = '\0';  // 确保字符串以 '\0' 结尾

    /* ----- 2. 解析 JSON ----- */
    cJSON *root = cJSON_Parse(buffer);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "INFO::JSON parsing error: %s\n", error_ptr);
        } else {
            fprintf(stderr, "INFO::JSON parsing error: Unknown error\n");
        }
        close(client_fd);
        free(info);
        return NULL;
    }

    // 获取 account 字段
    cJSON *account = cJSON_GetObjectItem(root, "account");
    if (account == NULL || !cJSON_IsString(account)) {
        fprintf(stderr, "INFO::The JSON file is missing the 'account' field or there is a type error.\n");
        cJSON_Delete(root);
        close(client_fd);
        free(info);
        return NULL;
    }

    const char *account_str = account->valuestring;
    printf("INFO::Client [%s:%d] Account: %s\n", info->client_ip, info->client_port, account_str);

    // 解析得到account的时候就要将其添加到哈希表
    // 将用户添加到哈希表中
    add_user(account_str,client_fd);

    // 清理 JSON 对象
    cJSON_Delete(root);

    // 循环接收客户端发来的数据
    // recv 会阻塞，直到收到数据或连接断开

    while ((bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        // 确保缓冲区以 '\0' 结尾，方便当作字符串处理
        buffer[bytes] = '\0';

        root = cJSON_Parse(buffer);
        if(!root) continue;

        cJSON *to = cJSON_GetObjectItem(root, "to");
        cJSON *msg = cJSON_GetObjectItem(root, "msg");
        cJSON *from = cJSON_GetObjectItem(root, "from");

        if (!cJSON_IsString(to) || !cJSON_IsString(msg)) {
            const char *err = "Format error. Need 'to' and 'msg'.";
            send(client_fd, err, strlen(err), 0);
            cJSON_Delete(root);
            continue;
        }

        const char *target = to->valuestring;
        const char *text = msg->valuestring;
        const char *sender = cJSON_IsString(from) ? from->valuestring : account_str;

        int target_sock = find_user_sockfd(target);

        if (target_sock != -1) {
            char out[1024];
            snprintf(out, sizeof(out), "[%s]: %s", sender, text);
            send(target_sock, out, strlen(out), 0);
        } else {
            char err[256];
            snprintf(err, sizeof(err), "INFO::User %s is not online.", target);
            send(client_fd, err, strlen(err), 0);
        }
        // 保存到数据库中
        // 先上锁
        pthread_mutex_lock(&cahtMessageDB_mutex);
        insert_messagee(chatMessageDB,sender,target,text);
        pthread_mutex_unlock(&cahtMessageDB_mutex);
        cJSON_Delete(root);
    }

    // 退出循环说明 recv 返回 <=0
    if (bytes == 0) {
        // recv 返回 0 表示客户端正常关闭连接
        printf("INFO::Client [%s:%d] has disconnected.\n", info->client_ip, info->client_port);
    } else {
        // recv 返回 -1 表示出错
        perror("recv error");
    }

    // 从在线哈希表中移除该用户
    remove_user_by_sockfd(client_fd);
    // 关闭客户端 socket
    close(client_fd);
    // 释放之前 malloc 分配的内存
    free(info);
    return NULL;
}

/* ========== 主函数 ========== */
int main() {

    int server_fd;                      // 服务器 socket 文件描述符
    struct sockaddr_in server_addr;     // 服务器地址结构

    /* ----- 1. 创建 socket ----- */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket 创建失败");
        exit(EXIT_FAILURE);
    }

    /* ----- 2. 设置 socket 选项（可选，但推荐） ----- */
    // SO_REUSEADDR 允许重用本地地址，防止服务器重启时“Address already in use”错误
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt 设置失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* ----- 3. 绑定地址和端口 ----- */
    // 清空服务器地址结构
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;                 // IPv4
    server_addr.sin_addr.s_addr = inet_addr(IP);      // 监听 IP
    server_addr.sin_port = htons(SERVER_PORT);        // 监听端口（转换为网络字节序）

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind 绑定失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* ----- 4. 开始监听 ----- */
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen 监听失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("服务器启动成功，监听 %s:%d\n", IP, SERVER_PORT);
    printf("等待客户端连接...\n");

    /* ----- 5. 主循环：不断接受客户端连接 ----- */
    while (1) {
        struct sockaddr_in client_addr;   // 客户端的地址结构
        socklen_t client_len = sizeof(client_addr); // 地址结构长度（值-结果参数）

        // accept 阻塞，直到有客户端连接到达
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept 接受连接失败");
            continue;   // 继续等待下一个连接
        }

        /* ----- 为客户端分配内存保存信息 ----- */
        client_info_t *info = (client_info_t *)malloc(sizeof(client_info_t));
        if (info == NULL) {
            perror("malloc 分配内存失败");
            close(client_fd);   // 关闭此客户端 socket
            continue;
        }

        // 保存客户端 socket 和地址结构
        info->client_fd = client_fd;
        info->client_addr = client_addr;

        // 将客户端 IP 从网络字节序二进制转换为点分十进制字符串
        inet_ntop(AF_INET, &client_addr.sin_addr, info->client_ip, INET_ADDRSTRLEN);
        // 获取客户端端口号（从网络字节序转换为主机字节序）
        info->client_port = ntohs(client_addr.sin_port);

        /* ----- 创建新线程处理该客户端 ----- */
        pthread_t tid;   // 线程 ID
        // 创建线程，线程函数为 handle_client，参数为 info
        if (pthread_create(&tid, NULL, handle_client, info) != 0) {
            perror("pthread_create 创建线程失败");
            // 清理资源
            free(info);
            close(client_fd);
            continue;
        }

        // 将线程设置为分离状态，这样线程结束时自动回收资源，无需 pthread_join
        pthread_detach(tid);
    }

    /* ----- 服务器关闭（实际上上面的无限循环不会执行到这里）----- */
    close(server_fd);
    return 0;
}