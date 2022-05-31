# OSH-2022-Lab3

本次实验我完成的部分为：

- 双人聊天室 2 + 2

  - 实现以换行为分隔符的消息分割

    将每一次 `recv()` 读到的数据 `buffer` 用 `strtok()` 函数以 `"\n"` 进行分割成 `buffer_split`，并且尽可能保证原有的 `"\n"` 保留，即保留原有的读取数据。接着以分割的每一段 `buffer_split[i]` 进行 `send()` 发送，并且添加 `"Message:"` 前缀。从而实现以换行为分隔符的消息分割。

  - 处理 `send()` 无法一次发送所有数据的情况

    当 `send()` 返回值 `send_len` 小于 `send()` 函数的规定发送大小 `send_size` 时，则将发送的消息 `send_message` 进行偏移，并修改发送大小 `send_size` ，然后再次进行 `send()` 操作，直到满足为止，说明 `send()` 操作已经发送完所有数据。

- 基于多线程的多人聊天室 4 + 2

  - 简单的多线程聊天室

  - 细粒度锁

    由于实现了细粒度锁，所以从这里开始介绍。定义了结构有

    ```c
    struct Pipe {
        int fd_send;
        int fd_recv[MAX_USER_LOAD];
        int *online;    // 使用指针来修改在线状态
    };
    
    typedef struct QueueBase {
        char message[MAX_MESSAGE_BUFFER_LEN];   // 消息
        struct Pipe *pipe;      // 将消息与用户信息绑定
    }QueueBase;
    
    typedef struct MessageQueue {
        QueueBase base[MAX_QUEUE_CAPACITY];     // 消息队列的基址，暂时设定大小为 1024
        int front;      // 队首
        int rear;       // 队尾
    }MessageQueue;
    ```

    其中的 `online` 用来确认在线状态（使用状态），然后将用户信息与消息绑定在一起形成一个消息队列。在整个程序中，只有在消息队列的入队操作和出队操作中才会进行互斥锁的加锁与解锁。同时将每一个用户对应于一个线程，在每次有待发送的消息时，将消息入队，同时使用 `pthread_detach(pthread_self())` ，确保资源的使用，从而能够支持用户的随时加入与退出。然后再使用一个线程来处理消息队列内的消息，在 `handle_send()` 中，每当消息队列内有消息时，就会将该消息发送给所有其他在线的用户。

- 基于 IO 复用/异步 IO 的多人聊天室 4

  - SELECT

    将服务端套接字设置为非阻塞，程序不间断地运行 `accept()` 操作和 `select()` 操作，当有新用户加入时，则进行 `FD_SET()` 操作并且更新最大的操作符 `maxfd`，当用户退出时则进行 `FD_CLR()` 操作清除其状态。每当 `select()` 读到信息时，对所有用户状态进行判断，如果当用户已经进入聊天室的话，则对其进行聊天操作，如第一部分的操作。
