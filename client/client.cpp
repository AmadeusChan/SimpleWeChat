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
#include <fstream>

#include "json.hpp"

#define PORT 8888
#define ADDRESS "127.0.0.1"

#define BUFFER_SIZE 65536
#define MAX_FILE_PAC_LEN 16348 

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
		static std::mutex dispMtx;
		static void msg(std::string msg_) {
			msgMtx.lock();
			std::cout<<msg_;
			msgMtx.unlock();
		}
		static void showWelcome() {
			msg("welcome to SimpleWechat app.\n");
		}
		static void showHint() {
			msg("please input new command. input help for more details. \n>\n");
		}
		static std::string waitingForCommand() {
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
			msg("logout: logout\n");
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
std::mutex MainDisplay::dispMtx;

class MainControl {
	public: 
		static const int FORMAL_STATE, CHATTING_STATE;
		static ClientSession* sess;
		static int sockfd;
		static bool isLogin;

		static int fileCnt;
		static std::deque<std::string> fileList;

		static std::mutex loginMtx;

		static void init();
		static void mainLoop();
		static bool getLogin();
		static void setLogin(bool flag_);
		static void registerUser(std::string name_, std::string pwd_);
		static void registerSuccess();
		static void registerFailed();
		static void login(std::string name_, std::string pwd);
		static void loginSucess();
		static void loginFailed();
		static void getAllUsers();
		static void getAllUsersSuccess(json list);
		static void getAllFriends(std::string name_);
		static void getAllFriendsSuccess(json list);
		static void addFriend(std::string name_, std::string fri_);
		static void addFriendSuccess();
		static void addFriendFailed();
		static void showProfile(std::string name_);
		static void sendMessage(std::string sender_, std::string receiver_, std::string msg_);
		static void showMessage(json msg);
		static void showAllUnreadMessage(std::string name_);
		static void showAllUnreadMessageSuccess(json list);
		static void logout();
		static void showMessageList(json list);
		static void sendFile(std::string sender_, std::string receiver_, std::string file);
		static void sendNextFilePacket(json response_);
		static void sendReceiveFileRequest(std::string receiver_);
		static void receiveFileFailed();

		static void getFilePacket(json j);
		static void receiveFileSuccess();
		static void sendSticker(std::string sender_, std::string receiver_, std::string msg_);
		static void sendExitRequest();
		/*
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
			//std::cout<<buffer<<std::endl;
			writeEvent->push_back(SessionEvent(SessionEvent::WRITE_EVENT, buffer, len));
			writeEventMtx->unlock();
		}
		void writeString(std::string str) {
			//std::cout<<str<<std::endl;
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
				} else if (type.compare("login_ack") == 0) {
					MainControl::loginSucess();
				} else if (type.compare("login_nak") == 0) {
					MainControl::loginFailed();
				} else if (type.compare("get_all_users_ack") == 0) {
					MainControl::getAllUsersSuccess(j["content"]);
					//MainControl::getAllFriendsResponse();
				} else if (type.compare("get_all_friends_ack") == 0) {
					MainControl::getAllFriendsSuccess(j["content"]);
				} else if (type.compare("add_friend_ack") == 0) {
					MainControl::addFriendSuccess();
				} else if (type.compare("add_friend_nak") == 0) {
					MainControl::addFriendFailed();
				} else if (type.compare("get_all_unread_msg_ack") == 0) {
					MainControl::showAllUnreadMessageSuccess(j["content"]);
				} else if (type.compare("realtime_msg") == 0) {
					MainControl::showMessageList(j["content"]);
				} else if (type.compare("send_file_ack") == 0) {
					MainControl::sendNextFilePacket(j);
				} else if (type.compare("receive_file_request_nak") == 0) {
					MainControl::receiveFileFailed();
				} else if (type.compare("send_file") == 0) {
					MainControl::getFilePacket(j["content"]);
					json res;
					res["type"] = "send_file_ack";
					res["num"] = j["content"]["num"];
					res["max_num"] = j["content"]["max_num"];
					//res["id"] = j["content"]["id"];
					res["sender"] = j["content"]["sender"];
					res["receiver"] = j["content"]["receiver"];
					res["file_name"] = j["content"]["file_name"];
					writeString(res.dump());
					int num = res["num"];
					int max_num = res["max_num"];
					if (num == max_num) {
						MainControl::receiveFileSuccess();
					}
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
							temp[tempCnt] = 0;
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
	MainDisplay::dispMtx.lock();
	MainDisplay::msg("register sucess!\n");
	MainDisplay::dispMtx.unlock();
}

void MainControl::registerFailed() {
	//MainDisplay::msg("register failed!  please input new command.>");
	MainDisplay::dispMtx.lock();
	MainDisplay::msg("register failed!\n");
	MainDisplay::dispMtx.unlock();
}

void MainControl::mainLoop() {
	MainDisplay::showWelcome();
	int state = FORMAL_STATE;
	std::string userName, chatto;
	while (1) {
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		MainDisplay::dispMtx.lock();
		if (state == CHATTING_STATE) MainDisplay::msg("now chatting to " + chatto + "...\n");
		MainDisplay::showHint();
		MainDisplay::dispMtx.unlock();
		std::string str = MainDisplay::waitingForCommand();

		std::vector<std::string> args = split(str, ' ');
		if (state == FORMAL_STATE) {
			if (args[0].compare("register") == 0) {
				MainControl::registerUser(args[1], args[2]);
			} else if (args[0].compare("login") == 0 && !getLogin()) {
				MainControl::login(args[1], args[2]);
				userName = args[1];
			} else if (args[0].compare("help") == 0) {
				MainDisplay::showHelp();
			} else if (args[0].compare("exit") == 0) {
				MainControl::logout();
				MainControl::sendExitRequest();
				break;
			} else if (getLogin()) {
				if (args[0].compare("search") == 0) {
					MainControl::getAllUsers();
				} else if (args[0].compare("add") == 0) {
					MainControl::addFriend(userName, args[1]);
				} else if (args[0].compare("ls") == 0) {
					MainControl::getAllFriends(userName);
				} else if (args[0].compare("chat") == 0) {
					chatto = args[1];
					state = CHATTING_STATE;
				} else if (args[0].compare("recvmsg") == 0) {
					MainControl::showAllUnreadMessage(userName);
				} else if (args[0].compare("recvfile") == 0) {
					MainControl::sendReceiveFileRequest(userName);
				} else if (args[0].compare("profile") == 0) {
					MainControl::showProfile(userName);
				} else if (args[0].compare("sync") == 0) {
				} else if (args[0].compare("logout") == 0) {
					MainControl::logout();
				} else {
					MainDisplay::showInvalidCommand();
				}
			} else {
				MainDisplay::showNotLoginWarnning();
			}
		} else {
			if (args[0].compare("sendmsg") == 0) {
				MainControl::sendMessage(userName, chatto, str.substr(args[0].length()+1, std::string::npos));
			} else if (args[0].compare("senfile") == 0) {
				MainControl::sendFile(userName, chatto, args[1]);
			} else if (args[0].compare("exit") == 0) {
				state = FORMAL_STATE;
			} else if (args[0].compare("sticker") == 0) {
				MainControl::sendSticker(userName, chatto, args[1]);
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

void MainControl::login(std::string name_, std::string pwd_) {
	json j;
	j["type"] = "login";
	json content;
	content["name"] = name_;
	content["pwd"] = pwd_;
	j["content"] = content;
	sess->writeString(j.dump());
}

void MainControl::loginSucess() {
	MainDisplay::dispMtx.lock();
	MainDisplay::msg("login sucess!\n");
	MainDisplay::dispMtx.unlock();
	setLogin(true);
}

void MainControl::loginFailed() {
	MainDisplay::dispMtx.lock();
	MainDisplay::msg("login failed!\n");
	MainDisplay::dispMtx.unlock();
}

void MainControl::getAllUsers() {
	json j;
	j["type"] = "getAllUsers";
	sess->writeString(j.dump());
}

void MainControl::getAllFriends(std::string name_) {
	json j;
	j["type"] = "getAllFriends";
	json cont;
	cont["name"] = name_;
	j["content"] = cont;
	sess->writeString(j.dump());
}

void MainControl::getAllUsersSuccess(json list) {
	MainDisplay::dispMtx.lock();
	MainDisplay::msg("all users:\n");
	int len = list.size();
	for (int i=0; i<len; ++i) {
		std::string str = list[i];
		MainDisplay::msg(" - " + str + "\n");
	}
	MainDisplay::msg("\n");
	MainDisplay::dispMtx.unlock();
}

void MainControl::getAllFriendsSuccess(json list) {
	MainDisplay::dispMtx.lock();
	MainDisplay::msg("all friends:\n");
	int len = list.size();
	for (int i=0; i<len; ++i) {
		std::string str = list[i];
		MainDisplay::msg(" - " + str + "\n");
	}
	MainDisplay::msg("\n");
	MainDisplay::dispMtx.unlock();
}

/*
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

void MainControl::addFriend(std::string name_, std::string fri_) {
	json j;
	json content;
	j["type"] = "addFriend";
	content["name"] = name_;
	content["friend"] = fri_;
	j["content"] = content;
	sess->writeString(j.dump());
}

void MainControl::addFriendSuccess() {
	MainDisplay::dispMtx.lock();
	MainDisplay::msg("successfully added friend!\n");
	MainDisplay::dispMtx.unlock();
}

void MainControl::addFriendFailed() {
	MainDisplay::dispMtx.lock();
	MainDisplay::msg("add friend fail. you're already friends!\n");
	MainDisplay::dispMtx.unlock();
}

void MainControl::showProfile(std::string name_) {
	MainDisplay::msg("profile:\n\n");
	MainDisplay::msg("username: " + name_ + "\n\n");
	getAllFriends(name_);
}

void MainControl::sendMessage(std::string sender_, std::string receiver_, std::string msg_){
	json j;
	j["type"] = "sendMessage";
	json content;
	content["sender"] = sender_;
	content["receiver"] = receiver_;
	content["msg"] = msg_;
	content["isSticker"] = false;
	j["content"] = content;
	sess->writeString(j.dump());
}

void MainControl::sendSticker(std::string sender_, std::string receiver_, std::string msg_) {
	json j;
	j["type"] = "sendMessage";
	json content;
	content["sender"] = sender_;
	content["receiver"] = receiver_;
	content["msg"] = msg_;
	content["isSticker"] = true;
	j["content"] = content;
	sess->writeString(j.dump());
}

void MainControl::showMessage(json msg_) {
	//std::cout<<"to show msg"<<std::endl;
	bool isSticker=false;
	try {
		isSticker = msg_["isSticker"];
	} catch (std::exception e) {
		isSticker = false;
	}
	if (!isSticker) {
		std::string sender = msg_["sender"];
		std::string receiver = msg_["receiver"];
		std::string msg = msg_["msg"];
		MainDisplay::msg("message: " + sender + " said to " + receiver + ": " + msg + "\n");
	} else {
		try {
			std::string stickerName = msg_["msg"];
			std::ifstream fin("./stickers/" + stickerName + ".txt");
			if (!fin.good()) {
				fin.close();
				return ;
			}
			std::string line;
			while (!fin.eof()) {
				std::getline(fin, line);
				MainDisplay::msg(line+"\n");
			}
			fin.close();
			std::string sender = msg_["sender"];
			MainDisplay::msg("you received a sticker from " + sender+"\n");
		} catch (std::exception e) {
		}
	}
}

void MainControl::showAllUnreadMessage(std::string name_) {
	json j;
	j["type"] = "getAllMessage";
	json content;
	content["name"] = name_;
	content["unreadOnly"] = true;
	j["content"] = content;
	sess->writeString(j.dump());
}

void MainControl::showAllUnreadMessageSuccess(json list) {
	MainDisplay::dispMtx.lock();
	int len = list.size();
	for (int i=0; i<len; ++i) {
		showMessage(list[i]);
	}
	MainDisplay::dispMtx.unlock();
}

void MainControl::showMessageList(json list) {
	//std::cout<<"real time msg received!"<<std::endl;
	MainDisplay::dispMtx.lock();
	//std::cout<<"to print real time msg received!"<<std::endl;
	//std::cout<<list.dump()<<std::endl;
	int len = list.size();
	for (int i=0; i<len; ++i) {
		showMessage(list[i]);
	}
	//MainDisplay::showHint();
	MainDisplay::msg(">\n");
	MainDisplay::dispMtx.unlock();
}


void MainControl::logout() {
	MainDisplay::msg("logout!\n");
	json j;
	j["type"] = "logout";
	sess->writeString(j.dump());
	setLogin(false);
}

int MainControl::fileCnt = 0;
std::deque<std::string> MainControl::fileList;

std::string byte2Hex(unsigned char c_) {
	unsigned int hi = c_/16;
	unsigned int low = c_%16;
	std::string val = "";
	if (hi<10) val.push_back('0'+hi);
	else val.push_back('A'+hi-10);
	if (low<10) val.push_back('0'+low);
	else val.push_back('A'+low-10);
	//std::cout<<int(c_)<<":"<<int(hi)<<":"<<int(low)<<":"<<val<<std::endl;
	return val;
}

char hex2Byte(char s0, char s1) {
	char hi, low;
	if (s0>='0' && s0<='9') hi = s0-'0';
	else hi = s0-'A'+10;
	if (s1>='0' && s1<='9') low = s1-'0';
	else low = s1-'A'+10;
	return hi*16+low;
}

std::string translateStr(char *str_, int len) {
	std::string val = "";
	//std::cout<<"len: "<<len<<std::endl;
	//std::cout<<str_<<std::endl;
	for (int i=0; i<len; ++i) {
		val = val + byte2Hex(str_[i]);
	}
	//std::cout<<val<<std::endl;
	return val;
}

void inverseTranslate(char *str_, int len) {
	std::string val = "";
	int j=0;
	for (int i=0; i<len; i+=2, ++j) {
		str_[j] = hex2Byte(str_[i], str_[i+1]);
	}
	str_[j]=0;
}

char hex2Byte(std::string s_) {
	char hi, low;
	if (s_[0]>='0' && s_[0]<='9') hi = s_[0]-'0';
	else hi = s_[0]-'A'+10;
	if (s_[1]>='0' && s_[1]<='9') low = s_[1]-'0';
	else low = s_[1]-'A'+10;
	return hi*16+low;
}

std::string inverseTranslate(std::string str_) {
	std::string val = "";
	int len = str_.size();
	for (int i=0; i<len; i+=2) {
		val.push_back(hex2Byte(str_.substr(i, 2)));
	}
	return val;
}

void MainControl::sendFile(std::string sender_, std::string receiver_, std::string file_) {
	char buffer[MAX_FILE_PAC_LEN + 10];
	bzero(buffer, MAX_FILE_PAC_LEN + 10);
	std::ifstream fin(file_, std::ifstream::binary);
	fin.seekg (0, fin.end);
	int len = fin.tellg();
	fin.seekg (0, fin.beg);
	fin.read(buffer, std::min(MAX_FILE_PAC_LEN, len));
	fin.close();

	std::string file_name = "";
	for (int i=file_.size()-1; i>=0; --i) {
		if (file_[i]!='/') {
			file_name = file_[i] + file_name;
		} else {
			break;
		}
	}

	int max_num = len / MAX_FILE_PAC_LEN;
	if (len % MAX_FILE_PAC_LEN == 0 && len>0) max_num--;
	json j;
	j["type"] = "sendFile";
	json content;
	content["sender"] = sender_;
	content["receiver"] = receiver_;
	content["num"] = 0;
	content["max_num"] = max_num;
	content["id"] = fileCnt;
	content["file_name"] = file_name;
	content["payload"] = translateStr(buffer, len<=MAX_FILE_PAC_LEN?len:MAX_FILE_PAC_LEN);
	fileList.push_back(file_);
	fileCnt++;
	j["content"] = content;
	sess->writeString(j.dump());
	MainDisplay::dispMtx.lock();
	MainDisplay::msg("seding file...\n>\n");
	MainDisplay::dispMtx.unlock();
}

void MainControl::sendNextFilePacket(json response_) {
	int id = response_["id"];
	std::string file_ = fileList[id];
	int num = response_["num"];
	int max_num = response_["max_num"];
	if (num == max_num) {
		MainDisplay::dispMtx.lock();
		MainDisplay::msg("successfully sent file!\n>\n");
		MainDisplay::dispMtx.unlock();
	} else {
		num++;

		int pos = MAX_FILE_PAC_LEN*num;
		char buffer[MAX_FILE_PAC_LEN + 10];
		bzero(buffer, MAX_FILE_PAC_LEN + 10);
		std::ifstream fin(file_, std::ifstream::binary);
		fin.seekg(0, fin.end);
		int total_len=fin.tellg();
		//std::cout<<total_len<<std::endl;
		fin.seekg(pos);
		fin.read(buffer, std::min(total_len-(num)*MAX_FILE_PAC_LEN, MAX_FILE_PAC_LEN));
		fin.close();

		json j;
		j["type"] = "sendFile";
		json content;
		content["sender"] = response_["sender"];
		content["receiver"] = response_["receiver"];
		content["num"] = num;
		content["max_num"] = max_num;
		content["id"] = id;
		content["file_name"] = response_["file_name"];
		//content["payload"] = translateStr(buffer, std::min(MAX_FILE_PAC_LEN, total_len-(num-1)*MAX_FILE_PAC_LEN));
		content["payload"] = translateStr(buffer, num<max_num?MAX_FILE_PAC_LEN:(total_len-(num)*MAX_FILE_PAC_LEN));
		//content["payload"] = translateStr(std::string(buffer));
		j["content"] = content;
		sess->writeString(j.dump());
		//std::string payload = content["payload"];
		//std::cout<<payload.size()<<" "<<total_len-(num-1)*MAX_FILE_PAC_LEN<<std::endl;
	}
}

void MainControl::getFilePacket(json j) {
	int num = j["num"];
	//int maxNum = j["max_num"];
	std::string name = j["file_name"];
	name = "./downloads/" + name;
	std::string payload = j["payload"];
	std::string sender = j["sender"];
	std::string receiver = j["receiver"];

	std::ofstream fout;
	if (num) {
		fout.open(name.c_str(), std::ios_base::app);
	} else {
		fout.open(name.c_str());
	}
	std::cout<<"getFilePacket:"<<j.dump()<<std::endl<<name<<std::endl;
	fout<<inverseTranslate(payload);
	fout.close();
	/*
	if (num == maxNum) {
		sendFileRequest(sender, receiver, name);
	}
	*/
}

void MainControl::receiveFileFailed() {
	MainDisplay::dispMtx.lock();
	MainDisplay::msg("there is no file to be recieved!\n>\n");
	MainDisplay::dispMtx.unlock();
}

void MainControl::sendReceiveFileRequest(std::string receiver_) {
	json j;
	j["type"] = "receive_file_request";
	json content;
	content["receiver"] = receiver_;
	j["content"] = content;
	sess->writeString(j.dump());
}

void MainControl::receiveFileSuccess() {
	MainDisplay::dispMtx.lock();
	MainDisplay::msg("successfully recieved file!\n>\n");
	MainDisplay::dispMtx.unlock();
}

void MainControl::sendExitRequest() {
	json j;
	j["type"] = "exit_request";
	sess->writeString(j.dump());
}

int main() {
	/*
	char s[20] = "01AB";
	std::cout<<translateStr(s, 4)<<std::endl;
	return 0;
	*/
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
