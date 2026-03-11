#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <cjson/cJSON.h>

#define DEFAULT_IP   "127.0.0.1"
#define DEFAULT_PORT 9999
#define BUFFER_SIZE  1024
#define ACCOUNT_SIZE 64

// 发送方和接收方账号宏定义（可以分别运行两个客户端，修改这两个宏即可）
#define SENDER_ACCOUNT   "34511"
#define RECEIVER_ACCOUNT "123"

int sock_fd;  // 全局 socket，方便线程使用
int running = 1;  // 控制线程退出

// 接收线程函数：不断接收服务器发来的消息并打印
void *receive_thread(void *arg) {
    char recv_buf[BUFFER_SIZE];
    ssize_t bytes;

    while (running) {
        bytes = recv(sock_fd, recv_buf, sizeof(recv_buf) - 1, 0);
        if (bytes > 0) {
            recv_buf[bytes] = '\0';
            // 打印接收到的消息（可以解析 JSON，这里直接打印）
            printf("\n[收到] %s\n", recv_buf);
            printf("> ");  // 恢复输入提示符
            fflush(stdout);
        } else if (bytes == 0) {
            printf("服务器关闭了连接\n");
            running = 0;
            break;
        } else {
            perror("recv 错误");
            running = 0;
            break;
        }
    }
    return NULL;
}

int connect_to_server(const char *ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket 创建失败");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        perror("IP 地址转换失败");
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("连接服务器失败");
        close(sock);
        return -1;
    }

    printf("已连接到 %s:%d\n", ip, port);
    return sock;
}

int main(int argc, char *argv[]) {
    const char *server_ip = DEFAULT_IP;
    int port = DEFAULT_PORT;

    sock_fd = connect_to_server(server_ip, port);
    if (sock_fd < 0) {
        return 1;
    }

    // ----- 发送登录信息（使用宏定义的发送方账号）-----
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        fprintf(stderr, "创建 JSON 对象失败\n");
        close(sock_fd);
        return 1;
    }
    if (!cJSON_AddStringToObject(root, "account", SENDER_ACCOUNT)) {
        fprintf(stderr, "添加字段失败\n");
        cJSON_Delete(root);
        close(sock_fd);
        return 1;
    }
    char *login_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!login_json) {
        fprintf(stderr, "序列化 JSON 失败\n");
        close(sock_fd);
        return 1;
    }

    size_t len = strlen(login_json);
    if (send(sock_fd, login_json, len, 0) != (ssize_t)len) {
        perror("发送账户信息失败");
        free(login_json);
        close(sock_fd);
        return 1;
    }
    printf("登录账户: %s\n", login_json);
    free(login_json);

    // 创建接收线程
    pthread_t tid;
    if (pthread_create(&tid, NULL, receive_thread, NULL) != 0) {
        perror("创建接收线程失败");
        close(sock_fd);
        return 1;
    }
    pthread_detach(tid);  // 分离线程，自动回收资源

    // 主线程负责发送用户输入的消息
    char input[BUFFER_SIZE];
    while (running) {
        printf("> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;  // EOF
        }
        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "/quit") == 0) {  // 输入 /quit 退出
            running = 0;
            break;
        }

        if (input[0] == '\0') {
            continue;  // 忽略空行
        }

        // 构造消息 JSON：包含 from, to, msg
        cJSON *msg_root = cJSON_CreateObject();
        if (!msg_root) {
            fprintf(stderr, "创建消息 JSON 失败\n");
            break;
        }
        cJSON_AddStringToObject(msg_root, "from", SENDER_ACCOUNT);
        cJSON_AddStringToObject(msg_root, "to", RECEIVER_ACCOUNT);
        cJSON_AddStringToObject(msg_root, "msg", input);

        char *msg_json = cJSON_PrintUnformatted(msg_root);
        cJSON_Delete(msg_root);
        if (!msg_json) {
            fprintf(stderr, "序列化消息失败\n");
            break;
        }

        size_t msg_len = strlen(msg_json);
        if (send(sock_fd, msg_json, msg_len, 0) != (ssize_t)msg_len) {
            perror("发送失败");
            free(msg_json);
            break;
        }
        printf("已发送: %s\n", msg_json);
        free(msg_json);
    }

    running = 0;          // 通知接收线程退出
    close(sock_fd);       // 关闭 socket 会使接收线程的 recv 返回 0
    printf("程序退出\n");
    return 0;
}