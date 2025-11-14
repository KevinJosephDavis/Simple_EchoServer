#include<iostream>
#include <ostream>
#include<pthread.h>
#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<string.h>
#include<arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

//
void *handle_client(void *arg) {
    int client_fd=*(int*)arg;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr=*(struct sockaddr_in*)((int*)arg+1);
    free(arg);

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET,&client_addr.sin_addr,client_ip,INET_ADDRSTRLEN);
    uint16_t client_port=ntohs(client_addr.sin_port);

    while(1) {
        memset(buffer,0,BUFFER_SIZE);
        ssize_t read_bytes=read(client_fd,buffer,BUFFER_SIZE-1);
        if(read_bytes<=0) {
            if(read_bytes<0) {
                std::cerr<<"[客户端"<<client_ip<<"："<<client_port<<"] 读取失败"<<std::endl;
            } else {
                std::cout<<"[客户端"<<client_ip<<"："<<client_port<<"] 断开连接"<<std::endl;
            }
            break;
        }

        std::cout<<"[客户端"<<client_ip<<"："<<client_port<<"] 收到消息："<<buffer;
        ssize_t send_bytes=send(client_fd,buffer,read_bytes,0);
        if(send_bytes<0) {
            std::cerr<<"[客户端"<<client_ip<<"："<<client_port<<"] 发送回声消息失败"<<std::endl;
            break;
        } else {
            std::cout<<"[客户端"<<client_ip<<"："<<client_port<<"] 发送回声消息成功"<<std::endl;
        }
    }
    close(client_fd);
    pthread_exit(nullptr);
}

int main() {
    //1.创建监听socket
    int server_fd=socket(AF_INET,SOCK_STREAM,0);
    if(server_fd<0) {
        std::cerr<<"创建监听socket失败"<<std::endl;
        return 0;
    }

    //2.绑定IP与端口
    struct sockaddr_in server_addr{};
    server_addr.sin_family=AF_INET;
    server_addr.sin_addr.s_addr=INADDR_ANY;
    server_addr.sin_port=htons(PORT);

    if(bind(server_fd,(sockaddr*)&server_addr,sizeof(server_addr))<0) {
        std::cerr<<"绑定IP与地址失败"<<std::endl;
        close(server_fd);
        return 0;
    }

    //3.开始监听
    if(listen(server_fd,10)<0) {
        std::cerr<<"监听失败"<<std::endl;
        close(server_fd);
        return 0;
    }

    std::cout<<"服务端开始监听，端口号为："<<PORT<<std::endl;

    while(1) {
        //主线程持续接收连接
        struct sockaddr_in client_addr{};
        socklen_t client_len=sizeof(client_addr);
        int client_fd=accept(server_fd,(sockaddr*)&client_addr,&client_len);
        if(client_fd<0) {
            std::cerr<<"接受连接失败"<<std::endl;
            continue;
        }

        //打印客户端信息
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET,&client_addr.sin_addr,client_ip,INET_ADDRSTRLEN);
        std::cout<<"\n新客户端连接："<<std::endl;
        std::cout<<"ip:"<<client_ip<<" port:"<<ntohs(client_addr.sin_port)<<std::endl;

        //创建子线程，用来收发消息
        void* arg=malloc(sizeof(int)+sizeof(struct sockaddr_in));
        *(int*)arg=client_fd;
        memcpy((int*)arg+1,&client_addr,sizeof(struct sockaddr_in));

        pthread_t tid;
        if(pthread_create(&tid,nullptr,handle_client,arg)!=0) {
            std::cerr<<"创建子线程失败"<<std::endl;
            close(client_fd);
            free(arg);
        }

        pthread_detach(tid);
    }
    close(server_fd);
    return 0;
}
