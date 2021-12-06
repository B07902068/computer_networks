#include <iostream>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h> 
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>

#define ERR_EXIT(a) { perror(a); exit(1); }

#define BUFF_SIZE 1024

using namespace std;


typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[BUFF_SIZE];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    // you don't need to change this.
	int item;
    int wait_for_write;  // used by handle_read to know if the header is read or not.
} request;
typedef struct command {
	int command_num;
	char file_name[1024];
	char full_text[1024];
} CMD;

server svr;

static void init_server(unsigned short port);

void ls(char folder_addr[], char result[]){
    DIR* dp;
    struct dirent* dentry;

    
    if(! (dp=opendir(folder_addr)) ) {
        printf("opendir error\n");
        exit(2);
    }

    while(1) {
        dentry=readdir(dp);
        if(!dentry)
            break;
		if (strncmp(dentry->d_name, ".", 1) == 0)
			continue;
        strcat(result, dentry->d_name);
		result[strlen(result)] = '\n';
		//printf("%s\n", dentry->d_name);
    }
	result[strlen(result)] = '\0';
    return;
}

void put(int remoteSocket, char folder_addr[], char file_name[])
{
	char path[BUFF_SIZE] = {}, buffer[BUFF_SIZE] = {};
	strcpy(path, folder_addr);
	strcat(path, "/");
	strcat(path, file_name);
	fprintf(stderr, "path = %s\n", path);

	int fd, size = -1, sent, n;
	off_t offset = -1;
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "open fail\n");
		sent = send(remoteSocket, &size, sizeof(size), 0); // send -1 size for not exist
	} else {
		if ( (offset = lseek(fd, 0, SEEK_END)) < 0) {
			ERR_EXIT("seek end error");	
		}
		fprintf(stderr, "size = %d\n", offset);
		size = offset;
		sent = send(remoteSocket, &size, sizeof(size), 0);// send file size
		if ( (offset = lseek(fd, 0, SEEK_SET)) < 0) {
			ERR_EXIT("seek set error");	
		}

		while ((n = read(fd, buffer, BUFF_SIZE)) > 0) {
			sent = send(remoteSocket, buffer, n, 0);
		}
		fprintf(stderr, "sent %d byte\n", size);
		
		if (close(fd) < 0) {
			ERR_EXIT("close error");	
		}	
	}

}
int get(int localSocket, char folder_addr[], char file_name[])
{
	char path[BUFF_SIZE] = {}, buffer[BUFF_SIZE] = {};
	int size, recved, fd;

	recved = recv(localSocket, &size,sizeof(size) ,0);	
	if (recved < 0){
		cout << "recv failed, with received bytes = " << recved << endl;
		return -1;
	} else if (recved > 0){
		printf("%d:%d\n",recved,size);
		if (size < 0) {
			printf("The %s doesn't exist.\n", file_name);
		} else {
			strcpy(path, folder_addr);
			strcat(path, "/");
			strcat(path, file_name);
			fprintf(stderr, "path = %s\n", path);
			if ( (fd = open(path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
				ERR_EXIT("open error");
			}

			while (size > 0) {
				recved = recv(localSocket,buffer,sizeof(buffer),0);	
				if (recved < 0){
					cout << "recv failed, with received bytes = " << recved << endl;
					break;
				} else if (recved > 0){
					if (write(fd, buffer, recved) != recved) {
						ERR_EXIT("write error");
					}
					size -= recved;
				} else if (recved == 0){
					cout << "<end>\n";
					break;
				}	
			}
			if (close(fd) < 0) {
				ERR_EXIT("close error");	
			}
			if (size  > 0) {
				return -1;
			} else {
				return 1;
			}
		}
	} else if (recved == 0){
		cout << "<end>\n";
		return 0;
	}
	return 1;
}

int main(int argc, char** argv){

	if (argc != 2) {
		fprintf(stderr, "need 2 argument\n");
		return -1;
	}

	//create own folder
	char folder_addr[] = "./server_folder";
	struct stat st = {0};
	if (stat(folder_addr, &st) == -1) {
		if (mkdir(folder_addr, 0700) != 0) {
			fprintf(stderr, "fail to create folder\n");
			return -1;
		}
		fprintf(stderr, "folder created\n");
	} else {
		fprintf(stderr, "folder already exists\n");
	}
	init_server(atoi(argv[1]));
    int localSocket, remoteSocket, port;//4097;                               
	localSocket = svr.listen_fd;
	port = svr.port;
    
	struct  sockaddr_in localAddr,remoteAddr;
          
    int addrLen = sizeof(struct sockaddr_in);  

    /*localSocket = socket(AF_INET , SOCK_STREAM , 0);
    
    if (localSocket == -1){
        printf("socket() call failed!!");
        return 0;
    }

    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = htons(port);

    char Message[BUFF_SIZE] = {};


        
    if( bind(localSocket,(struct sockaddr *)&localAddr , sizeof(localAddr)) < 0) {
        printf("Can't bind() socket");
        return 0;
    }
    
    listen(localSocket , 3);*/

	char Message[BUFF_SIZE] = {};
	char receiveMessage[BUFF_SIZE] = {};
	int recved;
	CMD cmd;

	int maxfd = getdtablesize();
	fd_set master, readingset;
	FD_ZERO(&master);
	FD_ZERO(&readingset);
	FD_SET(svr.listen_fd, &master);
	struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    while(1){    
        //std::cout <<  "Waiting for connections...\n"
         //       <<  "Server Port:" << port << std::endl;

		readingset = master;
		if (select(maxfd, &readingset, NULL, NULL, &timeout) == -1) {
			ERR_EXIT("select");		
		}
		
		for (int i = 0; i < maxfd; i++) {
			if (FD_ISSET(i, &readingset)) {
				if(i == svr.listen_fd) {
					remoteSocket = accept(localSocket, (struct sockaddr *)&remoteAddr, (socklen_t*)&addrLen);  
				
					if (remoteSocket < 0) {
						printf("accept failed!");
						return 0;
					}
					
					std::cout << "Connection accepted" << std::endl;
					FD_SET(remoteSocket, &master);
					fprintf(stderr, "getting a new request... fd %d from %s\n", remoteSocket, inet_ntoa(remoteAddr.sin_addr));
				} else {
					remoteSocket = i;
					//memset(receiveMessage, 0, sizeof(receiveMessage));
					//if ((recved = read(remoteSocket, receiveMessage, sizeof(receiveMessage))) <= 0){
					//bzero(receiveMessage,sizeof(char)*BUFF_SIZE);
					//if ((recved = recv(remoteSocket,receiveMessage,sizeof(char)*BUFF_SIZE,0)) <= 0){
					if ((recved = recv(remoteSocket,&cmd,sizeof(CMD),0)) <= 0){
						if (recved == 0){
							cout << "<end>\n";
						} else {
							cout << "recv failed, with received bytes = " << recved << endl;
						}
						close(remoteSocket);
						FD_CLR(remoteSocket, &master);
					} else if (recved > 0){
						//printf("%d:%s\n",recved,receiveMessage);
						printf("%d\n",recved);
						fprintf(stderr, "%d\n%s\n%s\n", cmd.command_num, cmd.file_name, cmd.full_text);
						int sent;
						
						if (cmd.command_num == 1) {
							char result[BUFF_SIZE] = {};
							ls(folder_addr, result);
							//printf("ls result:\n%s\n", result);
							strcpy(Message,result);
							sent = send(remoteSocket,Message,strlen(Message),0);
						} else if(cmd.command_num == 2) { //recieve from client
							if (get(remoteSocket, folder_addr, cmd.file_name) <= 0) {
								fprintf(stderr, "get error\n");
								close(remoteSocket);
								FD_CLR(remoteSocket, &master);
							}
						} else if(cmd.command_num == 3) { //send to client
							put(remoteSocket, folder_addr, cmd.file_name);
						} else if(cmd.command_num == 4) {
		
						}
						/*strcpy(Message,"Hello World!!\n");
						sent = send(remoteSocket,Message,strlen(Message),0);
						strcpy(Message,"Computer Networking is interesting!!\n");
						sent = send(remoteSocket,Message,strlen(Message),0);*/
					}
						
				} //if(i == svr.listen_fd)else
			}//if(FD_ISSET)
		}///for(i = 0~maxfd)
    }//while(1)
    return 0;
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    /*if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }*/
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }
}
