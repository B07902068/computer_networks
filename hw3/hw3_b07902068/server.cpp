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
#include <signal.h>
#include <sys/time.h>

#include "opencv2/opencv.hpp"
#include <pthread.h>



#define ERR_EXIT(a) { perror(a); exit(1); }

#define DATA_SIZE 4096
#define BUFF_SIZE 512

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

int winSize = 1, winThreshold = 16, winBase = 0, next_send = 0, latest_ack = -1;

void sigroutine(int signo) {
	if (signo == SIGALRM) {
		winThreshold = winSize / 2;
		if (winThreshold < 1) {
			winThreshold = 1;
		}
		winSize = 1;

		//next_send = winBase;
		next_send = latest_ack + 1;
		if (next_send >= winBase + winSize) {
			winBase = next_send;
		}

		char s[BUFF_SIZE] = {};
		sprintf(s, "time    out,             threshold = %d\n", winThreshold);
		write(fileno(stdout), s, strlen(s));
	}
	return;
}




int play(int localSocket, struct sockaddr_in agent, char file_name[])
{
	socklen_t agent_size = sizeof(agent);
	int index = 0, segment_size = 0;
	
	deque<segment> sent_queue;
	segment s_tmp;
	
	//receive ack -1 to start
	memset(&s_tmp, 0, sizeof(s_tmp));
	recvfrom(localSocket, &s_tmp, sizeof(s_tmp), 0, NULL, NULL);
	printf("recv     ack #%d\n", s_tmp.head.ackNumber);

	VideoCapture cap(file_name);
	if (!cap.isOpened()) {
		fprintf(stderr, "open fail\n");
		exit(-1);
	}




	Mat imgServer;

	int n = (int)cap.get(CAP_PROP_FRAME_COUNT);
	double FPS = (double)cap.get(CAP_PROP_FPS);
	// get the resolution of the video
	int width = cap.get(CV_CAP_PROP_FRAME_WIDTH);
	int height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);
	//cout << n << ", " << FPS << endl;
	//cout  << width << ", " << height << endl;

	imgServer = Mat::zeros(height, width, CV_8UC3);    
 
 
     // ensure the memory is continuous (for efficiency issue.)
    if(!imgServer.isContinuous()){
         imgServer = imgServer.clone();
    }

	//send resolution seq = 0;
	memset(&s_tmp, 0, sizeof(s_tmp));
	index = 0;
	sprintf(s_tmp.data, "%d %d %f", width, height, FPS);
	s_tmp.head.length = strlen(s_tmp.data);
	s_tmp.head.seqNumber = index;
	sendto(localSocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
	printf("send    data #%d,     winSize = %d\n", index, winSize);
	index++;

	sent_queue.push_back(s_tmp);	

	//signal(SIGALRM, sigroutine);
	struct sigaction act;
	act.sa_handler = sigroutine;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_INTERRUPT;

	sigaction(SIGALRM, &act, NULL);

	struct itimerval value;
	value.it_value.tv_sec = 0;
	value.it_value.tv_usec = 1000;
	value.it_interval.tv_sec = 0;
	value.it_interval.tv_usec = 0;
	//setitimer(ITIMER_REAL, &value, NULL);

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 1000;
	if(setsockopt(localSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		fprintf(stderr, "set socket timeout error\n");
		exit(-1);
	}
	
	/*memset(&s_tmp, 0, sizeof(s_tmp));
	recvfrom(localSocket, &s_tmp, sizeof(s_tmp), 0, NULL, NULL);
	printf("recv     ack #%d\n", s_tmp.head.ackNumber);*/

	int imgSize = width * height * 3;


	next_send = 1;
	//test send 1 frame
	for (int i = 0; i < n; i++) {
		//get a frame from the video to the container on server.
	    cap >> imgServer;
		
		int sent_size = 0, remain = imgSize;

		/*if(index >= 700) {
			exit(-1);
		}*/
		
		while (sent_size < imgSize) {
			//fprintf(stderr, "winbase %d winsize %d latest_ack %d next_send %d\n", winBase, winSize, latest_ack, next_send);
			//if (index >= 400)
				//exit(-1);
			 
			if (next_send < winBase + winSize && next_send < index) {
				for (int i = 0; next_send < winBase + winSize && next_send < index; i++) {
					memcpy(&s_tmp, &sent_queue[i], sizeof(s_tmp));
					s_tmp.head.winBase = winBase;
					s_tmp.head.winSize = winSize;
					sendto(localSocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
					printf("resnd   data #%d,     winSize = %d\n", s_tmp.head.seqNumber, winSize);
					next_send++;
				}
			} else if (index < winBase + winSize) {
				memset(&s_tmp, 0, sizeof(s_tmp));
				s_tmp.head.seqNumber = index;
				s_tmp.head.winBase = winBase;
				s_tmp.head.winSize = winSize;
				if (remain >= DATA_SIZE) {
					memcpy(s_tmp.data, imgServer.data + sent_size, DATA_SIZE);
					s_tmp.head.length = DATA_SIZE;				
				} else {
					memcpy(s_tmp.data, imgServer.data + sent_size, remain);
					s_tmp.head.length = remain;
				}
				sendto(localSocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
				printf("send    data #%d,     winSize = %d\n", index, winSize);
				index++;
				sent_queue.push_back(s_tmp);
				next_send = index;
				sent_size += s_tmp.head.length;
				remain -= s_tmp.head.length;
				if (sent_size == imgSize) {
					break;
				}

			} else {
				memset(&s_tmp, 0, sizeof(s_tmp));
				s_tmp.head.ackNumber = -1;
				value.it_value.tv_sec = 0;
				value.it_value.tv_usec = 1000;
				setitimer(ITIMER_REAL, &value, NULL);
				recvfrom(localSocket, &s_tmp, sizeof(s_tmp), 0, NULL, NULL);
				if (s_tmp.head.ackNumber != -1) {
					printf("recv    ack  #%d\n", s_tmp.head.ackNumber);/////do not print -1 after signal
				}
				if (s_tmp.head.ackNumber == sent_queue.front().head.seqNumber) {
					value.it_value.tv_sec = 0;
					value.it_value.tv_usec = 0;
					setitimer(ITIMER_REAL, &value, NULL);
					sent_queue.pop_front();
					
					latest_ack = s_tmp.head.ackNumber;
				} else if (s_tmp.head.ackNumber > sent_queue.front().head.seqNumber) {
					value.it_value.tv_sec = 0;
					value.it_value.tv_usec = 0;
					setitimer(ITIMER_REAL, &value, NULL);					
					while (s_tmp.head.ackNumber >= sent_queue.front().head.seqNumber) {
						sent_queue.pop_front();
						if (sent_queue.size() == 0) {
							break;
						}
					}
					
					latest_ack = s_tmp.head.ackNumber;
				}
				if (s_tmp.head.ackNumber >= winBase + winSize - 1) { //modify window iff receive all ack of current window 
					winBase = s_tmp.head.ackNumber + 1;
					if (winSize < winThreshold) {
						winSize *= 2;
					} else {
						winSize++;
					}
					if (next_send < winBase) {
						next_send = winBase;
					}
				}

			}
		}
		
	}

	//no new segment to be sent
	while (sent_queue.size() > 0) {
		if (next_send < winBase + winSize && next_send < index) {
			for (int i = 0; next_send < winBase + winSize && next_send < index; i++) {
				memcpy(&s_tmp, &sent_queue[i], sizeof(s_tmp));
				s_tmp.head.winBase = winBase;
				s_tmp.head.winSize = winSize;
				sendto(localSocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
				printf("resnd   data #%d,     winSize = %d\n", s_tmp.head.seqNumber, winSize);
				next_send++;
			}
		} else {
			memset(&s_tmp, 0, sizeof(s_tmp));
			s_tmp.head.ackNumber = -1;
			value.it_value.tv_sec = 0;
			value.it_value.tv_usec = 1000;
			setitimer(ITIMER_REAL, &value, NULL);
			recvfrom(localSocket, &s_tmp, sizeof(s_tmp), 0, NULL, NULL);
			if (s_tmp.head.ackNumber != -1) {
				printf("recv    ack  #%d\n", s_tmp.head.ackNumber);
			}
			if (s_tmp.head.ackNumber == sent_queue.front().head.seqNumber) {
				value.it_value.tv_sec = 0;
				value.it_value.tv_usec = 0;
				setitimer(ITIMER_REAL, &value, NULL);
				sent_queue.pop_front();
				
				latest_ack = s_tmp.head.ackNumber;
			} else if (s_tmp.head.ackNumber > sent_queue.front().head.seqNumber) {
				value.it_value.tv_sec = 0;
				value.it_value.tv_usec = 0;
				setitimer(ITIMER_REAL, &value, NULL);					
				while (s_tmp.head.ackNumber >= sent_queue.front().head.seqNumber) {
					sent_queue.pop_front();
					if (sent_queue.size() == 0) {
						break;
					}
				}
				
				latest_ack = s_tmp.head.ackNumber;
			}
			if (s_tmp.head.ackNumber == winBase + winSize - 1) { //modify window iff receive all ack of current window 
				winBase = s_tmp.head.ackNumber + 1;
				if (winSize < winThreshold) {
					winSize *= 2;
				} else {
					winSize++;
				}
				if (next_send < winBase) {
					next_send = winBase;
				}
			}
		}
	}
	memset(&s_tmp, 0, sizeof(s_tmp));
	s_tmp.head.seqNumber = index;
	s_tmp.head.fin = 1;
	sendto(localSocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
	printf("send    fin\n");

	s_tmp.head.fin = 0;
	while (s_tmp.head.fin == 0) {	
		memset(&s_tmp, 0, sizeof(s_tmp));
		recvfrom(localSocket, &s_tmp, sizeof(s_tmp), 0, NULL, NULL);
	
		//fprintf(stderr, "not fin\n");
	}
	printf("recv    finack\n");
	
	cap.release();
	return 0;
}




int main(int argc, char* argv[]){

	char agent_ip[50], local_ip[50];
	int agent_port, local_port;

	if(argc != 5){
        fprintf(stderr,"用法: %s <agent IP> <agent port> <sender port> <path of source file>\n", argv[0]);
        fprintf(stderr, "例如: ./server local 8888 8887 tmp.mpg\n");
        exit(1);
    } else {
        setIP(agent_ip, argv[1]);
        setIP(local_ip, "local");

        sscanf(argv[2], "%d", &agent_port);
        sscanf(argv[3], "%d", &local_port);

    }
	
	int localSocket;
	struct sockaddr_in sender, agent;
	socklen_t agent_size;

	/*Create UDP socket*/
	localSocket = socket(PF_INET, SOCK_DGRAM, 0);

    /*Configure settings in sender struct*/
    sender.sin_family = AF_INET;
    sender.sin_port = htons(local_port);
    sender.sin_addr.s_addr = inet_addr(local_ip);
    memset(sender.sin_zero, '\0', sizeof(sender.sin_zero));  

    /*Configure settings in agent struct*/
    agent.sin_family = AF_INET;
    agent.sin_port = htons(agent_port);
    agent.sin_addr.s_addr = inet_addr(agent_ip);
    memset(agent.sin_zero, '\0', sizeof(agent.sin_zero));

	/*Initialize size variable to be used later on*/
	agent_size = sizeof(agent);    

    /*bind socket*/
    bind(localSocket,(struct sockaddr *)&sender,sizeof(sender));


	//test for send a messege
	/*segment s_tmp;
	recvfrom(localSocket, &s_tmp, sizeof(s_tmp), 0, NULL, NULL);

	
	memset(&s_tmp, 0, sizeof(s_tmp));
	VideoCapture cap(argv[4]);
	if (!cap.isOpened()) {
		fprintf(stderr, "open fail\n");
		
	} else {
		
		Mat imgServer;

		int n = (int)cap.get(CAP_PROP_FRAME_COUNT);
		double FPS = (double)cap.get(CAP_PROP_FPS);
		cout << n << ", " << FPS << endl;
		
		
		// get the resolution of the video
		int width = cap.get(CV_CAP_PROP_FRAME_WIDTH);
		int height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);
		cout  << width << ", " << height << endl;

		sprintf(s_tmp.data, "%d %d %f", width, height, FPS);
		sendto(localSocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
	}*/

	play(localSocket, agent, argv[4]);

	
	return 0;
}

