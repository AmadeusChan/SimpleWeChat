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
#include <exception>

#include "json.hpp"

#define PORT 8888
#define BUFFER_SIZE 4096

using json = nlohmann::json;

class ClientSession;

class MainControl {
	private:
		static std::deque<ClientSession*> sessions;
		static std::mutex sessMtx, pwdMtx, profileMtx, histMtx;
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
		}

		static bool checkLogin(std::string name_, std::string pwd_) {
			bool flag = false;

			pwdMtx.lock();
			std::ifstream f("users.json");
			json j;
			f>>j;
			f.close();
			pwdMtx.unlock();

			int len = j.size();
			for (int i=0; i<len; ++i) {
				std::string name = j[i][0];
				std::string pwd = j[i][1];
				if (name.compare(name_) == 0 && pwd.compare(pwd_) == 0) {
					flag = true;
					break;
				}
			}

			return flag;
		}

		static std::string getAllUser() {
			pwdMtx.lock();
			std::ifstream fin("users.json");
			json j;
			fin>>j;
			fin.close();
			pwdMtx.unlock();

			int len = j.size();
			json users;
			for (int i=0; i<len; ++i) {
				users.push_back(j[i][0]);
			}
			std::cout<<users<<std::endl;

			json response;
			response["content"] = users;
			response["type"] = "get_all_users_ack";

			return response.dump();
		}

		static std::string getAllFriends(std::string name_) {
			profileMtx.lock();
			std::ifstream fin("profile.json");
			json j;
			fin>>j;
			fin.close();
			profileMtx.unlock();

			printf("getting all users\n");
			json response;
			response["content"] = j[name_]["friends"];
			response["type"] = "get_all_friends_ack";


			return response.dump();
		}

		static bool addFriends(std::string name_, std::string friend_) {
			bool flag = true;

			profileMtx.lock();
			std::ifstream fin("profile.json");
			json j;
			fin>>j;
			fin.close();
			profileMtx.unlock();

			try {
				json k = j[name_]["friends"];
				int len = k.size();
				for (int i=0; i<len; ++i) {
					std::string fri = k[i];
					if (fri.compare(friend_) == 0) {
						flag = false;
						break;
					}
				}
			} catch (std::exception e) {
				flag = false;
			}

			if (flag) {
				j[name_]["friends"].push_back(friend_);
				j[friend_]["friends"].push_back(name_);
				profileMtx.lock();
				std::ofstream fout("profile.json");
				fout<<j;
				fout.close();
				profileMtx.unlock();
			}

			return flag;
		}

		static void sendMessage(std::string sender_, std::string receiver_, std::string msg_) {
			histMtx.lock();

			std::ifstream fin("history.json");
			json j;
			fin>>j;
			fin.close();

			json record;
			record["sender"] = sender_;
			record["receiver"] = receiver_;
			record["msg"] = msg_;
			record["isRead"] = false;

			j[receiver_].push_back(record);
			
			json record_;
			record_["sender"] = sender_;
			record_["receiver"] = receiver_;
			record_["msg"] = msg_;
			record_["isRead"] = true;
			j[sender_].push_back(record_);

			std::ofstream fout("history.json");
			fout<<j;
			fout.close();

			histMtx.unlock();
		}

		static std::string getAllMessage(std::string name_, bool unreadOnly_) {
			json allMsg;

			histMtx.lock();
			std::ifstream fin("history.json");
			json j;
			fin>>j;
			fin.close();
			histMtx.unlock();

			int len = j[name_].size();
			for (int i=0; i<len; ++i) {
				if (unreadOnly_ == false || j[name_][i]["isRead"] == false) {
					allMsg.push_back(j[name_][i]);
				}
				j[name_][i]["isRead"] = true;
			}

			histMtx.lock();
			std::ofstream fout("history.json");
			fout<<j;
			fout.close();
			histMtx.unlock();

			return allMsg.dump();
		}


};

std::deque<ClientSession*> MainControl::sessions;
std::mutex MainControl::sessMtx;
std::mutex MainControl::pwdMtx;
std::mutex MainControl::profileMtx; 
std::mutex MainControl::histMtx;

class SessionEvent {

	public: 
		static const int READ_EVENT, WRITE_EVENT;

		SessionEvent(int type_, const char *content_, int size_) {
			type = type_;
			size = size_ + 1;
			content = new char[size_ + 2];
			memcpy(content, content_, size_);
			if (type_ == WRITE_EVENT) {
				content[size_] = ';';
				content[size_ + 1] = 0;
				printf("string to be sent: %s\n", content);
			}
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
		char temp[BUFFER_SIZE];
		char buffer[BUFFER_SIZE];
		bool isLogin;
		char userName[BUFFER_SIZE];

		bool isKilled;
		std::deque<SessionEvent> *writeEvent;
		std::deque<SessionEvent> *readEvent;

		std::mutex *killedMtx, *writeEventMtx, *readEventMtx, *logMtx, *loginMtx;

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
			loginMtx = new std::mutex();

			printLog("constructing");
		}

		bool getLogin() {
			//return true;
			bool flag;
			loginMtx->lock();
			flag = isLogin;
			loginMtx->unlock();
			return flag;
		}
		void setLogin(bool flag) {
			loginMtx->lock();
			isLogin = flag;
			loginMtx->unlock();
		}

		void writeString(const char *content, int len) {
			writeEventMtx->lock();
			writeEvent->push_back(SessionEvent(SessionEvent::WRITE_EVENT, content, len));
			writeEventMtx->unlock();
		}

		void writeString(std::string str) {
			writeString(str.c_str(), str.size());
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
			try {
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
				} else if (type.compare("login") == 0) {
					bool flag = MainControl::checkLogin(content["name"], content["pwd"]);
					if (flag) {
						char response[32] = "{\"type\": \"login_ack\"}";
						writeString(response, strlen(response));
					} else {
						char response[32] = "{\"type\": \"login_nak\"}";
						writeString(response, strlen(response));
					}
					setLogin(flag);
				} else if (getLogin()) {
					if (type.compare("getAllUsers") == 0) {
						std::string users = MainControl::getAllUser();
						writeString(users.c_str(), users.size());
					} else if (type.compare("getAllFriends") == 0) {
						std::string friends = MainControl::getAllFriends(content["name"]);
						writeString(friends);
					} else if (type.compare("addFriend") == 0) {
						std::string name = content["name"];
						std::string fri = content["friend"];
						bool flag = MainControl::addFriends(name, fri);
						json response;
						if (flag) {
							response["type"] = "add_friend_ack";
						} else {
							response["type"] = "add_friend_nak";
						}
						writeString(response.dump());
					} else if (type.compare("sendMessage") == 0) {
						std::string sender = content["sender"];
						std::string receiver = content["receiver"];
						std::string msg = content["msg"];
						MainControl::sendMessage(sender, receiver, msg);
					} else if (type.compare("getAllMessage") == 0) {
						std::string name = content["name"];
						bool unreadOnly = content["unreadOnly"];
						writeString(MainControl::getAllMessage(name, unreadOnly));
					}
				}
			} catch (std::exception& e) {
				printLog("not valid msg");
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

				bzero(buffer, BUFFER_SIZE);
				int n = read(sockfd, buffer, BUFFER_SIZE);
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
							temp[tempCnt] = 0;
							saveReadString(temp, tempCnt);
							//writeString(temp, tempCnt);
							tempCnt = 0;
							bzero(temp, BUFFER_SIZE);
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
		static char buffer[BUFFER_SIZE];

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
char NetworkManager:: buffer[BUFFER_SIZE];

int main() {
	NetworkManager::init();
	return 0;
}
