#include<iostream>
#include<cstring> //
#include<mutex> //
#include<thread> //
#include<memory> //
#include<sys/socket.h>
#include<netinet/in.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<system_error>

#define PORT 8080
#define BUFFER_SIZE 1024

//全局互斥锁,保护多线程对cout的并发访问
std::mutex cout_mutex;

//封装客户端数据,替代void*打包
struct ClientData {
    int client_fd; //和客户端通信的socket
    struct sockaddr_in client_addr; //客户端地址信息
};

class EchoServer {
private:
    int server_fd;//监听socket
    uint16_t port;//监听端口

    void print_client_info(const ClientData* data,const std::string& title) {
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(data->client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        uint16_t client_port = ntohs(data->client_addr.sin_port);

        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "\n========== " << title << " ==========\n";
        std::cout << "客户端IP: " << client_ip << "\n";
        std::cout << "客户端Port: " << client_port << "\n";
        std::cout << "=================================\n";
    }

    void handle_client(std::unique_ptr<ClientData> client_data) {
        int client_fd=client_data->client_fd;
        auto& client_addr=client_data->client_addr;
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET,&(client_addr.sin_addr),client_ip,INET_ADDRSTRLEN);
        uint16_t client_port=ntohs(client_addr.sin_port);

        std::string buffer;
        buffer.reserve(BUFFER_SIZE);

        while(1) {
            char raw_buf[BUFFER_SIZE];
            ssize_t read_bytes=read(client_fd,raw_buf,BUFFER_SIZE);
            if(read_bytes <= 0) {
                {
                    std::lock_guard<std::mutex>lock(cout_mutex);
                    if(read_bytes == 0) {
                        std::cout << "客户端[" << client_ip << ":" << client_port << "]已关闭连接\n";
                    } else {
                        std::cerr << "读取客户端[" << client_ip << ":" << client_port << "]消息失败: " << std::strerror(errno) << "\n";
                    }
                }
                close(client_fd);
                break;
            }
            buffer.assign(raw_buf,read_bytes);
            
            {
                std::lock_guard<std::mutex>lock(cout_mutex);
                std::cout << "收到客户端[" << client_ip << ":" << client_port << "]消息: " << buffer << "\n";
            }

            ssize_t sent_bytes=send(client_fd,buffer.c_str(),buffer.length(),0);
            if(sent_bytes < 0) {
                std::lock_guard<std::mutex>lock(cout_mutex);
                std::cerr << "发送回声消息到客户端[" << client_ip << ":" << client_port << "]失败: " << std::strerror(errno) << "\n";
                close(client_fd);
                break;
            }
        }
        close(client_fd);
        print_client_info(client_data.get(),"客户端连接关闭");
    }  

public:
    EchoServer(uint16_t port):port(PORT),server_fd(-1){};

    //启动服务器
    void start() {
        //1.创建监听socket
        server_fd=socket(AF_INET,SOCK_STREAM,0);
        if(server_fd < 0) {
            throw std::system_error(errno,std::generic_category(),"创建监听socket失败");
        }

        //2.绑定地址和端口
        struct sockaddr_in server_addr;
        server_addr.sin_family=AF_INET;
        server_addr.sin_addr.s_addr=INADDR_ANY;
        server_addr.sin_port=htons(PORT);
        if(bind(server_fd,(sockaddr*)&server_addr,sizeof(server_addr)) < 0) {
            close(server_fd);
            throw std::system_error(errno,std::generic_category(),"绑定地址和端口失败");
        }

        //3.开始监听
        if(listen(server_fd,10) < 0) {
            close(server_fd);
            throw std::system_error(errno,std::generic_category(),"监听失败");
        }

        {
            std::lock_guard<std::mutex>lock(cout_mutex);
            std::cout << "服务器启动，监听端口: " << PORT << std::endl;
        }

        //4.接受客户端连接
        while(1) {
            struct sockaddr_in client_addr;
            socklen_t client_addr_len=sizeof(client_addr);
            int client_fd=accept(server_fd,(sockaddr*)&client_addr,&client_addr_len);
            if(client_fd < 0) {
                std::lock_guard<std::mutex>lock(cout_mutex);
                std::cerr << "接受客户端连接失败: " << std::strerror(errno) << "\n";
                continue;
            }

            //封装客户端数据
            auto client_data=std::make_unique<ClientData>();
            client_data->client_fd=client_fd;
            client_data->client_addr=client_addr;

            print_client_info(client_data.get(),"新客户端连接");

            //创建线程处理客户端请求
            std::thread client_thread(&EchoServer::handle_client, this, std::move(client_data));
            client_thread.detach(); //分离线程
        }
    }
    ~EchoServer() {
        if(server_fd != -1) {
            close(server_fd);
            std::cout<<"服务器已关闭\n";
        }
    }
};

int main() {
    try {
        EchoServer server(PORT);
        server.start();
    } catch (const std::system_error& e) {
        std::cerr << "系统错误: " << e.what() << "\n";
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}