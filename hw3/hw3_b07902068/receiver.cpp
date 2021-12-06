/*extern "C" { 
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>

#include <stdarg.h>
#include <inttypes.h>
#include <iostream>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h> 

#include <sys/stat.h>
#include <sys/types.h>

#include "opencv2/opencv.hpp"
#include <pthread.h>
#include <signal.h>


#define ERR_EXIT(a) { perror(a); exit(1); }

#define DATA_SIZE 4096
#define BUFF_SIZE 512
#define QUEUE_SIZE 32

using namespace std;
using namespace cv;


typedef struct {
	int length;
	int seqNumber;
	int ackNumber;
	int fin;
	int syn;
	int ack;
	int winBase;
	int winSize;
} header;

typedef struct{
	header head;
	char data[DATA_SIZE];
} segment;

void setIP(char *dst, char *src) {
    if(strcmp(src, "0.0.0.0") == 0 || strcmp(src, "local") == 0
            || strcmp(src, "localhost")) {
        sscanf("127.0.0.1", "%s", dst);
    } else {
        sscanf(src, "%s", dst);
    }
}

int parse_cmd(char command[], char file_name[]) 
{
	int cmd = 0;
	char *ptr = strchr(command, ' ');
	
	char temp[512] = {};

	

	if (ptr != NULL) {
		sscanf(command, "%s", temp);
		sscanf(ptr, "%s", file_name);
	} else {
		cmd = -1;
		return cmd;
	}
	if(strlen(file_name) == 0) {
		cmd = -1;		
		return cmd;	
	}
	
	if (strcmp(temp, "play") == 0) {
		if(strstr(file_name, ".mpg") == NULL) {
			cmd = -2;
		}
		
	} else {
		cmd= -1;
	}	
	
	return cmd;
}

int play(int localSocket, struct sockaddr_in agent)
{
	socklen_t agent_size = sizeof(agent);
	int index = -1;
	deque<segment> recved_queue;

	//tell server to start
	segment s_tmp;
	memset(&s_tmp, 0, sizeof(s_tmp));
	s_tmp.head.ack = 1;
	s_tmp.head.ackNumber = -1;
	sendto(localSocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
	index = s_tmp.head.ackNumber;
	printf("send     ack  #%d\n", index); 


	//receive width, height, fps
	memset(&s_tmp, 0, sizeof(s_tmp));
	while (index != 0) {
		recvfrom(localSocket, &s_tmp, sizeof(s_tmp), 0, NULL, NULL);////need to deal data been drop
		index = s_tmp.head.seqNumber;
		if (index == 0) { 
			printf("recv     data #%d\n", index);
		} else {
			printf("drop     data #%d\n", s_tmp.head.seqNumber);
		}
	}

	//fprintf(stderr, "%s\n", s_tmp.data);
	int width, height;
	double FPS;
	sscanf(s_tmp.data, "%d %d %lf", &width, &height, &FPS);
	//fprintf(stderr, "%d %d %f\n", width, height, FPS);
 

	memset(&s_tmp, 0, sizeof(s_tmp));
	s_tmp.head.ack = 1;
	s_tmp.head.ackNumber = index;///0
	sendto(localSocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
	printf("send     ack  #%d\n", index); 

    Mat imgClient;
    
    //allocate container to load frames 
    imgClient = Mat::zeros(height, width, CV_8UC3);
 
 

    if(!imgClient.isContinuous()){
         imgClient = imgClient.clone();
    }
	int imgSize = width * height * 3;
	int fin = 0;
	//test receive 1 frame
	int recved_size = 0;

	int segment_size;
	int window_index = 0, winBase, winSize;
	while (fin == 0) {
		memset(&s_tmp, 0, sizeof(s_tmp));
		segment_size = recvfrom(localSocket, &s_tmp, sizeof(s_tmp), 0, NULL, NULL);
		if (segment_size <= 0) {
			fprintf(stderr, "no receive\n");			
			continue;
		}
		if (segment_size < sizeof(s_tmp)) {
			fprintf(stderr, "smaller size %d %lu\n", segment_size, sizeof(s_tmp));			
		}

		/*if(s_tmp.head.seqNumber == 382) {
			exit(-1);
		}*/

		
		fin = s_tmp.head.fin;

		if (fin == 1) {//no need to deal drop before fin, fin is after all ack
			printf("recv     fin\n");
			memset(&s_tmp, 0, sizeof(s_tmp));
			s_tmp.head.ack = 1;
			s_tmp.head.fin = 1;
			s_tmp.head.ackNumber = index;
			sendto(localSocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
			printf("send     finack\n");
		} else {
			if (s_tmp.head.seqNumber == window_index + 1 || s_tmp.head.seqNumber == index + 1) {
				window_index = s_tmp.head.seqNumber;
				winBase = s_tmp.head.winBase;
				winSize = s_tmp.head.winSize;
			}
			//fprintf(stderr, "wi %d wb %d ws %d q_size %lu\n", window_index, winBase, winSize, recved_queue.size());
			if (recved_queue.size() < QUEUE_SIZE && s_tmp.head.seqNumber == index + 1) {
				recved_queue.push_back(s_tmp);
				index = s_tmp.head.seqNumber;
				printf("recv     data #%d\n", index);
				memset(&s_tmp, 0, sizeof(s_tmp));
				s_tmp.head.ack = 1;
				s_tmp.head.ackNumber = index;
				sendto(localSocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
				printf("send     ack  #%d\n", index);
			} else {
				printf("drop     data #%d\n", s_tmp.head.seqNumber);
				memset(&s_tmp, 0, sizeof(s_tmp));
				s_tmp.head.ack = 1;
				s_tmp.head.ackNumber = index;
				sendto(localSocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
				printf("send     ack  #%d\n", index);
			}
		}

		if ((window_index == winBase + winSize - 1 && recved_queue.size() >= QUEUE_SIZE) || fin == 1) {
			if (recved_queue.size() > 0) {
				printf("flush\n");
			}
			while (recved_queue.size() > 0) {
				memcpy(imgClient.data + recved_size, recved_queue.front().data, recved_queue.front().head.length);
				recved_size += recved_queue.front().head.length;
				recved_queue.pop_front();
				//fprintf(stderr, "%d %d\n", recved_size, imgSize);
				if(recved_size == imgSize) {
					imshow("Video", imgClient);
					
					// notice: this part is necessary due to openCV's design.
					// waitKey means a delay to get the next frame.
					char c = (char)waitKey(1000/FPS);
					//char c = (char)waitKey(0);
					recved_size = 0;
					//fprintf(stderr, "show\n");
				}
			}
			window_index = index;
		}
	}

	/*memset(&s_tmp, 0, sizeof(s_tmp));
	s_tmp.head.ack = 1;
	s_tmp.head.fin = 1;
	s_tmp.head.ackNumber = index;
	sendto(localSocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
	printf("send     finack\n");*/



	
	destroyAllWindows();
	return 1;
}



int main(int argc , char *argv[])
{
	char agent_ip[50], local_ip[50];
	int agent_port, local_port;

	if(argc != 4){
        fprintf(stderr,"用法: %s <agent IP> <agent port> <receiver port>\n", argv[0]);
        fprintf(stderr, "例如: ./receiver local 8888 8889\n");
        exit(1);
    } else {
        setIP(agent_ip, argv[1]);
        setIP(local_ip, "local");

        sscanf(argv[2], "%d", &agent_port);
        sscanf(argv[3], "%d", &local_port);

    }

	int localSocket;
	struct sockaddr_in receiver, agent;
	socklen_t agent_size;

	/*Create UDP socket*/
	localSocket = socket(PF_INET, SOCK_DGRAM, 0);

    /*Configure settings in sender struct*/
    receiver.sin_family = AF_INET;
    receiver.sin_port = htons(local_port);
    receiver.sin_addr.s_addr = inet_addr(local_ip);
    memset(receiver.sin_zero, '\0', sizeof(receiver.sin_zero));  

    /*Configure settings in agent struct*/
    agent.sin_family = AF_INET;
    agent.sin_port = htons(agent_port);
    agent.sin_addr.s_addr = inet_addr(agent_ip);
    memset(agent.sin_zero, '\0', sizeof(agent.sin_zero));

	/*Initialize size variable to be used later on*/
	agent_size = sizeof(agent);    

    /*bind socket*/
    bind(localSocket,(struct sockaddr *)&receiver,sizeof(receiver));
	
	//test for send a messege
	/*segment s_tmp;
	memset(&s_tmp, 0, sizeof(s_tmp));
	recvfrom(localSocket, &s_tmp, sizeof(s_tmp), 0, NULL, NULL);
	fprintf(stderr, "%s\n", s_tmp.data);
	int width, height;
	double FPS;
	sscanf(s_tmp.data, "%d %d %lf", &width, &height, &FPS);
	fprintf(stderr, "%d %d %f\n", width, height, FPS);*/

	char command[BUFF_SIZE];
	while (1) {
		fprintf(stdout, "$ ");
		fgets(command, BUFF_SIZE, stdin);
		command[4] = '\0';
		if (strcmp("play", command) != 0) {
			fprintf(stdout, "Command format error.\n");
			continue;
		}
		break;
	}
	play(localSocket, agent);


	
	return 0;
	

}
