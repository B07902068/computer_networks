/*extern "C" { 
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}*/
#include <stdarg.h>
#include <inttypes.h>
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
#include "opencv2/opencv.hpp"
#include <pthread.h>
#include <signal.h>


#define ERR_EXIT(a) { perror(a); exit(1); }

#define BUFF_SIZE 1024

using namespace std;
using namespace cv;


typedef struct command {
	int command_num;
	char file_name[1024];
	char full_text[1024];
} CMD;
char folder_addr[] = "./server_folder";

static void init_server(unsigned short port);



void ls(int remoteSocket, char folder_addr[]){
	
	int sent;
	char buffer[BUFF_SIZE] = {};

    DIR* dp;
    struct dirent* dentry;

    
    if(! (dp=opendir(folder_addr)) ) {
        printf("opendir error\n");
        exit(2);
    }
	int len = 0;
    while(1) {
        dentry=readdir(dp);
        if(!dentry) {
			len = 0;
            break;
		}
		if (strncmp(dentry->d_name, ".", 1) == 0)
			continue;
		//printf("%s\n", dentry->d_name);
		len = strlen(dentry->d_name);
		sent = send(remoteSocket, &len, sizeof(int), 0);
		sent = send(remoteSocket, dentry->d_name, len, 0);
    }
	sent = send(remoteSocket, &len, sizeof(int), 0);

	
    return;
}

void put(int remoteSocket, char folder_addr[], char file_name[]) ///send file to client, when client cmd is "get"
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
		//fprintf(stderr, "size = %d\n", offset);
		size = offset;
		sent = send(remoteSocket, &size, sizeof(size), 0);// send file size
		if ( (offset = lseek(fd, 0, SEEK_SET)) < 0) {
			ERR_EXIT("seek set error");	
		}

		while ((n = read(fd, buffer, BUFF_SIZE)) > 0) {
			sent = send(remoteSocket, buffer, n, 0);
		}
		fprintf(stderr, "sent %d byte, socket %d\n", size, remoteSocket);
		
		if (close(fd) < 0) {
			ERR_EXIT("close error");	
		}	
	}

}
int get(int localSocket, char folder_addr[], char file_name[])  ///get file from client, when client cmd is "put"
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
					cout << "recv failed, with received bytes = " << recved << endl;////client disconnect
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

int play(int remoteSocket, char folder_addr[], char file_name[])
{
	char path[BUFF_SIZE] = {}, buffer[BUFF_SIZE] = {};
	strcpy(path, folder_addr);
	strcat(path, "/");
	strcat(path, file_name);
	fprintf(stderr, "path = %s\n", path);

	int sent, size = -1, recved;


	VideoCapture cap(path);


	if (!cap.isOpened()) {
		fprintf(stderr, "open fail\n");
		sent = send(remoteSocket, &size, sizeof(size), 0); // send -1 size for not exist
	} else {
			// server
		size = 1;
		sent = send(remoteSocket, &size, sizeof(size), 0);
		
		Mat imgServer;

		int n = (int)cap.get(CAP_PROP_FRAME_COUNT);
		double FPS = (double)cap.get(CAP_PROP_FPS);
		cout << n << ", " << FPS << endl;
		
		
		// get the resolution of the video
		int width = cap.get(CV_CAP_PROP_FRAME_WIDTH);
		int height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);
		cout  << width << ", " << height << endl;

		sent = send(remoteSocket, &n, sizeof(int), 0);
		sent = send(remoteSocket, &FPS, sizeof(double), 0);
		sent = send(remoteSocket, &width, sizeof(int), 0);
		sent = send(remoteSocket, &height, sizeof(int), 0);
		
		//allocate container to load frames 
		
		imgServer = Mat::zeros(width, height, CV_8UC3);    

	 
	 
		 // ensure the memory is continuous (for efficiency issue.)
		if(!imgServer.isContinuous()){
		     imgServer = imgServer.clone();
		}

		
		int ack = 0;
		for(int i = 0; i < n; i++){


		    //get a frame from the video to the container on server.
		    cap >> imgServer;
			
		    
		    // get the size of a frame in bytes 
		    int imgSize = imgServer.total() * imgServer.elemSize();
			//fprintf(stderr, "imgsize = %d\n", imgSize);
		    
		    // allocate a buffer to load the frame (there would be 2 buffers in the world of the Internet)
		    uchar buffer[imgSize];
		    
		    // copy a frame to the buffer
		    memcpy(buffer,imgServer.data, imgSize);
			sent = send(remoteSocket, buffer, imgSize, 0);
		    
		    recved = recv(remoteSocket, &ack, sizeof(int), 0);
			if (recved < 0){
				cout << "recv failed, with received bytes = " << recved << endl;
				//break;
				return -1;
			} else if (recved == 0){
				cout << "<end>\n";
				return 0;
			} 
			if (ack <= 0) {
				break;
			}
		}
		  ////////////////////////////////////////////////////
		
		cap.release();
		fprintf(stderr, "end of play, socket %d\n", remoteSocket);		
	}
	return 0;

}

void *serve(void *arg){
	int remoteSocket =  *(int*)arg;
	printf( "Connection accepted, Thread socket %d\n", remoteSocket);
	char Message[BUFF_SIZE] = {};
	char receiveMessage[BUFF_SIZE] = {};
	int recved;
	CMD cmd;

	while (1) {
		if ((recved = recv(remoteSocket,&cmd,sizeof(CMD),0)) <= 0){
			if (recved == 0){
				cout << "<end>\n";
			} else {
				cout << "recv failed, with received bytes = " << recved << endl;
			}
			printf("close connection %d\n", remoteSocket);
			close(remoteSocket);
			break;
		} else if (recved > 0){
			printf("%d\n",recved);
			fprintf(stderr, "%d\n%s\n%s\n", cmd.command_num, cmd.file_name, cmd.full_text);
			
			if (cmd.command_num == 1) {
				ls(remoteSocket, folder_addr);
			} else if(cmd.command_num == 2) { //recieve from client
				if (get(remoteSocket, folder_addr, cmd.file_name) <= 0) {
					fprintf(stderr, "get error\n");
					close(remoteSocket);
				}
			} else if(cmd.command_num == 3) { //send to client
				put(remoteSocket, folder_addr, cmd.file_name);
			} else if(cmd.command_num == 4) {
				play(remoteSocket, folder_addr, cmd.file_name);
			}
		}
	}
}

int main(int argc, char** argv){

	if (argc != 2) {
		fprintf(stderr, "need 2 argument\n");
		return -1;
	}
	signal(SIGPIPE, SIG_IGN);

	//create own folder
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


    int localSocket, remoteSocket, port;//4097; 

	port = atoi(argv[1]);    

	struct  sockaddr_in localAddr,remoteAddr;
          
    int addrLen = sizeof(struct sockaddr_in);  

    localSocket = socket(AF_INET , SOCK_STREAM , 0);
    
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
    
    listen(localSocket , 3);

	pthread_t pid;
	while(1){    
        std::cout <<  "Waiting for connections...\n"
                <<  "Server Port:" << port << std::endl;

        remoteSocket = accept(localSocket, (struct sockaddr *)&remoteAddr, (socklen_t*)&addrLen);  
        
        if (remoteSocket < 0) {
            printf("accept failed!");
            return 0;
        }
                
        //std::cout << "Connection accepted" << std::endl;
	
		int* input = new int(remoteSocket);
		pthread_create(&pid, NULL, serve,(void*)input);
		pthread_detach(pid);

    }
    return 0;
}

