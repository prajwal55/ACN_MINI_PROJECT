#include "unp.h"

void game_cli(FILE* fp, int sockfd);

int main(int argc, char **argv)
{
	int	sockfd;
	struct sockaddr_in	servaddr;  
	char buf[MAXLINE];
	
	if (argc != 3)
		err_quit("usage: tcpcli <IPaddress> <username>");
	
	sockfd = Socket(AF_INET, SOCK_STREAM, 0);
	
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(SERV_PORT);
	Inet_pton(AF_INET, argv[1], &servaddr.sin_addr);
	
	Connect(sockfd, (SA *) &servaddr, sizeof(servaddr));
	
	int n = snprintf(buf, sizeof(buf), "#NAME %s", argv[2]);
	Write(sockfd, buf, n);
	Writen(fileno(stdout), "\n[Local Notification:] Username message sent out.\n", 50);
	
	game_cli(stdin, sockfd); //start number-guessing game
	
	exit(0);
}