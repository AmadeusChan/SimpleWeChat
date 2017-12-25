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

#define PORT 8889

class NetworkManager {
	private:
		static int port;
		static char buffer[256];

		static void error(const char *msg) {
		    perror(msg);
		    exit(1);
		}

		static void processWord(char *word, int len) {
			printf("string received: ");
			for (int i=0; i<len; ++i) {
				putchar(word[i]);
			}
			putchar('\n');
		}

		static void readFromNewSocket(int newsockfd_) {
			// `: esc
			// ;: flag
			printf("reading from client\n");
			char temp[512];
			int tempCnt = 0;
			bool isEsc = false;
			char flag = ';';
			char esc = '`';
			while (1) {
				bzero(buffer, 256);
				int n = read(newsockfd_, buffer, 255);
				//if (n > 0) printf("%s", buffer);
				if (n < 0) {
					error("ERROR reading from socket");
					break;
				}
				for (int i=0; i<n; ++i) {
					//printf("%c", buffer[i]);
					if ((buffer[i] != flag && buffer[i] != esc) || isEsc) {
						isEsc = false;
						temp[tempCnt++] = buffer[i];
					} else {
						if (buffer[i] == flag) {
							processWord(temp, tempCnt);
							tempCnt = 0;
						} else {
							isEsc = true;
						}
					}
				}
			}
			close(newsockfd_);
		}

	public:
		static void init() {
			port = 8888;
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
			printf("waiting...");
			newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
			if (newsockfd < 0) {
				error("ERROR on accept");
				return ;
			}
			readFromNewSocket(newsockfd);
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
