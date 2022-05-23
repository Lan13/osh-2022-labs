#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define MAX_MESSAGE_LEN 1048576
#define MAX_MESSAGE_BUFFER_LEN 4096

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct Pipe {
    int fd_send;
    int fd_recv[32];
};

void *handle_chat(void *data) {

    pthread_detach(pthread_self());

    struct Pipe *pipe = (struct Pipe *)data;

    char login[] = "Welcome to chatting room!\n";
    write(pipe->fd_send, login, sizeof(login));
    printf("user%d entered the chatting room!\n", pipe->fd_send);

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
        printf("user%d send a message: %s", pipe->fd_send, buffer);

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
        }

        // 
        pthread_mutex_lock(&mutex);
        // 将 prefix 和 suffix 加到每行消息并且发送
        for (int i = 0; i < message_cnt; i++) {
            char *send_message = (char*)malloc(sizeof(char) * (strlen(buffer_split[i]) + 64));
            if (!send_message) {
                perror("send_message malloc");
            }
            sprintf(send_message, "Message from %d:", pipe->fd_send);   // 添加 prefix
            strcat(send_message, buffer_split[i]);  // 添加发送的消息内容
            strcat(send_message, "\n");         // 添加 suffix
            
            // 当前需要发送消息的长度
            int len = strlen(send_message);
            // 如果消息不是太长，可以直接发送
            if (len < MAX_MESSAGE_BUFFER_LEN) {
                // 接下来向其他 31 个用户发送消息
                for (int j = 0; j < 32; j++) {
                    if (pipe->fd_recv[j] != pipe->fd_send) {
                        // send 函数用来发送数据，将 send_message 数据发送到 fd_recv
                        send(pipe->fd_recv[j], send_message, strlen(send_message), 0);
                    }
                }
                free(send_message);
                break;
            }

            // 如果消息太长的话，不能一次性发送完成，则分割成多段进行发送
            char *send_message_split = send_message;
            while (len >= MAX_MESSAGE_BUFFER_LEN) {
                // 接下来向其他 31 个用户发送分段消息
                for (int j = 0; j < 32; j++) {
                    if (pipe->fd_recv[j] != pipe->fd_send) {
                        // send 函数用来发送数据，将 send_message 数据发送到 fd_recv
                        send(pipe->fd_recv[j], send_message_split, MAX_MESSAGE_BUFFER_LEN, 0);
                    }
                }
                // 将地址偏移
                send_message_split = send_message_split + MAX_MESSAGE_BUFFER_LEN;
                len = len - MAX_MESSAGE_BUFFER_LEN;
            }

            // 接下来向其他 31 个用户发送剩余消息
            for (int j = 0; j < 32; j++) {
                if (pipe->fd_recv[j] != pipe->fd_send) {
                    // 将剩余 len 长度的消息发送完成
                    // send 函数用来发送数据，将 send_message 数据发送到 fd_recv
                    send(pipe->fd_recv[j], send_message_split, len, 0);
                }
            }
            free(send_message);
        }

        // 
        pthread_mutex_unlock(&mutex);
        free(buffer_split);
    }

    free(buffer);

    printf("user%d left the chatting room!\n", pipe->fd_send);
    close(pipe->fd_send);
    pthread_exit(NULL);
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

    int user_fd[32];
    int online[32];
    memset(online, 0, sizeof(int) * 32);
    pthread_t thread[32];
    struct Pipe pipe[32];

    int i = 0;
    while(1) {
        while (online[i] == 1)
            i = (i + 1) % 32;
        user_fd[i] = accept(fd, NULL, NULL);
        if (user_fd[i] == -1) {
            perror("user_fd accept");
            return 1;
        }
        pipe[i].fd_send = user_fd[i];
        // debug
        // printf("current pipe[%d].fd_send = %d\n", i, pipe[i].fd_send);
        online[i] = 1;
        // debug
        // printf("current online[%d] = %d\n", i, online[i]);

        for (int j = 0; j < 32; j++) {
            pipe[i].fd_recv[j] = user_fd[j];
        }
        for (int j = 0; j < 32; j++) {
            if (online[j] == 1) {
                pipe[j].fd_recv[i] = user_fd[i];
            }
        }
        pthread_create(&thread[i], NULL, handle_chat, (void *)&pipe[i]);
    }
    return 0;
}