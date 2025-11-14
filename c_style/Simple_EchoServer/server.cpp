#include<iostream>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<cstring>

const int PORT=8080;
const int BUFFER_SIZE=1024;

int main(){
	//1.创建socket
    int server_fd=socket(AF_INET,SOCK_STREAM,0);
	if(server_fd<0) {
		std::cerr<<"无法创建socket"<<std::endl;
		return 0;
	}

	//2.绑定IP地址与端口
	struct sockaddr_in server_addr{};//需要<netinet/in.h>头文件，否则只有sockaddr
	server_addr.sin_family=AF_INET;//ipv4
	server_addr.sin_port=htons(PORT);//端口号，本机字节序转换为网络传输字节序
	server_addr.sin_addr.s_addr=INADDR_ANY;//设置IP地址，网络接口任意

	if(bind(server_fd,(sockaddr*)&server_addr,sizeof(server_addr))<0) {
		//理解sizeof(server_addr)为什么变成socklen_t类型以及sockaddr与sockaddr_in的关系
		std::cerr<<"绑定失败"<<std::endl;
		close(server_fd);//及时关闭，以免浪费资源
		return 0;
	}

	//3.开始监听
	if(listen(server_fd,10)<0) {
		//已完成连接队列的长度为10
		std::cerr<<"监听失败"<<std::endl;
		close(server_fd);
		return 0;
	}

	//走到这里说明监听成功
	std::cout<<"服务器开始监听，端口为"<<PORT<<std::endl;

	//4.有来自客户端的连接，将其放入已完成连接队列中
	struct sockaddr_in client_addr{};//存储客户端信息
	socklen_t client_len=sizeof(client_addr);
	int client_fd=accept(server_fd,(sockaddr*)&client_addr,&client_len);
	if(client_fd<0){
		std::cerr<<"接受连接失败"<<std::endl;
		//这里是否需要关闭server_fd？没必要吧，因为还可以接着accept下一个连接？不对，这里没有循环
		//
		close(server_fd);
		return 0;
	}
	
	//5.显示客户端信息
	char client_ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET,&(client_addr.sin_addr),client_ip,INET_ADDRSTRLEN);//将二进制ip转换为字符串
	std::cout<<"客户端ip："<<client_ip<<std::endl;
	std::cout<<"端口："<<ntohs(client_addr.sin_port)<<std::endl;

	//6.与客户端通信
	char buffer[BUFFER_SIZE];//缓冲区大小，用于存放消息
	while(1){
		//持续读取客户端信息
		//因为是循环，所以每次回到循环开头都要清空缓冲区
		memset(buffer,0,BUFFER_SIZE);
		int read_bytes=read(client_fd,buffer,BUFFER_SIZE-1);//最多只能读BUFFERSIZE-1个字符，最后要留一个位置给/0
		if(read_bytes<=0) {
			if(read_bytes<0) {
				std::cerr<<"读取客户端信息失败"<<std::endl;
			} else {
				//read_bytes==0，意味读到了EOF,结束
				std::cout<<"客户端终止了通信"<<std::endl;
			}
			break;//不管是上面两种情况的哪一种，都要break
		}
		
		//走到这里意味着读到了信息，打印出来即可
		std::cout<<"收到客户端信息："<<buffer;
		
		//回声
		int send_bytes=send(client_fd,&buffer,read_bytes,0);
		if(send_bytes<0) {
			std::cerr<<"发送回声消息失败"<<std::endl;
			break;//疑问：出问题了不需要关闭server_fd和client_fd吗？
		}
		std::cout<<"成功发送"<<std::endl;
	}
	close(client_fd);
	close(server_fd);//先后顺序有没有说法？
	std::cout<<"服务端已关闭"<<std::endl;
	return 0;
}
