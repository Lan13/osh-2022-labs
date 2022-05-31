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

    char *buffer = (char*)malloc(sizeof(char) * MAX_MESSAGE_BUFFER_LEN);
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
                    // recv之前需要 memset buffer
                    memset(buffer, 0, sizeof(char) * MAX_MESSAGE_BUFFER_LEN);
                    ssize_t len = recv(fdi, buffer, MAX_MESSAGE_BUFFER_LEN, 0);
                    if (len <= 0) {
                        printf("user%d left the chatting room!\n", fdi);
                        FD_CLR(fdi, &allusers);     // 离开时清除用户的 fd
                        close(fdi);
                        break;
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

                    // 直接对换行符进行处理，因为如果只有一个\n时，strtok只会返回一个null
                    if (message_cnt == 0) {
                        for (int fdj = 0; fdj <= maxfd; fdj++) {
                            if (fdj != fdi && FD_ISSET(fdj, &allusers)) {  // 只能发送给在线的用户，否则会发送多次
                                // send 函数用来发送数据，将 send_message 数据发送到 fdj
                                send(fdj, "Message:\n", 9, 0);
                            }
                        }
                    }

                    char *send_message = (char*)malloc(sizeof(char) * MAX_MESSAGE_BUFFER_LEN + 9);
                    if (!send_message) {
                        perror("send_message malloc");
                    }
                    // 将 prefix 和 suffix 加到每行消息并且发送
                    for (int i = 0; i < message_cnt; i++) {
                        int base_offset = 8;
                        strcpy(send_message, "Message:");   // 添加 prefix
                        strcat(send_message, buffer_split[i]);  // 添加发送的消息内容
                         // 把原有的"\n"加回去，这里特殊判断比较多的原因是因为strtok()分割的性质
                        if (strlen(buffer_split[i]) < MAX_MESSAGE_BUFFER_LEN && (len != MAX_MESSAGE_BUFFER_LEN || i != message_cnt - 1)) {
                            strcat(send_message, "\n");
                            base_offset = 9;
                        }
                    
                        for (int fdj = 0; fdj <= maxfd; fdj++) {
                            if (fdj != fdi && FD_ISSET(fdj, &allusers)) {  // 只能发送给在线的用户，否则会发送多次
                                ssize_t send_len;
                                size_t send_size = strlen(buffer_split[i]) + base_offset;
                                // send 函数用来发送数据，将 send_message 数据发送到 fdj
                                // 处理一次send发送不完的情况：如果一次send发送不完的话，则继续发送
                                while ((send_len = send(fdj, send_message, send_size, 0)) < send_size) {
                                    printf("send twice!\n");
                                    send_message = send_message + send_len;
                                    send_size = send_size - send_len;
                                }
                            }
                        }
                    }
                    free(send_message);
                    free(buffer_split);
                }
            }
        }
    }
    return 0;
}