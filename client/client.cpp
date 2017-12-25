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
#include <vector>
#include <exception>

#include "json.hpp"

#define PORT 8888
#define ADDRESS "127.0.0.1"

#define BUFFER_SIZE 4096

using json = nlohmann::json;

class ClientSession;

std::vector<std::string> split(std::string str_, char sep_) {
	std::vector<std::string> vec;
	std::string *temp = new std::string();
	int len = str_.size();
	for (int i=0; i<len; ++i) {
		if (str_[i] == sep_) {
			vec.push_back(*temp);
			temp = new std::string();
		} else {
			temp->push_back(str_[i]);
		}
	}
	vec.push_back(*temp);
	return vec;
}

class MainDisplay {
	public:
		static std::mutex msgMtx;
		static void msg(std::string msg_) {
			msgMtx.lock();
			std::cout<<msg_;
			msgMtx.unlock();
		}
		static void showWelcome() {
			msg("welcome to SimpleWechat app.\n");
		}
		static void showHint() {
			msg("please input new command. input help for more details. >");
		}
		static std::string waitingForCommand() {
			showHint();
			std::string str;
			std::getline(std::cin, str);
			msg("\n");
			return str;
		}
		static void showHelp() {
			msg("register [user name] [password]: register your account.");
			msg("login [user name] [password]: login with given username and password\n");
			msg("search: show all users.\n");
			msg("add [user name]: add [user name] to your friend list\n");
			msg("ls: show all friends\n");
			msg("char [user name]: chat to [user name]\n");
			msg("recvmsg: receive all unread message\n");
			msg("recvfile: receive the most recent undownloaded file to ~/Downloads.\n");
			msg("profile: show profile of current user.\n");
			msg("sync: synchronze profile of all friends\n");
			msg("\n");
		}
		static void showInvalidCommand() {
			msg("invalid command! the following instructions should help.\n");
			showHelp();
		}
		static void showNotLoginWarnning() {
			msg("you haven't login yet. please login to your own account.\n");
		}
		static void showMessage(std::string sender_, std::string receiver_, std::string msg_) {
			msg(sender_);
			msg(" said to ");
			msg(receiver_);
			msg(": ");
			msg(msg_);
			msg("\n");
		}
};

std::mutex MainDisplay::msgMtx;

class MainControl {
	public: 
		static const int FORMAL_STATE, CHATTING_STATE;
		static ClientSession* sess;
		static int sockfd;
		static bool isLogin;

		static std::mutex loginMtx;

		static void init();
		static void mainLoop();
		static bool getLogin();
		static void setLogin(bool flag_);
		static void registerUser(std::string name_, std::string pwd_);
		static void registerSuccess();
		static void registerFailed();
		/*
		static bool login(std::string name_, std::string pwd);
		static json getAllUsers();
		static json getAllFriends(std::string name_);
		static bool addFriend(std::string name_, std::string friend_);
		*/
};

class SessionEvent {

	public: 
		static const int READ_EVENT, WRITE_EVENT;

