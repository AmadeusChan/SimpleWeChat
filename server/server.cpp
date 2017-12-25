/* A simple server in the internet domain using TCP
   The port number is passed as an argument */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include <algorithm>
#include <iostream>
#include <deque>
#include <thread>
#include <mutex>

#define PORT 8888


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
							writeString(temp, tempCnt);
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

class NetworkManager {
	private:
		static int port;
		static char buffer[256];

		static void error(const char *msg) {
		    perror(msg);
		    exit(1);
		}

	public:
		static void init() {
			port = PORT;
			int sockfd, newsockfd;
			socklen_t clilen;
			struct sockaddr_in serv_addr, cli_addr;
			sockfd = socket(AF_INET, SOCK_STREAM, 0);
			if (sockfd < 0) {
				error("ERROR opening socket");
				return ;
			}
			bzero((char *) &serv_addr, sizeof(serv_addr));
			serv_addr.sin_family = AF_INET;
			serv_addr.sin_addr.s_addr = INADDR_ANY;
			serv_addr.sin_port = htons(port);
			if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
				error("ERROR on binding");
				return ;
			}
			listen(sockfd,5);
			clilen = sizeof(cli_addr);
			int sessCnt = 0;
			while (1) {
				printf("waiting for new connection...\n");
				newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
				if (newsockfd < 0) {
					error("ERROR on accept");
					return ;
				}
				ClientSession *sess = new ClientSession(newsockfd, sessCnt++);
				std::thread *sessThread = new std::thread();
				*sessThread = std::thread(&ClientSession::startReading, *sess);
				sessThread = new std::thread();
				*sessThread = std::thread(&ClientSession::start, *sess);
			}
			//readFromNewSocket(newsockfd);
			/*
			bzero(buffer,256);
			n = read(newsockfd,buffer,255);
			if (n < 0) {
				error("ERROR reading from socket");
				return ;
			}
			printf("Here is the message: %s\n",buffer);
			n = write(newsockfd,"I got your message",18);
			if (n < 0) {
				error("ERROR writing to socket");
				return ;
			}
			close(newsockfd);
			*/
			close(sockfd);
		}
};

int NetworkManager:: port;
char NetworkManager:: buffer[256];

int main() {
	NetworkManager::init();
	return 0;
}
