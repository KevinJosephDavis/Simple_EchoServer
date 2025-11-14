#include<iostream>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<netinet/in.h>
#include<cstring>

const int PORT=8080;
const int BUFFER_SIZE=1024;

int main() {
	//1.创建Socket
	int sockfd=socket(AF_INET,SOCK_STREAM,0);
	if(sockfd<0) {
		std::cerr<<"创建socket失败"<<std::endl;
		return 0;
	}

	//2.确定要发送的服务端
	struct sockaddr_in serv_addr{};
	serv_addr.sin_family=AF_INET;
	serv_addr.sin_port=htons(PORT);
	
	//将服务端IP地址转换为二进制
	if(inet_pton(AF_INET,"127.0.0.1",&serv_addr.sin_addr)<0) {
		std::cerr<<"IP地址无效"<<std::endl;
		close(sockfd);
		return 0;
	}

	//走到这里，已经确定好了目标

	//3.连接服务端
	if(connect(sockfd,(sockaddr*)&serv_addr,sizeof(serv_addr))<0) {
		std::cerr<<"连接服务端失败"<<std::endl;
		close(sockfd);
		return 0;
	}
	
	//成功建立连接，准备收发消息
	std::cout<<"成功与服务端建立连接，输入quit可退出"<<std::endl;

	//4.发消息+接收回声消息
	char buffer[BUFFER_SIZE];
	std::string input;
	while(1) {
		std::cout<<" > ";//表示接收客户端输入的信息
		std::cin>>input;
		if(input == "quit") {
			break;
		}
		input += '\n';//方便服务端确认一行消息已经结束，避免粘包
		
		//开始发送消息
		int send_bytes=send(sockfd,input.c_str(),input.length(),0);
		if(send_bytes<0) {
			std::cerr<<"发送消息失败"<<std::endl;
			break;
		}
		
		//发送成功，准备接收服务端的回声消息
		memset(buffer,0,BUFFER_SIZE);//清空缓冲区，为新来的消息腾出空间
		int recv_bytes=read(sockfd,buffer,BUFFER_SIZE-1);
		if(recv_bytes <= 0 ) {
			if(recv_bytes == 0) {
				std::cout<<"服务端断开了连接"<<std::endl;
			} else {
				std::cerr<<"读取回声消息失败"<<std::endl;
			}
			break;
		}

		//成功读取回声消息，打印出来
		std::cout<<"服务端发送回声消息："<<buffer;
	}

	//5.关闭连接
	close(sockfd);
	std::cout<<"连接关闭"<<std::endl;
	return 0;
}

