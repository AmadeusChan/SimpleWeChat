#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

#include <algorithm>
#include <iostream>
#include <deque>
#include <thread>
#include <mutex>

#define PORT 8888


class MainControl {
	public: 
};


class SessionEvent {

	public: 
		static const int READ_EVENT, WRITE_EVENT;

		SessionEvent(int type_, char *content_, int size_) {
			type = type_;
			size = size_;
			content = new char[size_ + 1];
			memcpy(content, content_, size_);
		}

		int type;
		char *content;
		int size;

};

const int SessionEvent::READ_EVENT = 0;
const int SessionEvent::WRITE_EVENT = 1;

class ClientSession {
	private:
		int sockfd;
		int id;
		char temp[512];
		char buffer[256];
		bool isLogin;

		bool isKilled;
		std::deque<SessionEvent> *writeEvent;
		std::deque<SessionEvent> *readEvent;

		std::mutex *killedMtx, *writeEventMtx, *readEventMtx, *logMtx;

		//std::mutex killedMtx, writeEventMtx, readEventMtx;
		
		void printLog(const char *msg) {
			logMtx->lock();
			printf("log from session %d: %s\n", id, msg);
			logMtx->unlock();
		}
	public:
		ClientSession(int sockfd_, int id_) {
			sockfd = sockfd_;
			id = id_;
			isLogin = false;
			writeEvent = new std::deque<SessionEvent>();
			readEvent = new std::deque<SessionEvent>();
			isKilled = false;

			killedMtx = new std::mutex();
			writeEventMtx = new std::mutex();
			readEventMtx = new std::mutex();
			logMtx = new std::mutex();

			printLog("constructing");
		}

		void writeString(char *content, int len) {
			writeEventMtx->lock();
			writeEvent->push_back(SessionEvent(SessionEvent::WRITE_EVENT, content, len));
			writeEventMtx->unlock();
		}

		void saveReadString(char *content, int len) {
			printLog("saveReadString");
			readEventMtx->lock();
			readEvent->push_back(SessionEvent(SessionEvent::READ_EVENT, content, len));
			//std::cout<<readEvent->size()<<std::endl;
			readEventMtx->unlock();
		}

		void processWord(char *word, int len) {
			printLog("string received");
			for (int i=0; i<len; ++i) {
				putchar(word[i]);
			}
			putchar('\n');
		}

		void startReading() {
			// `: esc
			// ;: flag
			printLog("reading from client");
			int tempCnt = 0;
			bool isEsc = false;
			char flag = ';';
			char esc = '`';
			while (1) {
				killedMtx->lock();
				if (isKilled) {
					killedMtx->unlock();
					break;
				}
				killedMtx->unlock();

				bzero(buffer, 256);
				int n = read(sockfd, buffer, 255);
				if (n < 0) {
					printLog("ERROR reading from socket");
					break;
				}
				for (int i=0; i<n; ++i) {
					//printf("%c", buffer[i]);
					if ((buffer[i] != flag && buffer[i] != esc) || isEsc) {
						isEsc = false;
						temp[tempCnt++] = buffer[i];
					} else {
						if (buffer[i] == flag) {
							//processWord(temp, tempCnt);
							saveReadString(temp, tempCnt);
							//writeString(temp, tempCnt);
							tempCnt = 0;
						} else {
							isEsc = true;
						}
					}
				}
			}
		} 
		void start() { // main loop fun
			printLog("start main loop");
			while (1) {
				/*
				std::cout<<"writing"<<std::endl;
				std::this_thread::sleep_for(std::chrono::milliseconds(3000));
				*/
				killedMtx->lock();
				if (isKilled) {
					killedMtx->unlock();
					break;
				}
				killedMtx->unlock();

				writeEventMtx->lock();
				if (writeEvent->size()>0) { // do some writing stuff
					SessionEvent event = writeEvent->front();
					writeEvent->pop_front();
					int n = write(sockfd, event.content, event.size);
					if (n < 0) {
						printLog("error when writing to socket!");
					}
					writeEventMtx->unlock();
				} else {
					writeEventMtx->unlock();
				}

				readEventMtx->lock();
				//std::cout<<readEvent->size()<<std::endl;
				//std::this_thread::sleep_for(std::chrono::milliseconds(rand()%10000));
				if (readEvent->size()>0) { // process the read string
					//printLog("processing saved string");
					SessionEvent event = readEvent->front();
					readEvent->pop_front();
					processWord(event.content, event.size);
					readEventMtx->unlock();
				} else {
					//printLog("no saved string");
					readEventMtx->unlock();
				}
			}
		}
};

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

int main()
{
    int PORT_NO = 8888;
    //char serverAddress[20] = "111.231.223.231";
    char serverAddress[20] = "127.0.0.1";


    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[256];
    /*
    if (argc < 3) {
       fprintf(stderr,"usage %s hostname port\n", argv[0]);
       exit(0);
    }
    */

    portno = PORT_NO;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");
    server = gethostbyname(serverAddress);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 
        error("ERROR connecting");

    ClientSession *sess = new ClientSession(sockfd, 0);
    std::thread *sessThread = new std::thread();
    *sessThread = std::thread(&ClientSession::startReading, *sess);
    sessThread = new std::thread();
    *sessThread = std::thread(&ClientSession::start, *sess);

    while (1) {
    	//printf("Please enter the message: ");
    	bzero(buffer,256);
    	fgets(buffer,255,stdin);
	sess->writeString(buffer, strlen(buffer)-1);
	/*
    	n = write(sockfd,buffer,strlen(buffer)-1);
    	if (n < 0) 
    	     error("ERROR writing to socket");
	*/
    	//printf("write succ\n");
	/*
	bzero(buffer, 256);
	n = read(sockfd, buffer, 255);
	if (n>0) {
		printf("msg received: %s\n", buffer);
	}
	*/
    }

    bzero(buffer,256);
    n = read(sockfd,buffer,255);
    if (n < 0) 
         error("ERROR reading from socket");
    printf("%s\n",buffer);
    close(sockfd);
    return 0;
}
