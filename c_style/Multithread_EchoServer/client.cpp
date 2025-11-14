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

int main() {
    //1.创建socket
    int sockfd=socket(AF_INET,SOCK_STREAM,0);
    if(sockfd<0) {
        std::cerr<<"创建socket失败"<<std::endl;
        return 0;
    }

    //2.确定服务端IP与端口
    struct sockaddr_in serv_addr{};
    serv_addr.sin_family=AF_INET;
    serv_addr.sin_port=htons(PORT);

    //转换IP
    if(inet_pton(AF_INET,"127.0.0.1",&serv_addr.sin_addr)<=0) {
        std::cerr<<"无效地址/地址不支持"<<std::endl;
        close(sockfd);
        return 0;
    }

    //3.连接服务端
    if(connect(sockfd,(sockaddr*)&serv_addr,sizeof(serv_addr))<0) {
        std::cerr<<"连接失败"<<std::endl;
        close(sockfd);
        return 0;
    }

    std::cout<<"成功与服务端建立连接"<<std::endl;
    std::cout<<"输入quit退出"<<std::endl;

    char buffer[BUFFER_SIZE];
    std::string input;
    while(1) {
        std::cout<<" > ";
        std::cin>>input;
        if(input == "quit") {
            break;
        }

        input += '\n';

        ssize_t send_bytes=send(sockfd,input.c_str(),input.length(),0);
        if(send_bytes < 0) {
            std::cerr<<"发送消息失败"<<std::endl;
            break;
        }

        //接收服务端的回声消息
        memset(buffer,0,BUFFER_SIZE);
        ssize_t read_bytes=read(sockfd,buffer,BUFFER_SIZE-1);
        if(read_bytes <= 0) {
            if(read_bytes == 0) {
                std::cerr<<"服务端已关闭"<<std::endl;
            } else {
                std::cerr<<"读取回声消息失败"<<std::endl;
            }
            break;
        }

        std::cout<<"成功接收服务端回声，内容为："<<buffer;
    }
    close(sockfd);
    return 0;
}
