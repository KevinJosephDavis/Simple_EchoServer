#include<iostream>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<fcntl.h>
#include<vector>
#include<system_error>
#include<string>
#include<cstring>
#include<netinet/in.h>
#include<sys/epoll.h>
#include<cerrno>
#include<unistd.h>
#include<memory>

constexpr int MAX_EVENTS=1024;
constexpr int PORT=8080;
constexpr int BUFFER_SIZE=4096;
constexpr int EPOLL_TIMEOUT=-1; //表示超时

//定义客户端结构体
struct ClientData {
	int client_fd;//用来通信的文件描述符
	char ip[INET_ADDRSTRLEN];//IPv4地址
	uint16_t port;
	std::string read_buffer;//将读取的信息存入read_buffer
	std::string write_buffer;//将要发送的信息存入write_buffer
};

//打印客户端信息
void print_client_info(const ClientData* client,const std::string& title) {
	std::cout << "[" << title << "]"
			  << "客户端IP:" << client->ip
			  << "，客户端端口:" << client->port
			  << "，客户端fd:" << client->client_fd
			  << std::endl;
}

//将文件描述符设置为非阻塞模式
void set_no_block(int fd) {
	int status=fcntl(fd,F_GETFL,0);//获取fd的状态
	if(status == -1) {
		throw std::system_error(errno,std::generic_category(),"获取fd状态失败");
	}
	if(fcntl(fd,F_SETFL,status | O_NONBLOCK) == -1) {
		throw std::system_error(errno,std::generic_category(),"设置fd状态失败");
	}
}

//注册/修改事件
void add_or_modify(int epoll_fd,int fd,uint32_t events,ClientData* client) {
	struct epoll_event ev;
	memset(&ev,0,sizeof(ev));
	ev.events = events;
	ev.data.ptr = client;//绑定客户端数据，便于获取

	//先尝试修改事件，如果不存在再注册
	if(epoll_ctl(epoll_fd,EPOLL_CTL_MOD,fd,&ev) == -1) {
		if(errno == ENOENT) {
			//表示事件不存在，注册
			if(epoll_ctl(epoll_fd,EPOLL_CTL_ADD,fd,&ev) == -1) {
				throw std::system_error(errno,std::generic_category(),"注册事件失败");
			}
		} else {
			throw std::system_error(errno,std::generic_category(),"修改事件失败");
		}
	}
}

//删除事件
void remove(int epoll_fd,int fd) {
	if(fcntl(epoll_fd,EPOLL_CTL_DEL,fd,nullptr) == -1) {
		throw std::system_error(errno,std::generic_category(),"删除事件失败");
	}
	close(fd);
}

void handle_new_connection(int server_fd,int epoll_fd) {
	//server_fd的已完成连接队列有新连接
	struct sockaddr_in client_addr;
	socklen_t client_len=sizeof(client_addr);
	int client_fd=accept4(server_fd,(sockaddr*)(&client_addr),&client_len,SOCK_NONBLOCK);
	if(client_fd == -1) {
		std::cerr << "accept新连接失败" << std::strerror(errno) << std::endl;
		return;
	}

	auto client = std::make_unique<ClientData>();
	client->client_fd = client_fd;
	client->port=ntohs(client_addr.sin_port); 
	if(inet_ntop(AF_INET,&client_addr.sin_addr,client->ip,sizeof(client->ip)) == nullptr) {
		std::cerr << "IP转换失败" << std::strerror(errno) << std::endl;
		close(client_fd);
		return;
	}

	print_client_info(client.get(),"获得新客户端连接");
	add_or_modify(epoll_fd,client_fd,EPOLLIN | EPOLLET,client.release());
}

//处理客户端读事件
void handle_read_event(ClientData* client,int epoll_fd) {
	char raw_buf[BUFFER_SIZE];
	ssize_t read_bytes;

	//ET模式必须一次性读完
	while(true) {
		memset(raw_buf,0,sizeof(raw_buf));
		read_bytes=read(client->client_fd,raw_buf,BUFFER_SIZE-1);//非阻塞read
		if(read_bytes > 0) {
			//读取成功
			client->read_buffer.assign(raw_buf,read_bytes);
			std::cout << "收到客户端" << "[IP:" << client->ip << ",端口:" << client->port 
					  << "]的信息" << client->read_buffer << std::endl;
			//回声
			client->write_buffer=client->read_buffer;
			//注册写事件
			add_or_modify(epoll_fd,client->client_fd,EPOLLIN | EPOLLOUT | EPOLLET,client);
		} else if(read_bytes == 0) {
			//正常关闭
			print_client_info(client,"客户端正常关闭连接");
			remove(epoll_fd,client->client_fd);
			close(client->client_fd);//这一步是否必要？
			delete client;
			return;
		} else {
			//读取失败
			if(errno == EAGAIN || errno == EWOULDBLOCK) {
				//非阻塞read情况下数据已经读完
				break;//读完就退出循环
			} else {
				//客户端出现了异常断开
				std::cerr << "客户端连接异常断开" << std::strerror(errno) << std::endl;
				remove(epoll_fd,client->client_fd);
				close(client->client_fd);
				delete client;
				return;
			}
		}
	}
}

