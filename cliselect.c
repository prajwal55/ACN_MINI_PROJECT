#include "unp.h"
#include "ctype.h"
#include "stdbool.h"
#include "game.h"

void game_cli(FILE *fp, int sockfd)
{
    int	maxfdp1, stdineof, n, notificationBit;
    fd_set rset;
    char buf[MAXLINE], displayLine[MAXLINE];
    struct timeval SELECT_TIMEOUT = { 5, 0 }; //Initialize timeval to 5 seconds
    enum cli_state_t cliState = CLI_SENT;
    time_t idleStartTime = time(NULL);
    long int elapsedTime;
    const long int KEEP_ALIVE_PERIOD = 120;

    stdineof = 0;
    FD_ZERO(&rset);
    notificationBit = 0;

    for ( ; ; ) 
    {
        if (stdineof == 0)
            FD_SET(fileno(fp), &rset);
        
        FD_SET(sockfd, &rset);
        maxfdp1 = max(fileno(fp), sockfd) + 1;
        Select(maxfdp1, &rset, NULL, NULL, &SELECT_TIMEOUT); //Select() waits up to 5 seconds for descriptor to be ready
        bzero(buf, MAXLINE);
        bzero(displayLine, MAXLINE);

        if (FD_ISSET(sockfd, &rset)) //readable socket, server's reply received
        {	
            idleStartTime = time(NULL);
            notificationBit = 0;

            if ((n = Read(sockfd, buf, MAXLINE)) == 0) //connected terminated by server
            {
                if (stdineof == 1) //normal termination
                    return;		
                else //server crashes and connection terminated
                    err_quit("\nConnection terminated by server. Please try to reconnect later.\n");
            }
            else if(prefix("#END", buf)) //server ends the game and sends application ACK
            { 
                str_cut(buf, 4);
                Writen(fileno(stdout), buf, n - 4);
                return;	//normal termination
            }
            else
            {
                Writen(fileno(stdout), buf, n);
                cliState = CLI_RCVD; //server's reply received
            }
        }
        else //non-readable socket, server's reply not received
        {
            switch (cliState)
            {
                case CLI_SENT: //client's message sent, but server's reply not received

                    elapsedTime = (long int)(time(NULL) - idleStartTime);

                    if (elapsedTime >= KEEP_ALIVE_PERIOD) //terminate connection after server being unreachable for 120 seconds
                    {
                        n = snprintf(displayLine, sizeof(displayLine), "\n[%.24s] [Local Notification:] No response from server for %ld seconds.\nConnection terminated by program due to keep-alive timeout.\n\n",
                            timestamp(), elapsedTime);
                        Writen(fileno(stdout), displayLine, n);
                        return; //connection terminated by client due to keep-alive timeout
                    }
                    else if ((elapsedTime >= KEEP_ALIVE_PERIOD / 2) && (notificationBit == 0)) //send RECONN message to server after server being unreachable for 60 seconds
                    {
                        Writen(sockfd, "#RECONN", 7);
                        
                        n = snprintf(displayLine, sizeof(displayLine), "\n[%.24s] [Local Notification:] No response from server for %ld seconds.\nConnection will be terminated by program after another %ld seconds. Communicating with server...\n\n",
                            timestamp(), elapsedTime, KEEP_ALIVE_PERIOD / 2);
                        Writen(fileno(stdout), displayLine, n);

                        notificationBit = 1; //RECONN message sent and will be resent after notification bit is cleared
                    }
                    break;

                case CLI_RCVD: //server's reply not received because client has not replied

                    idleStartTime = time(NULL); //Restart idle timer
            }
        }

        bzero(displayLine, MAXLINE);
        
        if (FD_ISSET(fileno(fp), &rset)) //readable client's input
        {  
            if ((n = Read(fileno(fp), buf, MAXLINE)) == 0) 
            {
                stdineof = 1;
                Shutdown(sockfd, SHUT_WR);	//send FIN 
                FD_CLR(fileno(fp), &rset);
                continue;
            }
            
            lowerStr(buf, n);

            switch (cliState)
            {
                case CLI_SENT: //client's message sent awaiting server's reply

                    n = snprintf(displayLine, sizeof(displayLine), "\n[%.24s] [Local Notification:] Waiting for server to respond...\n", timestamp());
                    Writen(fileno(stdout), displayLine, n);
                    break;

                case CLI_RCVD: //server's reply received and send client's message to server

                    Writen(sockfd, buf, n);
                    n = snprintf(displayLine, sizeof(displayLine), "\n[%.24s] [Local Notification:] Message sent out.\n", timestamp());
                    Writen(fileno(stdout), displayLine, n);
                    cliState = CLI_SENT;
            }
        }
    }
}