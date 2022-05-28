#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <errno.h>

#define MAX_MESSAGE_LEN 1048576
#define MAX_MESSAGE_BUFFER_LEN 4096


int main(int argc, char **argv) {
    int port = atoi(argv[1]);
    int fd;
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket");
        return 1;
    }
    // 将服务端套接字设置成非阻塞
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    socklen_t addr_len = sizeof(addr);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr))) {
        perror("bind");
        return 1;
    }
    if (listen(fd, 2)) {
        perror("listen");
        return 1;
    }

    fd_set allusers;    // all socket that can be scanned
    fd_set users;       // socket after select
    FD_ZERO(&allusers);
    FD_ZERO(&users);
    struct timeval timeout;
    int maxfd = 0;

    // buffer 用来接受所有消息，最大不超过 1 MiB
    char *buffer = (char*)malloc(sizeof(char) * MAX_MESSAGE_LEN);
    while(1) {
        users = allusers;       // current users
        timeout.tv_sec = 0;     // setting time
        timeout.tv_usec = 5000;
        int newfd = accept(fd, NULL, NULL);     // 新用户加入
        // 在非阻塞操作当中，如果连续做读取操作但是却没有数据可读时
        // 会返回一个 EAGAIN 错误信号，在这里我们需要处理一下
        if (newfd == -1 && errno != EAGAIN) {
            perror("newfd accept");
            return 1;
        }
        if (newfd > 0) {
            FD_SET(newfd, &users);
            FD_SET(newfd, &allusers);
            if (maxfd < newfd) {        // 更新最大的 fd 值
                maxfd = newfd;
            }
            // debug
            char login[] = "Welcome to chatting room!\n";
            write(newfd, login, sizeof(login));
            printf("user%d entered the chatting room!\n", newfd);
        }
        

        int readcnt;
        if (readcnt = select(maxfd + 1, &users, NULL, NULL, &timeout) > 0) {
            int cnt = 0;
            for (int fdi = 0; fdi <= maxfd && cnt < readcnt; fdi++) {
                if (FD_ISSET(fdi, &users)) {
                    ssize_t len;
                    // 用于接受每段消息
                    char *recv_buffer = (char*)malloc(sizeof(char) * MAX_MESSAGE_BUFFER_LEN);
                    
                    // recv 函数用来复制数据，将 fd_send 的内容复制到 buffer 当中
                    // len 返回实际数据的字节数
                    len = recv(fdi, buffer, MAX_MESSAGE_BUFFER_LEN, 0);
                    if (len <= 0) {
                        printf("user%d left the chatting room!\n", fdi);
                        FD_CLR(fdi, &allusers);     // 离开时清除用户的 fd
                        close(fdi);
                        break;
                    }

                    // 如果实际消息太长，超过了 4096 字节，就再次接收新的消息，直到不再超出
                    while (len >= MAX_MESSAGE_BUFFER_LEN) {
                        // recv 函数用来复制数据，将 fd_send 的内容复制到 buffer 当中
                        // len 返回实际数据的字节数
                        len = recv(fdi, recv_buffer, MAX_MESSAGE_BUFFER_LEN, 0);
                        strcat(buffer, recv_buffer);    // 更新我们的消息 buffer
                    }

                    cnt++;
                    // debug
                    // printf("user%d send a message: %s", fdi, buffer);

                    char **buffer_split = (char **)malloc(sizeof(char*) * 1024);
                    if (!buffer_split) {
                        perror("buffer_split malloc");
                    }
                    int message_cnt = 0;    // 记录有多少行消息
                    // 对每行消息分别进行分割复制
                    buffer_split[message_cnt] = strtok(buffer, "\n");
                    while (buffer_split[message_cnt]) {
                        message_cnt++;
                        buffer_split[message_cnt] = strtok(NULL, "\n");
                    }

                    // 将 prefix 和 suffix 加到每行消息并且发送
                    for (int i = 0; i < message_cnt; i++) {
                        char *send_message = (char*)malloc(sizeof(char) * (strlen(buffer_split[i]) + 80));
                        if (!send_message) {
                            perror("send_message malloc");
                        }
                        // 添加 prefix 并且添加发送者信息
                        sprintf(send_message, "Message from uesr%d:", fdi);
                        strcat(send_message, buffer_split[i]);  // 添加发送的消息内容
                        strcat(send_message, "\n");         // 添加 suffix
                    
                        // 当前需要发送消息的长度
                        int len = strlen(send_message);
                        // 如果消息不是太长，可以直接发送
                        if (len < MAX_MESSAGE_BUFFER_LEN) {
                            // 接下来向其他 31 个用户发送消息
                            for (int fdj = 0; fdj <= maxfd; fdj++) {
                                if (fdj != fdi && FD_ISSET(fdj, &allusers)) {  // 只能发送给在线的用户，否则会发送多次
                                    // send 函数用来发送数据，将 send_message 数据发送到 fdj
                                    send(fdj, send_message, strlen(send_message), 0);
                                    // debug 测试发送给哪些用户
                                    // printf("user%d send messages to %d\n", fdi, fdj);
                                }
                            }
                            free(send_message);
                            break;
                        }
                        // 如果消息太长的话，不能一次性发送完成，则分割成多段进行发送
                        char *send_message_split = send_message;
                        while (len >= MAX_MESSAGE_BUFFER_LEN) {
                            // 接下来向其他 31 个用户发送分段消息
                            for (int fdj = 0; fdj <= maxfd; fdj++) {
                                if (fdj != fdi && FD_ISSET(fdj, &allusers)) {  // 只能发送给在线的用户，否则会发送多次
                                    // send 函数用来发送数据，将 send_message 数据发送到 fdj
                                    send(fdj, send_message_split, MAX_MESSAGE_BUFFER_LEN, 0);
                                }
                            }
                            // 将地址偏移
                            send_message_split = send_message_split + MAX_MESSAGE_BUFFER_LEN;
                            len = len - MAX_MESSAGE_BUFFER_LEN;
                        }

                        // 接下来向其他 31 个用户发送剩余消息
                        for (int fdj = 0; fdj <= maxfd; fdj++) {
                            if (fdj != fdi && FD_ISSET(fdj, &allusers)) {  // 只能发送给在线的用户，否则会发送多次
                                // 将剩余 len 长度的消息发送完成
                                // send 函数用来发送数据，将 send_message 数据发送到 fdj
                                send(fdj, send_message_split, len, 0);
                            }
                        }
                        free(send_message);
                    }
                    free(buffer_split);
                }
            }
        }
    }
    return 0;
}