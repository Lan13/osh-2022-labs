#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define MAX_MESSAGE_LEN 1048576
#define MAX_MESSAGE_BUFFER_LEN 4096

struct Pipe {
    int fd_send;
    int fd_recv;
};

void *handle_chat(void *data) {
    struct Pipe *pipe = (struct Pipe *)data;
    // buffer 用来接受所有消息，最大不超过 1 MiB
    char *buffer = (char*)malloc(sizeof(char) * MAX_MESSAGE_LEN);

    while (1) { 
        ssize_t len;
        // 用于接受每段消息
        char *recv_buffer = (char*)malloc(sizeof(char) * MAX_MESSAGE_BUFFER_LEN);
        
        // recv 函数用来复制数据，将 fd_send 的内容复制到 buffer 当中
        // len 返回实际数据的字节数
        len = recv(pipe->fd_send, buffer, MAX_MESSAGE_BUFFER_LEN, 0);
        if (len <= 0) {
            break;
        }

        // 如果实际消息太长，超过了 4096 字节，就再次接收新的消息，直到不再超出
        while (len >= MAX_MESSAGE_BUFFER_LEN) {
            // recv 函数用来复制数据，将 fd_send 的内容复制到 buffer 当中
            // len 返回实际数据的字节数
            len = recv(pipe->fd_send, recv_buffer, MAX_MESSAGE_BUFFER_LEN, 0);
            strcat(buffer, recv_buffer);    // 更新我们的消息 buffer
        }

        // 将 buffer 得到的消息用 \n 进行分割
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
            // printf("buffer_split[message_cnt]: %s\n", buffer_split[message_cnt]);
        }

        // printf("message_cnt: %d\n", message_cnt);

        // 将 prefix 和 suffix 加到每行消息并且发送
        for (int i = 0; i < message_cnt; i++) {
            char *send_message = (char*)malloc(sizeof(char) * (strlen(buffer_split[i]) + 36));
            if (!send_message) {
                perror("send_message malloc");
            }
            strcpy(send_message, "Message:");   // 添加 prefix
            strcat(send_message, buffer_split[i]);  // 添加发送的消息内容
            strcat(send_message, "\n");         // 添加 suffix

            // debug
            // printf("loop i:%d send a message: %s", i, send_message);
            // printf("loop i:%d message_cnt: %d\n", i, message_cnt);
            
            // 当前需要发送消息的长度
            int len = strlen(send_message);
            // 如果消息不是太长，可以直接发送
            if (len < MAX_MESSAGE_BUFFER_LEN) {
                // send 函数用来发送数据，将 send_message 数据发送到 fd_recv
                send(pipe->fd_recv, send_message, strlen(send_message), 0);
                free(send_message);
                continue;
            }

            // 如果消息太长的话，不能一次性发送完成，则分割成多段进行发送
            char *send_message_split = send_message;
            while (len >= MAX_MESSAGE_BUFFER_LEN) {
                // send 函数用来发送数据，将 send_message 数据发送到 fd_recv
                send(pipe->fd_recv, send_message_split, MAX_MESSAGE_BUFFER_LEN, 0);
                // 将地址偏移
                send_message_split = send_message_split + MAX_MESSAGE_BUFFER_LEN;
                len = len - MAX_MESSAGE_BUFFER_LEN;
            }
            // 将剩余 len 长度的消息发送完成
            // send 函数用来发送数据，将 send_message 数据发送到 fd_recv
            send(pipe->fd_recv, send_message_split, len, 0);
            free(send_message);
        }
        free(buffer_split);
    }

    free(buffer);
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