//处理客户端写事件
void handle_write_event(ClientData* client,int epoll_fd) {
	const char* data=client->write_buffer.data();
	size_t already_written=0;
	size_t data_len=client->write_buffer.size();
	ssize_t write_bytes=0;

	//循环发送，因为ET必须一次性写完数据
	while(already_written < data_len) {
		write_bytes=write(client->client_fd,data+already_written,data_len-already_written);
		if(write_bytes > 0) {
			already_written+=write_bytes;
		} else if(write_bytes == 0) {
			//写入0字节，无意义，退出循环
			break;
		} else {
			//写入失败
			if(errno == EAGAIN || errno == EWOULDBLOCK) {
				//数据暂时写不完，等到下次触发事件再写
				break;
			} else {
				std::cerr << "写入数据失败" << std::strerror(errno) << std::endl;
				remove(epoll_fd,client->client_fd);
				close(client->client_fd);
				delete client;
				return;
			}
		}
	}

	//数据全部写完，清空两个缓冲区，取消写事件
	if(already_written == data_len) {
		std::cout << "向客户端[IP:" << client->ip << ",端口:" << client->port << "]"
				  << "发送回声消息成功" << std::endl;
		client->read_buffer.clear();
		client->write_buffer.clear();
		//取消写事件，保留读事件
		add_or_modify(epoll_fd,client->client_fd,EPOLLIN | EPOLLET,client);
	}
}

//初始化服务器
int init_server() {
	//1.创建socket
	int server_fd=socket(AF_INET,SOCK_STREAM,0);
	if(server_fd < 0) {
		throw std::system_error(errno,std::generic_category(),"创建监听socket失败");
	}

	//2.绑定地址
	struct sockaddr_in server_addr{};
	server_addr.sin_family=AF_INET;
	server_addr.sin_addr.s_addr=INADDR_ANY;//监听所有网卡
	server_addr.sin_port=htons(PORT);
	if(bind(server_fd,(sockaddr*)(&server_addr),sizeof(server_addr)) < 0) {
		throw std::system_error(errno,std::generic_category(),"绑定地址与端口失败");
	}

	//3.监听
	if(listen(server_fd,128) == -1) {
		throw std::system_error(errno,std::generic_category(),"监听失败");
	}

	//4.设置为非阻塞
	set_no_block(server_fd);

	std::cout << "服务器初始化成功，监听端口" << PORT << std::endl;
	return server_fd;
}

int main() {
	try {
		//1.初始化服务器
		int server_fd=init_server();
		
		//2.创建epoll实例
		int epoll_fd=epoll_create1(0);
		if(epoll_fd < 0) {
			throw std::system_error(errno,std::generic_category(),"创建epoll实例失败");
		}

		//3.注册server_fd
		auto server_data=std::make_unique<ClientData>();
		server_data->client_fd=server_fd;
		add_or_modify(epoll_fd,server_fd,EPOLLIN | EPOLLET,server_data.release());

		//4.循环等待epoll事件
		struct epoll_event events[MAX_EVENTS];//存储就绪事件
		while(true) {
			int ready_events_cnt=epoll_wait(epoll_fd,events,MAX_EVENTS,EPOLL_TIMEOUT);
			if(ready_events_cnt == -1) {
				if(errno == EINTR) {
					//信号被中断，继续循环
					continue;
				} else {
					throw std::system_error(errno,std::generic_category(),"epoll_wait失败");
				}
			}

			//遍历所有就绪事件
			for(size_t i=0;i<ready_events_cnt;i++) {
				ClientData* data=static_cast<ClientData*>(events[i].data.ptr);
				int fd=data->client_fd;

				//1.如果是监听socket的事件
				if(fd == server_fd) {
					//说明监听socket关注的事件（是否有新连接）有了结果
					handle_new_connection(server_fd,epoll_fd);
				} else {
					if(events[i].events && EPOLLIN) {
						//如果关注读事件
						handle_read_event(data,epoll_fd);
					} else if(events[i].events && EPOLLOUT) {
						//如果关注写事件
						handle_write_event(data,epoll_fd);
					}
				}
			}
		}
		
		close(server_fd);
		close(epoll_fd);
	} catch(const std::exception& e) {
		std::cerr << "服务器异常退出" << e.what() <<std::endl;
		return 1;
	}
	return 0;
}
