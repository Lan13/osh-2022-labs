#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define MAX_MESSAGE_BUFFER_LEN 2048

struct Pipe {
    int fd_send;
    int fd_recv;
};

void *handle_chat(void *data) {
    struct Pipe *pipe = (struct Pipe *)data;
    char buffer[MAX_MESSAGE_BUFFER_LEN];
    ssize_t len;
    while (1) {
        // recv之前需要 memset buffer
        memset(buffer, 0, sizeof(char) * MAX_MESSAGE_BUFFER_LEN);
        len = recv(pipe->fd_send, buffer, MAX_MESSAGE_BUFFER_LEN, 0);
        if (len <= 0) {
            break;
        }

        // 将 buffer 得到的消息用 \n 进行分割
        char **buffer_split = (char **)malloc(sizeof(char*) * MAX_MESSAGE_BUFFER_LEN);
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
            send(pipe->fd_recv, "Message:\n", 9, 0);
        }

        // 将 prefix 和 suffix 加到每行消息并且发送
        char *send_message = (char*)malloc(sizeof(char) * (MAX_MESSAGE_BUFFER_LEN + 9));
        if (!send_message) {
            perror("send_message malloc");
        }
        for (int i = 0; i < message_cnt; i++) {
            int base_offset = 8;

            strcpy(send_message, "Message:");   // 添加 prefix
            strcat(send_message, buffer_split[i]);  // 添加发送的消息内容
            // 把原有的"\n"加回去，这里特殊判断比较多的原因是因为strtok()分割的性质
            if (strlen(buffer_split[i]) < MAX_MESSAGE_BUFFER_LEN && (len != MAX_MESSAGE_BUFFER_LEN || i != message_cnt - 1)) {
                strcat(send_message, "\n");
                base_offset = 9;
            }
            ssize_t send_len;
            while ((send_len = send(pipe->fd_recv, send_message, strlen(buffer_split[i]) + base_offset, 0)) < strlen(buffer_split[i]) + base_offset) {
                printf("send twice!\n");
                send_message = send_message + send_len;
            }
        }
        free(send_message);
        free(buffer_split);
    }
    return NULL;
}

int main(int argc, char **argv) {
    int port = atoi(argv[1]);
    int fd;
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket");
        return 1;
    }
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
    int fd1 = accept(fd, NULL, NULL);
    int fd2 = accept(fd, NULL, NULL);
    if (fd1 == -1 || fd2 == -1) {
        perror("accept");
        return 1;
    }
    pthread_t thread1, thread2;
    struct Pipe pipe1;
    struct Pipe pipe2;
    pipe1.fd_send = fd1;
    pipe1.fd_recv = fd2;
    pipe2.fd_send = fd2;
    pipe2.fd_recv = fd1;
    pthread_create(&thread1, NULL, handle_chat, (void *)&pipe1);
    pthread_create(&thread2, NULL, handle_chat, (void *)&pipe2);
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);
    return 0;
}