		SessionEvent(int type_, const char *content_, int size_) {
			type = type_;
			size = size_;
			if (type_ == WRITE_EVENT) {
				content = new char[size_*2+3];
				int actual_size = 0;
				char flag = ';';
				char esc = '`';
				for (int i=0; i<size_; ++i) {
					if (content_[i] == flag || content_[i] == esc) {
						content[actual_size++] = esc;
					} 
					content[actual_size++] = content_[i];
				}
				content[actual_size++] = flag;
				content[actual_size] = 0;
				/*
				printf("creating write event: %s\n", content);
				*/
				size = actual_size;
			} else {
				content = new char[size_ + 2];
				memcpy(content, content_, size_);
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

		bool isKilled;
		std::deque<SessionEvent> *writeEvent;
		std::deque<SessionEvent> *readEvent;

		std::mutex *killedMtx, *writeEventMtx, *readEventMtx, *logMtx;

		//std::mutex killedMtx, writeEventMtx, readEventMtx;
		
		void printLog(const char *msg) {
			return ;
			MainDisplay::msg("log from session " + std::to_string(id) + ": " + std::string(msg) + "\n");
			MainDisplay::showHint();
			/*
			logMtx->lock();
			printf("log from session %d: %s\n", id, msg);
			logMtx->unlock();
			*/
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

		void terminate() {
			killedMtx->lock();
			isKilled = true;
			killedMtx->unlock();
		}

		void writeString(const char *content, int len) {
			writeEventMtx->lock();
			char buffer[BUFFER_SIZE];
			bzero(buffer, BUFFER_SIZE);
			for (int i=0; i<len; ++i) {
				buffer[i] = content[i];
			}
			buffer[len] = 0;
			std::cout<<buffer<<std::endl;
			writeEvent->push_back(SessionEvent(SessionEvent::WRITE_EVENT, buffer, len));
			writeEventMtx->unlock();
		}
		void writeString(std::string str) {
			std::cout<<str<<std::endl;
			writeString(str.c_str(), str.size());
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
			/*
			for (int i=0; i<len; ++i) {
				putchar(word[i]);
			}
			putchar('\n');
			*/
			word[len] = 0;
			//MainDisplay::msg(std::string(word) + "\n");
			try {
				json j = json::parse(std::string(word));
				std::string type = j["type"];
				if (type.compare("reg_ack") == 0) {
					MainControl::registerSuccess();
				} else if (type.compare("reg_nak") == 0) {
					MainControl::registerFailed();
				}
			} catch (std::exception e) {
				printLog("invalid msg");
				MainDisplay::msg("something went wrong. maybe your input is invalid or msg is too long, etc");
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

const int MainControl::FORMAL_STATE=0;
const int MainControl::CHATTING_STATE=1;
ClientSession* MainControl::sess;
int MainControl::sockfd;
bool MainControl::isLogin;
std::mutex MainControl::loginMtx;

bool MainControl::getLogin() {
	bool flag;
	loginMtx.lock();
	flag = isLogin;
	loginMtx.unlock();
	return flag;
}

void MainControl::setLogin(bool flag_){
	loginMtx.lock();
	isLogin = flag_;
	loginMtx.unlock();
}

void MainControl::registerSuccess() {
	//MainDisplay::msg("register sucess! please input new command.>");
	MainDisplay::msg("register sucess!\n");
}

void MainControl::registerFailed() {
	//MainDisplay::msg("register failed!  please input new command.>");
	MainDisplay::msg("register failed!\n");
}

void MainControl::mainLoop() {
	MainDisplay::showWelcome();
	int state = FORMAL_STATE;
	bool isLogin = false;
	std::string userName;
	while (1) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		std::string str = MainDisplay::waitingForCommand();
		std::vector<std::string> args = split(str, ' ');
		if (state == FORMAL_STATE) {
			if (args[0].compare("register") == 0) {
				MainControl::registerUser(args[1], args[2]);
			} else if (args[0].compare("login") == 0) {
			} else if (args[0].compare("help") == 0) {
				MainDisplay::showHelp();
			} else if (args[0].compare("exit") == 0) {
				break;
			} else if (isLogin) {
				if (args[0].compare("search") == 0) {
				} else if (args[0].compare("add") == 0) {
				} else if (args[0].compare("ls") == 0) {
				} else if (args[0].compare("chat") == 0) {
				} else if (args[0].compare("recvmsg") == 0) {
				} else if (args[0].compare("recvfile") == 0) {
				} else if (args[0].compare("profile") == 0) {
				} else if (args[0].compare("sync") == 0) {
				} else {
					MainDisplay::showInvalidCommand();
				}
			} else {
				MainDisplay::showNotLoginWarnning();
			}
		} else {
			if (args[0].compare("sendmsg") == 0) {
			} else if (args[0].compare("senfile") == 0) {
			} else if (args[0].compare("exit") == 0) {
			} else {
				MainDisplay::showInvalidCommand();
			}
		}
		sess->terminate();
	}
}

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

void MainControl::registerUser(std::string name_, std::string pwd_) {
	json j;
	j["type"] = "register";
	json content;
	content["name"] = name_;
	content["pwd"] = pwd_;
	j["content"] = content;
	sess->writeString(j.dump());
}

/*
static void registerUser(std::string name_, std::string pwd);
static bool login(std::string name_, std::string pwd);
static json getAllUsers();
static json getAllFriends(std::string name_);
static bool addFriend(std::string name_, std::string friend_);
*/

void MainControl::init() {
	int PORT_NO = PORT;
	//char serverAddress[20] = "111.231.223.231";
	//char serverAddress[20] = "127.0.0.1";
	char serverAddress[20] = ADDRESS;
	
	int portno;
	struct sockaddr_in serv_addr;
	struct hostent *server;
	
	//char buffer[256];
	
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
	
	sess = new ClientSession(sockfd, 0);
	std::thread *sessThread = new std::thread();
	*sessThread = std::thread(&ClientSession::startReading, *sess);
	sessThread = new std::thread();
	*sessThread = std::thread(&ClientSession::start, *sess);
	
	/*
	while (1) {
	    //printf("Please enter the message: ");
	    std::string str;
	    std::getline(std::cin, str);
	    sess->writeString(str);
	    //printf("write succ\n");
	}
	*/
	
	/*
	bzero(buffer,256);
	n = read(sockfd,buffer,255);
	if (n < 0) 
	     error("ERROR reading from socket");
	printf("%s\n",buffer);
	close(sockfd);
	*/
	
}


int main() {
	MainControl::init();
	MainControl::mainLoop();
	/*
    int PORT_NO = 8888;
    //char serverAddress[20] = "111.231.223.231";
    char serverAddress[20] = "127.0.0.1";


    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[256];

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
    	//printf("write succ\n");
    }

    bzero(buffer,256);
    n = read(sockfd,buffer,255);
    if (n < 0) 
         error("ERROR reading from socket");
    printf("%s\n",buffer);
    close(sockfd);
    */
    return 0;
}
