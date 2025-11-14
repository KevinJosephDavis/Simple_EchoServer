#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <system_error>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>  // 用于设置非阻塞 IO

// 常量定义（现代 C++ 优先用 constexpr）
constexpr int PORT = 8080;
constexpr int BUFFER_SIZE = 4096;
constexpr int MAX_EVENTS = 1024;  // epoll 最大监听事件数（可调整）
constexpr int EPOLL_TIMEOUT = -1; // epoll_wait 阻塞时间（-1 表示无限阻塞，直到有事件）

// 客户端数据结构（复用你原有的逻辑）
struct ClientData {
    int client_fd;                // 客户端 socket FD
    std::string client_ip;        // 客户端 IP
    uint16_t client_port;         // 客户端端口
    std::string read_buffer;      // 读取缓冲区（存储客户端数据）
    std::string write_buffer;     // 写入缓冲区（待发送给客户端的数据）
};

// 打印客户端信息（复用你原有的逻辑）
void print_client_info(const ClientData* data, const std::string& title) {
    std::cout << "[" << title << "] "
              << "IP: " << data->client_ip
              << ", Port: " << data->client_port
              << ", FD: " << data->client_fd << std::endl;
}

// 设置文件描述符为非阻塞模式（epoll ET 模式必须配合非阻塞 IO）
void set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);  // 获取当前 FD 的状态标志
    if (flags == -1) {
        throw std::system_error(errno, std::generic_category(), "fcntl 获取状态失败");
    }
    // 添加 O_NONBLOCK 标志（非阻塞）
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::system_error(errno, std::generic_category(), "fcntl 设置非阻塞失败");
    }
}

// 向 epoll 实例注册事件（添加/修改 FD 和监听事件）
void epoll_add_or_modify(int epoll_fd, int fd, uint32_t events, ClientData* client_data) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;          // 要监听的事件（比如 EPOLLIN 读事件）
    ev.data.ptr = client_data;   // 绑定客户端数据（事件触发时可直接获取）

    // 先尝试修改事件（如果 FD 已注册），失败则添加（FD 未注册）
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        if (errno == ENOENT) {  // ENOENT 表示 FD 未注册，执行添加
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
                throw std::system_error(errno, std::generic_category(), "epoll_ctl 添加 FD 失败");
            }
        } else {
            throw std::system_error(errno, std::generic_category(), "epoll_ctl 修改 FD 失败");
        }
    }
}

// 从 epoll 实例中删除 FD（客户端断开时调用）
void epoll_remove(int epoll_fd, int fd) {
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        std::cerr << "epoll_ctl 删除 FD 失败：" << std::strerror(errno) << std::endl;
    }
    close(fd);  // 关闭客户端 FD
}

// 处理新客户端连接（epoll 监听到服务器 FD 的读事件时调用）
void handle_new_connection(int server_fd, int epoll_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // 接受新连接（非阻塞模式，即使没连接也不会阻塞）
    int client_fd = accept4(server_fd, (struct sockaddr*)&client_addr, &client_addr_len, SOCK_NONBLOCK);
    if (client_fd == -1) {
        std::cerr << "accept 新连接失败：" << std::strerror(errno) << std::endl;
        return;
    }

    // 初始化客户端数据（用 unique_ptr 管理，自动释放内存）
    auto client_data = std::make_unique<ClientData>();
    client_data->client_fd = client_fd;
    client_data->client_ip = inet_ntoa(client_addr.sin_addr);  // 转换 IP 为字符串
    client_data->client_port = ntohs(client_addr.sin_port);    // 转换端口为本地字节序

    print_client_info(client_data.get(), "新客户端连接");

    // 向 epoll 注册客户端 FD 的读事件（ET 模式：EPOLLIN | EPOLLET）
    epoll_add_or_modify(epoll_fd, client_fd, EPOLLIN | EPOLLET, client_data.release());
    // 注意：release() 转移 unique_ptr 的所有权，epoll 事件的 data.ptr 持有裸指针，后续在客户端断开时手动释放
}

// 处理客户端读事件（客户端发数据过来）
void handle_read_event(ClientData* client_data, int epoll_fd) {
    char raw_buf[BUFFER_SIZE];
    ssize_t read_bytes;

    // 循环读取（ET 模式必须一次性读完所有数据，否则不会再次触发读事件）
    while (true) {
        memset(raw_buf, 0, sizeof(raw_buf));
        // 非阻塞 read：数据没读完会返回 EAGAIN/EWOULDBLOCK，退出循环
        read_bytes = read(client_data->client_fd, raw_buf, BUFFER_SIZE - 1);

        if (read_bytes > 0) {
            // 读取成功，将数据存入客户端的 read_buffer（按长度拷贝，无需手动加 \0）
            client_data->read_buffer.assign(raw_buf, read_bytes);
            std::cout << "收到客户端[" << client_data->client_ip << ":" << client_data->client_port
                      << "] 数据：" << client_data->read_buffer << std::endl;

            // 回声逻辑：将收到的数据存入 write_buffer，准备发送（触发写事件）
            client_data->write_buffer = client_data->read_buffer;
            // 注册写事件（ET 模式），后续 epoll 会触发写事件，执行发送
            epoll_add_or_modify(epoll_fd, client_data->client_fd, EPOLLIN | EPOLLOUT | EPOLLET, client_data);

        } else if (read_bytes == 0) {
            // read_bytes == 0 表示客户端正常断开连接
            print_client_info(client_data, "客户端断开连接");
            epoll_remove(epoll_fd, client_data->client_fd);
            delete client_data;  // 释放客户端数据内存
            return;

        } else {
            // read_bytes < 0 表示读取失败
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // EAGAIN/EWOULDBLOCK：非阻塞模式下数据已读完，退出循环
                break;
            } else {
                // 其他错误（比如网络异常），关闭连接
                std::cerr << "读取客户端数据失败：" << std::strerror(errno) << std::endl;
                epoll_remove(epoll_fd, client_data->client_fd);
                delete client_data;
                return;
            }
        }
    }
}

