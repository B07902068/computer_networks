#include <iostream>
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h> 
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFF_SIZE 1024

#define ERR_EXIT(a) { perror(a); exit(1); }

using namespace std;

typedef struct command {
	int command_num;
	char file_name[1024];
	char full_text[1024];
} CMD;


void parse_cmd(CMD *cmd) 
{
	cmd->full_text[strlen(cmd->full_text)-1] = '\0';
	if (strcmp(cmd->full_text, "ls") == 0) {
		cmd->command_num = 1;
		cmd->file_name[0] = '\0';
		return;
	}
	char *ptr = strchr(cmd->full_text, ' ');
	
	char temp[512] = {};

	if (ptr != NULL) {
		strncpy(temp, cmd->full_text, ptr-(cmd->full_text));
		strcpy(cmd->file_name, ptr+1);
	} else {
		cmd->command_num = -1;
		return;
	}
	if(strlen(cmd->file_name) == 0) {
		cmd->command_num = -1;		
		return;	
	}
	
	if (strcmp(temp, "put") == 0) {
		cmd->command_num = 2;
	} else if (strcmp(temp, "get") == 0) {
		cmd->command_num = 3;
	} else if (strcmp(temp, "play") == 0) {
		cmd->command_num = 4;
	} else {
		cmd->command_num = -1;
	}	
	
	return;
}

int ls(int localSocket) {
	int recved;
	char receiveMessage[BUFF_SIZE] = {};
	bzero(receiveMessage,sizeof(char)*BUFF_SIZE);
	recved = recv(localSocket,receiveMessage,sizeof(char)*BUFF_SIZE,0);
	//recved = read(localSocket, receiveMessage, sizeof(receiveMessage));	
	if (recved < 0){
		cout << "recv failed, with received bytes = " << recved << endl;
	} else if (recved > 0){
		printf("%d:%s\n",recved,receiveMessage);
	} else if (recved == 0){
		cout << "<end>\n";
	}
	return recved;
}
void put(int localSocket, char folder_addr[], char file_name[])
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
		printf("The %s doesn't exist.\n", file_name);
		sent = send(localSocket, &size, sizeof(size), 0); // send -1 size for not exist
	} else {
		if ( (offset = lseek(fd, 0, SEEK_END)) < 0) {
			ERR_EXIT("seek end error");	
		}
		fprintf(stderr, "size = %d\n", offset);
		size = offset;
		sent = send(localSocket, &size, sizeof(size), 0);// send file size
		if ( (offset = lseek(fd, 0, SEEK_SET)) < 0) {
			ERR_EXIT("seek set error");	
		}

		while ((n = read(fd, buffer, BUFF_SIZE)) > 0) {
			sent = send(localSocket, buffer, n, 0);
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

int main(int argc , char *argv[])
{
	if (argc != 2) {
		printf("need 2 argument\n");
		return -1;
	}

	//create own folder
	char folder_addr[] = "./client_folder";
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
	
	//resolve ip:port
	char ip[20];
	int port;
	char *arg_start = argv[1];
	arg_start = strtok(arg_start, ":");
	strcpy(ip, arg_start);
	arg_start = strtok(NULL, ":");
	port = atoi(arg_start);
	//fprintf(stderr, "ip = %s\n%d\n", ip, port);
	
	

	////connection
    int localSocket, recved;
    localSocket = socket(AF_INET , SOCK_STREAM , 0);

    if (localSocket == -1){
        printf("Fail to create a socket.\n");
        return 0;
    }

    struct sockaddr_in info;
    bzero(&info,sizeof(info));

    info.sin_family = PF_INET;
    info.sin_addr.s_addr = inet_addr(ip); //"127.0.0.1"
    info.sin_port = htons(port); //4097


    int err = connect(localSocket,(struct sockaddr *)&info,sizeof(info));
    if(err==-1){
        printf("Connection error\n");
        return 0;
    }
	//////////////////////
    char receiveMessage[BUFF_SIZE] = {};
	
	int sent;
	CMD cmd = {};

	fd_set master, readingset;
	FD_ZERO(&master);
	FD_ZERO(&readingset);
	FD_SET(localSocket, &master);
	struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    while(1){
		fprintf(stdout, "$ ");
		//fflush
		fgets(cmd.full_text, BUFF_SIZE, stdin);
		parse_cmd(&cmd);
		if (cmd.command_num == -1) {
			fprintf(stderr, "Command format error.\n");
			//break;
			continue;
		}
		fprintf(stderr, "%d\n%s\n%s\n", cmd.command_num, cmd.file_name, cmd.full_text);
		//sent = write(localSocket, cmd.full_text, strlen(cmd.full_text));
		//sent = send(localSocket, cmd.full_text, strlen(cmd.full_text), 0);
		sent = send(localSocket, &cmd, sizeof(cmd), 0);		
		fprintf(stderr, "sent = %d\n", sent);


		if(cmd.command_num == 1) {
			if(ls(localSocket) <= 0) {
				fprintf(stderr, "ls error\n");
				break;
			}
		} else if(cmd.command_num == 2) {
			put(localSocket, folder_addr, cmd.file_name);
		} else if(cmd.command_num == 3) {
			if (get(localSocket, folder_addr, cmd.file_name) <= 0) {
				fprintf(stderr, "get error\n");
				break;
			}
		} else if(cmd.command_num == 4) {
			
		}

		/*while(1) {
			readingset = master;
			if (select(localSocket + 1, &readingset, NULL, NULL, &timeout) == -1) {
				ERR_EXIT("select");		
			}

			if (FD_ISSET(localSocket, &readingset)) {
				bzero(receiveMessage,sizeof(char)*BUFF_SIZE);
				recved = recv(localSocket,receiveMessage,sizeof(char)*BUFF_SIZE,0);
				//recved = read(localSocket, receiveMessage, sizeof(receiveMessage));	
				if (recved < 0){
					cout << "recv failed, with received bytes = " << recved << endl;
					break;
				} else if (recved > 0){
					printf("%d:%s\n",recved,receiveMessage);
				} else if (recved == 0){
					cout << "<end>\n";
					break;
				}
			} else {
				break; // not robust at all
			}
		}*/
		/*while(1){
		    bzero(receiveMessage,sizeof(char)*BUFF_SIZE);
		    if ((recved = recv(localSocket,receiveMessage,sizeof(char)*BUFF_SIZE,0)) < 0){
		        cout << "recv failed, with received bytes = " << recved << endl;
		        break;
		    } else if (recved > 0){
		    	printf("%d:%s\n",recved,receiveMessage);
			} else if (recved == 0){
		        cout << "<end>\n";
		        break;
		    }
		}*/
    }
    printf("close Socket\n");
    close(localSocket);
    return 0;
}

