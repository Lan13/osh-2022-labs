#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define MAX_MESSAGE_LEN 1048576
#define MAX_MESSAGE_BUFFER_LEN 4096
#define MAX_USER_LOAD 32
#define MAX_QUEUE_CAPACITY 1024

struct Pipe {
    int fd_send;
    int fd_recv[MAX_USER_LOAD];
    int *online;    // 使用指针来修改在线状态
};

typedef struct QueueBase {
    char* message;          // 消息
    struct Pipe *pipe;      // 将消息与用户信息绑定
}QueueBase;

// 考虑将不同用户发送的消息存入到一个消息队列当中，在入队时会有进行加锁操作，
// 以确保消息队列的安全性与正确性，通过此消息队列的方法，就能确保多条消息发送的有序性，
// 就不会出现同时 send 的情况。
// 通过入队和出队时的加锁解锁操作，就能实现细粒度锁的要求
typedef struct MessageQueue {
    QueueBase base[MAX_QUEUE_CAPACITY];     // 消息队列的基址，暂时设定大小为 1024
    int front;      // 队首
    int rear;       // 队尾
}MessageQueue;

int online[MAX_USER_LOAD];      // 记录用户在线状态
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
MessageQueue queue;     // 定义全局的消息队列

int initQueue(MessageQueue *Q) {
    Q->front = Q->rear = 0;
    return 1;
}

int enQueue(MessageQueue *Q, char *message, struct Pipe *pipe) {
    if ((Q->rear + 1) % MAX_QUEUE_CAPACITY == Q->front) {
        return 0;
    }

    pthread_mutex_lock(&mutex);
    Q->base[Q->rear].message = message;
    Q->base[Q->rear].pipe = pipe;
    Q->rear = (Q->rear + 1) % MAX_QUEUE_CAPACITY;
    pthread_mutex_unlock(&mutex);
    return 1;
}

QueueBase* deQueue(MessageQueue *Q) {
    if (Q->rear == Q->front) {
        return NULL;
    }
    pthread_mutex_lock(&mutex);
    QueueBase* ret = &Q->base[Q->front];
    Q->front = (Q->front + 1) % MAX_QUEUE_CAPACITY;
    pthread_mutex_unlock(&mutex);
    return ret;
}

void *handle_chat(void *data) {
    // 将线程和主线程分离开，使得线程运行结束后可以终止自己并释放资源
    // 支持用户的“随时”加入与退出
    pthread_detach(pthread_self());
    struct Pipe *pipe = (struct Pipe *)data;

    // 每个用户登录时的欢迎语，能确认在线状态
    char login[] = "Welcome to chatting room!\n";
    write(pipe->fd_send, login, sizeof(login));

    // debug
    // 在服务器中查看 user 登录登出信息，用于 debug
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
        // debug
        // printf("user%d send a message: %s", pipe->fd_send, buffer);

        // 将需要发送的消息写入到消息队列，在入队操作中有加锁解锁操作
        // 可以防止不同用户同步地写入到消息队列，从而表现成不能同时发送消息
        enQueue(&queue, buffer, pipe);
    }

    free(buffer);

    *pipe->online = 0;  // 线程结束后，将用户在线状态退出，释放出一个新的位置
    // debug
    // 在服务器中查看 user 登录登出信息，用于 debug
    printf("user%d left the chatting room!\n", pipe->fd_send);
    close(pipe->fd_send);   // 线程结束后，将写端关闭

    pthread_exit(NULL);
    return NULL;
}