// 处理客户端写事件（向客户端发送数据）
void handle_write_event(ClientData* client_data, int epoll_fd) {
    const char* data = client_data->write_buffer.c_str();
    size_t data_len = client_data->write_buffer.size();
    ssize_t write_bytes;
    size_t total_written = 0;

    // 循环发送（ET 模式必须一次性写完所有数据）
    while (total_written < data_len) {
        // 非阻塞 write：数据没写完会返回 EAGAIN/EWOULDBLOCK，退出循环
        write_bytes = write(client_data->client_fd, data + total_written, data_len - total_written);

        if (write_bytes > 0) {
            total_written += write_bytes;
        } else if (write_bytes == 0) {
            // 写入 0 字节，无意义，退出循环
            break;
        } else {
            // 写入失败
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 数据暂时写不完，下次触发写事件再写
                break;
            } else {
                // 其他错误，关闭连接
                std::cerr << "向客户端发送数据失败：" << std::strerror(errno) << std::endl;
                epoll_remove(epoll_fd, client_data->client_fd);
                delete client_data;
                return;
            }
        }
    }

    // 数据全部发送完成，清空 write_buffer，取消写事件（只保留读事件）
    if (total_written == data_len) {
        std::cout << "向客户端[" << client_data->client_ip << ":" << client_data->client_port
                  << "] 回声成功：" << client_data->write_buffer << std::endl;
        client_data->write_buffer.clear();
        client_data->read_buffer.clear();
        // 取消写事件，只保留读事件（等待客户端下次发数据）
        epoll_add_or_modify(epoll_fd, client_data->client_fd, EPOLLIN | EPOLLET, client_data);
    }
}

// 初始化服务器 socket
int init_server_socket() {
    // 创建 socket（TCP 协议）
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        throw std::system_error(errno, std::generic_category(), "创建 socket 失败");
    }

    // 设置端口复用（避免服务器重启时端口被占用）
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
        throw std::system_error(errno, std::generic_category(), "setsockopt 失败");
    }

    // 绑定端口和 IP
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;         // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // 监听所有网卡 IP
    server_addr.sin_port = htons(PORT);       // 端口转换为网络字节序

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        throw std::system_error(errno, std::generic_category(), "bind 端口失败");
    }

    // 开始监听（backlog=128 表示等待队列最大长度）
    if (listen(server_fd, 128) == -1) {
        throw std::system_error(errno, std::generic_category(), "listen 失败");
    }

    // 设置服务器 FD 为非阻塞（配合 epoll ET 模式）
    set_non_blocking(server_fd);

    std::cout << "服务器初始化成功，监听端口：" << PORT << std::endl;
    return server_fd;
}

int main() {
    try {
        // 1. 初始化服务器 socket
        int server_fd = init_server_socket();

        // 2. 创建 epoll 实例（参数大于 0 即可，现代 Linux 忽略该参数）
        int epoll_fd = epoll_create1(0);
        if (epoll_fd == -1) {
            throw std::system_error(errno, std::generic_category(), "epoll_create1 失败");
        }

        // 3. 向 epoll 注册服务器 FD 的读事件（监听新连接，ET 模式）
        auto server_data = std::make_unique<ClientData>();  // 服务器 FD 绑定的占位数据
        server_data->client_fd = server_fd;
        epoll_add_or_modify(epoll_fd, server_fd, EPOLLIN | EPOLLET, server_data.get());

        // 4. 循环等待 epoll 事件（服务器主循环）
        struct epoll_event events[MAX_EVENTS];  // 存储就绪事件的数组
        while (true) {
            // 阻塞等待事件触发（EPOLL_TIMEOUT=-1 无限阻塞）
            int ready_events = epoll_wait(epoll_fd, events, MAX_EVENTS, EPOLL_TIMEOUT);
            if (ready_events == -1) {
                if (errno == EINTR) {  // EINTR：被信号中断（比如 Ctrl+C），忽略继续循环
                    continue;
                }
                throw std::system_error(errno, std::generic_category(), "epoll_wait 失败");
            }

            // 遍历所有就绪事件
            for (int i = 0; i < ready_events; ++i) {
                ClientData* data = static_cast<ClientData*>(events[i].data.ptr);
                int fd = data->client_fd;

                // 事件类型判断
                if (fd == server_fd) {
                    // 服务器 FD 的读事件：新客户端连接
                    handle_new_connection(server_fd, epoll_fd);
                } else {
                    if (events[i].events & EPOLLIN) {
                        // 客户端 FD 的读事件：客户端发数据
                        handle_read_event(data, epoll_fd);
                    }
                    if (events[i].events & EPOLLOUT) {
                        // 客户端 FD 的写事件：可以向客户端发数据
                        handle_write_event(data, epoll_fd);
                    }
                }
            }
        }

        // 5. 资源释放（实际不会执行，因为主循环是无限的）
        close(epoll_fd);
        close(server_fd);

    } catch (const std::exception& e) {
        std::cerr << "服务器异常退出：" << e.what() << std::endl;
        return 1;
    }

    return 0;
}