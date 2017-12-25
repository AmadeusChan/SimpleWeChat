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
#include <string>
#include <fstream>

#include "json.hpp"

#define PORT 8888

using json = nlohmann::json;

class ClientSession;

class MainControl {
	private:
		static std::deque<ClientSession*> sessions;
		static std::mutex sessMtx, pwdMtx;
	public: 
		static void registerSession(ClientSession *sess) {
			sessMtx.lock();
			sessions.push_back(sess);
			sessMtx.unlock();
		}

		static bool registerUser(std::string name_, std::string pwd_) {
			pwdMtx.lock();

			std::ifstream f("users.json");
			json j;
			f>>j;
			f.close();

			int len = j.size();
			bool flag = true;
			std::string name, pwd;

			for (int i=0; i<len; ++i) {
				name = j[i][0];
				pwd = j[i][1];
				if (name.compare(name_) == 0) {
					flag = false;
					break;
				}
			}

			if (flag) {
				json k;
				k[0] = name_;
				k[1] = pwd_;
				j.push_back(k);
				std::ofstream of("users.json");
				of<<j;
				of.close();
			}

			pwdMtx.unlock();
			return flag;

			/*
			FILE * pFile = fopen("users.txt", "r");
			char theName[64], thePwd[64];
			bool flag = true;
			while (fscanf(pFile, "%s", theName) != EOF) {
				fscanf(pFile, "%s", thePwd);
				if (strcmp(name, theName) == 0) {
					flag = false;
					break;
				}
			}
			fclose(pFile);
			if (flag) {
				pFile = fopen("users.txt", "a+");
				fprintf(pFile, "%s\n%s\n", theName, thePwd);
				fclose(pFile);
			}
			pwdMtx.unlock();
			return flag;
			*/
		}

		static bool checkLogin(char *name, char *pwd) {
			FILE * pFile = fopen("users.txt", "r");
			char theName[64], thePwd[64];
			bool flag = false;
			while (fscanf(pFile, "%s", theName) != EOF) {
				fscanf(pFile, "%s", thePwd);
				if (strcmp(name, theName) == 0 && strcmp(pwd, thePwd) == 0) {
					flag = true;
					break;
				}
			}
			fclose(pFile);
			return flag;
		}

		static char* getAllUser() {
			FILE * pFile = fopen("users.txt", "r");
			char theName[64], thePwd[64];
			char *users = new char[1024];
			int cnt = 0;
			while (fscanf(pFile, "%s", theName) != EOF) {
				fscanf(pFile, "%s", thePwd);
				memcpy(users + cnt, theName, strlen(theName));
				cnt += strlen(theName);
				users[cnt++] = ' ';
			}
			fclose(pFile);
			return users;
		}

		static char* getAllFriends(char *name) {
			FILE * pFile = fopen("friends.txt", "r");
			char theName[64], theB[64];
			char *fri = new char[1024];
			int cnt = 0;
			while (fscanf(pFile, "%s", theName) != EOF) {
				fscanf(pFile, "%s", theB);
				if (strcmp(theName, name) == 0) {
					memcpy(fri + cnt, theB, strlen(theB));
					cnt += strlen(theB);
					fri[cnt++] = ' ';
				} else if (strcmp(theB, name) == 0) {
					memcpy(fri + cnt, theName, strlen(theName));
					cnt += strlen(theName);
					fri[cnt++] = ' ';
				}
			}
			return fri;
		}

		static char* getAllMessage(char *name, bool notReadOnly) {
			FILE * pFile = fopen("message.txt", "r");
			char sender[64], receiver[64], msg[64];
			char *val = new char[4096];
			int cnt = 0;
			while (fscanf(pFile, "%s", sender) != EOF) {
				fscanf(pFile, "%s", receiver);
				fscanf(pFile, "%s", msg);
				memcpy(val + cnt, sender, strlen(sender));
				val[cnt++] = ' ';
				memcpy(val + cnt, msg, strlen(msg));
				val[cnt++] = ' ';
			}
			return val;
		}


};

std::deque<ClientSession*> MainControl::sessions;
std::mutex MainControl::sessMtx;
std::mutex MainControl::pwdMtx;

class SessionEvent {

	public: 
		static const int READ_EVENT, WRITE_EVENT;

		SessionEvent(int type_, char *content_, int size_) {
			type = type_;
			size = size_ + 1;
			content = new char[size_ + 2];
			memcpy(content, content_, size_);
			if (type_ == WRITE_EVENT) content[size_] = ';';
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
		char userName[64];

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
			userName[0] = 0;

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

		/*
		char *splitBySpace(char *word) {
			int len = strlne(word);
			for (int i=0; i<len; ++i) {
				if 
			}
		}
		*/

		void processWord(char *word_, int len) {
			printLog("string received");
			for (int i=0; i<len; ++i) {
				putchar(word_[i]);
			}
			putchar('\n');
			word_[len] = 0;
			std::string word = std::string(word_);
			std::cout<<word<<std::endl;
			json j = json::parse(word);
			json content = j["content"];
			std::string type = j["type"];
			if (type.compare("register") == 0) {
				bool flag = MainControl::registerUser(content["name"], content["pwd"]);
				if (flag) {
					char response[20] = "{\"type\": \"reg_ack\"}";
					writeString(response, strlen(response));
				} else {
					char response[20] = "{\"type\": \"reg_nak\"}";
					writeString(response, strlen(response));
				}
			}
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
					delete[] event.content;
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
					delete[] event.content;
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
				std::this_thread::sleep_for(std::chrono::milliseconds(rand()%100));
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