void *handle_send(void *data) {
    // 从消息队列中获取发送的消息，然后发送给所有
    QueueBase* message_from_queue;
    while(1) {
        if ((message_from_queue = deQueue(&queue)) != NULL) {       // 不断检测是否有消息需要发送
            struct Pipe *pipe = message_from_queue->pipe;
            // 将从 queue 得到的消息用 \n 进行分割
            char **buffer_split = (char **)malloc(sizeof(char*) * 1024);
            if (!buffer_split) {
                perror("buffer_split malloc");
            }

            int message_cnt = 0;    // 记录有多少行消息
            // 对每行消息分别进行分割复制
            buffer_split[message_cnt] = strtok(message_from_queue->message, "\n");
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
                sprintf(send_message, "Message from uesr%d:", pipe->fd_send);
                strcat(send_message, buffer_split[i]);  // 添加发送的消息内容
                strcat(send_message, "\n");         // 添加 suffix
            
                // 当前需要发送消息的长度
                int len = strlen(send_message);
                // 如果消息不是太长，可以直接发送
                if (len < MAX_MESSAGE_BUFFER_LEN) {
                    // 接下来向其他 31 个用户发送消息
                    for (int j = 0; j < MAX_USER_LOAD; j++) {
                        if (pipe->fd_recv[j] != pipe->fd_send && online[j] == 1) {  // 只能发送给在线的用户，否则会发送多次
                            // send 函数用来发送数据，将 send_message 数据发送到 fd_recv
                            send(pipe->fd_recv[j], send_message, strlen(send_message), 0);
                            // debug 测试发送给哪些用户
                            // printf("user%d send messages to %d\n", pipe->fd_send, pipe->fd_recv[j]);
                        }
                    }
                    free(send_message);
                    break;
                }

                // 如果消息太长的话，不能一次性发送完成，则分割成多段进行发送
                char *send_message_split = send_message;
                while (len >= MAX_MESSAGE_BUFFER_LEN) {
                    // 接下来向其他 31 个用户发送分段消息
                    for (int j = 0; j < MAX_USER_LOAD; j++) {
                        if (pipe->fd_recv[j] != pipe->fd_send && online[j] == 1) {  // 只能发送给在线的用户，否则会发送多次
                            // send 函数用来发送数据，将 send_message 数据发送到 fd_recv
                            send(pipe->fd_recv[j], send_message_split, MAX_MESSAGE_BUFFER_LEN, 0);
                        }
                    }
                    // 将地址偏移
                    send_message_split = send_message_split + MAX_MESSAGE_BUFFER_LEN;
                    len = len - MAX_MESSAGE_BUFFER_LEN;
                }

                // 接下来向其他 31 个用户发送剩余消息
                for (int j = 0; j < MAX_USER_LOAD; j++) {
                    if (pipe->fd_recv[j] != pipe->fd_send && online[j] == 1) {  // 只能发送给在线的用户，否则会发送多次
                        // 将剩余 len 长度的消息发送完成
                        // send 函数用来发送数据，将 send_message 数据发送到 fd_recv
                        send(pipe->fd_recv[j], send_message_split, len, 0);
                    }
                }
                free(send_message);
            }
            free(buffer_split);
        }
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

    initQueue(&queue);      // 初始化消息队列

    // 使用一个新的进程来处理消息发送
    pthread_t thread_send;
    pthread_create(&thread_send, NULL, handle_send, NULL);

    int user_fd[MAX_USER_LOAD];
    memset(online, 0, sizeof(int) * MAX_USER_LOAD);
    pthread_t thread[MAX_USER_LOAD];
    struct Pipe pipe[MAX_USER_LOAD];

    int i = 0;
    while(1) {
        while (online[i] == 1)      // 查找空闲区分配
            i = (i + 1) % MAX_USER_LOAD;
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
        pipe[i].online = &online[i];

        // 给新用户添加其他所有用户的接受端信息
        for (int j = 0; j < MAX_USER_LOAD; j++) {
            pipe[i].fd_recv[j] = user_fd[j];
        }

        // 更新所有在线用户接受端信息
        // 因为原来的在线用户接收端信息在初始化添加时并不是目前生成的接受端信息
        // 并且可能是上一次登录的用户已做过更新得到的值，但是现在已经退出，
        // 信息未来得及更新,所以需要更新一次
        for (int j = 0; j < MAX_USER_LOAD; j++) {
            if (online[j] == 1) {
                pipe[j].fd_recv[i] = user_fd[i];
            }
        }

        pthread_create(&thread[i], NULL, handle_chat, (void *)&pipe[i]);
    }
    return 0;
}