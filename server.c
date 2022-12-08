#include "unp.h"
#include "stdbool.h"
#include "time.h"
#include "ctype.h"
#include "game.h"

int main(int argc, char **argv)
{
    int i, maxi, maxfd, listenfd, connfd, sockfd, gcount, dcount, rcount, nready;
    ssize_t	n;
    fd_set rset, allset;
    char buf[MAXLINE], displayLine[MAXLINE], broadcastMsg[MAXLINE];
    time_t lastBackupTime;
    long int elapsedTime, timeSpent;
    socklen_t clilen;
    struct sockaddr_in	cliaddr, servaddr;
    struct timeval SELECT_TIMEOUT = { 5, 0 }; //Initialize timeval to 5 seconds
    struct GameRecord activeRecords[FD_SETSIZE];
    struct PastGameRecord pastRecords[FD_SETSIZE];
    struct DisconnectGameRecord disconnRecords[FD_SETSIZE];
    const long int KEEP_ALIVE_PERIOD = 120;
    const long int BACKUP_INTERVAL = 60;
    const char GAME_RULES[]   = "I will pick a number from 1 to 99 randomly and you guess the number.\nThe fewer the number of guesses, the better.\n";
    const char PROMPT_START[] = "Ready? (Y/N): ";
    const char PROMPT_GUESS[] = "Make a guess from the range (1 - 99) or enter \"quit\" to end game: ";
    const char DIVIDER[]      = "___________________________________________________________________________________________________________________________\n\n";
  
    gcount = gameRead(pastRecords); //Initialize past records
    appendActiveRecords();
    dcount = disconnRead(disconnRecords); //Initialize disconnect records
    srand(time(NULL));
    
    listenfd = Socket(AF_INET, SOCK_STREAM, 0);
    
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port        = htons(SERV_PORT);
    
    Bind(listenfd, (SA *) &servaddr, sizeof(servaddr));
    Listen(listenfd, LISTENQ);

    n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s] Server starts to accept incoming connections.\n%s\n", DIVIDER, timestamp(), DIVIDER);
    Writen(fileno(stdout), displayLine, n);
    
    maxfd = listenfd; 	                 
    maxi = -1;
    
    for (i = 0; i < FD_SETSIZE; i++)
        activeRecords[i].sockfd = -1; //Initialize all active records into available entries
    
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    
    lastBackupTime = time(NULL);
    
    for ( ; ; ) 
    {
        rset = allset;		
        FD_SET(fileno(stdin), &rset);
        nready = Select(maxfd + 1, &rset, NULL, NULL, &SELECT_TIMEOUT); //Select() waits up to 5 seconds for each active record to be ready     
              
        if (FD_ISSET(listenfd, &rset)) //new client connection
        {	
            clilen = sizeof(cliaddr);
            connfd = Accept(listenfd, (SA *) &cliaddr, &clilen);
            
            for (i = 0; i < FD_SETSIZE; i++)
            {
                if (activeRecords[i].sockfd < 0) 
                {   //Initialize new active record
                    activeRecords[i].sockfd = connfd;	
                    strcpy(activeRecords[i].preBuf, " ");
                    activeRecords[i].gameState = SERV_WAIT_1;
                    activeRecords[i].closeFlag = CF_CONNRESET_EARLY;
                    activeRecords[i].idleStartTime = time(NULL);
                    activeRecords[i].startTime = (time_t)(-1);
                    break;
                }
            }
        
            if (i == FD_SETSIZE)
                err_quit("too many clients");
            
            FD_SET(connfd, &allset); //add new descriptor to set
            
            if (connfd > maxfd)
                maxfd = connfd;			
            
            if (i > maxi)
                maxi = i; //max index of active records  
            
            if (--nready <= 0)
                continue; 
        }
        
        for (i = 0; i <= maxi; i++) //check for all active records
         {	
            if ((sockfd = activeRecords[i].sockfd) < 0)
                continue;
            
            if (FD_ISSET(sockfd, &rset)) //readable socket for active records
            {
                bzero(buf, MAXLINE);
                bzero(displayLine, MAXLINE);
                activeRecords[i].idleStartTime = time(NULL);
                activeRecords[i].warningBit = 0;

                if ((n = Read(sockfd, buf, MAXLINE)) == 0) //connection closed by client
                    activeRecords[i].gameState = CLI_CLOSE; 

                if (strstr(buf, "#RECONN") != NULL) //resend server's previous reply, this is due to simulation of sleep()
                {                                   //server crashed and rebooted
                    n = snprintf(buf, sizeof(buf), "%s[%.24s] [Resend Message:] %s%s",
                        DIVIDER, timestamp(), "[Sorry, server encountered internal error just now.]", activeRecords[i].preBuf);
                    Writen(sockfd, buf, n);

                    if (activeRecords[i].gameState == SERV_WAIT_1) //request client to re-run program
                    {
                        Writen(sockfd, "[Please reconnect later.]", 25);

                        Close(sockfd);
                        FD_CLR(sockfd, &allset);
                        activeRecords[i].sockfd = -1;

                        n = snprintf(displayLine, sizeof(displayLine), "[%.24s] [System Message:]\n%s1 unknown connection terminated due to internal error.\n%s",
                            timestamp(), DIVIDER, DIVIDER);
                        Writen(fileno(stdout), displayLine, n);
                        continue;
                    }
                    n = snprintf(displayLine, sizeof(displayLine), "[%.24s] [Server's reply to %s:]\n%s\n%s",
                        timestamp(), activeRecords[i].username, buf, DIVIDER);
                    Writen(fileno(stdout), displayLine, n);
                    continue;
                }

                bzero(activeRecords[i].preBuf, MAXLINE);

                switch (activeRecords[i].gameState)
                {  
                    case SERV_WAIT_1: //Username received
                        
                        if (prefix("#NAME ", buf))
                        {
                            strcpy(activeRecords[i].username, buf); //record username
                            str_cut(activeRecords[i].username, 6);
                            Getpeername(sockfd, (SA*)&cliaddr, &clilen);
                            strcpy(activeRecords[i].address, Sock_ntop((SA*)&cliaddr, clilen));
                            
                            //display client's information                             
                            n = snprintf(displayLine, sizeof(displayLine), "[%.24s] [System Message:]\n%s%s [%s] entered the game field.\n%s", 
                                timestamp(), DIVIDER, activeRecords[i].username, activeRecords[i].address, DIVIDER);
                            Writen(fileno(stdout), displayLine, n);
                                                        
                            //send welcome message, game rules and start-game prompt
                            n = snprintf(buf, sizeof(buf), "\n%s[%.24s]\n\nWelcome %s!\n%s\n%s", 
                                DIVIDER, timestamp(), activeRecords[i].username, GAME_RULES, PROMPT_START);
                            Writen(sockfd, buf, n);

                            bzero(displayLine, MAXLINE); 
                            n = snprintf(displayLine, sizeof(displayLine), "[%.24s] [Server's reply to %s:] %s\n%s", 
                                timestamp(), activeRecords[i].username, buf, DIVIDER);
                            Writen(fileno(stdout), displayLine, n);

                            activeRecords[i].gameState = SERV_WAIT_2; //wait for client's start/decline game message
                        }
                        strcpy(activeRecords[i].preBuf, buf);
                        break;
                    
                    case SERV_WAIT_2: //Client's start-game/decline-game/invalid message received

                        activeRecords[i].closeFlag = CF_CONNRESET_EARLY; //does not store disconnect record

                        n = snprintf(displayLine, sizeof(displayLine), "[%.24s] [Player's Response:]\n%s%s: %s\n", 
                            timestamp(), DIVIDER, activeRecords[i].username, buf);
                        Writen(fileno(stdout), displayLine, n);
                        lowerStr(buf, n);

                        if (strcmp(buf, "y\n") == 0) //if client is ready to play the game
                        {
                            activeRecords[i].closeFlag = CF_CONNRESET_LATE; //store disconnect record if client terminates abruptly
                            bzero(displayLine, MAXLINE);
                            
                            n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s] [System Message:]\n%s%s is ready to start the game.\n", 
                                DIVIDER, timestamp(), DIVIDER, activeRecords[i].username);
                            Writen(fileno(stdout), displayLine, n);
                                            
                            if(getDisconnRecord(disconnRecords, activeRecords[i].username, &dcount, &activeRecords[i].randomNumber, 
                                                &activeRecords[i].lowerBound, &activeRecords[i].upperBound, &activeRecords[i].attemptsNo) >= 1)
                            {   //disconnect record exists
                                disconnWrite(disconnRecords, dcount);
                                
                                activeRecords[i].gameState = SERV_WAIT_3; //wait for client's continue/restart game message
                                activeRecords[i].warningBit = 0;
                                activeRecords[i].bestAttemptsNo = getBestAttemptsNo(pastRecords, activeRecords[i].username, gcount);
                                
                                n = snprintf(buf, sizeof(buf), "\n%s[%.24s]\n\n%s, you have an unfinished game. Do you want to continue the previous game? (Y/N): ", 
                                    DIVIDER, timestamp(), activeRecords[i].username);
                                Writen(sockfd, buf, n);
                            }
                            else //disconnect record does not exist
                            {   
                                activeRecords[i].gameState = CLI_SERV_PLAY; //start the game
                                activeRecords[i].warningBit = 0;
                                activeRecords[i].randomNumber = (rand() % 99) + 1;
                                activeRecords[i].lowerBound = 1;
                                activeRecords[i].upperBound = 99;
                                activeRecords[i].startTime = time(NULL);
                                activeRecords[i].attemptsNo = 0;
                                activeRecords[i].bestAttemptsNo = getBestAttemptsNo(pastRecords, activeRecords[i].username, gcount);
                                   
                                //Prompt client to guess number
                                n = snprintf(buf, sizeof(buf), "\n%s[%.24s]\n\n[Attempt No.%d] %s", 
                                    DIVIDER, timestamp(), activeRecords[i].attemptsNo + 1, PROMPT_GUESS);
                                Writen(sockfd, buf, n);

                                //display client's information and random number generated for the client
                                bzero(displayLine, MAXLINE);
                                n = snprintf(displayLine, sizeof(displayLine), "\nPlayer: %s\nLocal address: %s\nRandom number generated: %d\n",
                                    activeRecords[i].username, activeRecords[i].address, activeRecords[i].randomNumber);
                                Writen(fileno(stdout), displayLine, n);
                            }
                        }
                        else if (strcmp(buf, "n\n") == 0) //if client is not ready to play the game
                        {
                            bzero(displayLine, MAXLINE);
                            n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s] [System Message:]\n%s%s is not ready and decides to leave the game.\n", 
                                DIVIDER, timestamp(), DIVIDER, activeRecords[i].username);
                            Writen(fileno(stdout), displayLine, n);

                            n = snprintf(buf, sizeof(buf), "#END\n%s[%.24s]\n\nBye %s! See you next time.\n", 
                                DIVIDER, timestamp(), activeRecords[i].username);
                            Writen(sockfd, buf, n); str_cut(buf, 4);
                            
                            activeRecords[i].gameState = CLI_CLOSE; //going to terminate client's connection
                            activeRecords[i].closeFlag = CF_NOTREADY; //connection closed due to not ready condition
                        }
                        else //invalid response from client
                        {
                            bzero(displayLine, MAXLINE);
                            n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s] [System Message:]\n%sInvalid response from %s.\n", 
                                DIVIDER, timestamp(), DIVIDER, activeRecords[i].username);
                            Writen(fileno(stdout), displayLine, n);

                            n = snprintf(buf, sizeof(buf), "\n%s[%.24s]\n\nInvalid input.\n\nEnter Y to start game, N to exit: ", 
                                DIVIDER, timestamp());
                            Writen(sockfd, buf, n);
                        }
                        strcpy(activeRecords[i].preBuf, buf);
                        
                        bzero(displayLine, MAXLINE); 
                        n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s] [Server's reply to %s:] %s\n%s", 
                            DIVIDER, timestamp(), activeRecords[i].username, buf, DIVIDER);
                        Writen(fileno(stdout), displayLine, n);
                        break;
                        
                    case SERV_WAIT_3: //Client's restart-game/continue-game/invalid message received
                    
                        n = snprintf(displayLine, sizeof(displayLine), "[%.24s] [Player's Response:]\n%s%s: %s\n", 
                            timestamp(), DIVIDER, activeRecords[i].username, buf);
                        Writen(fileno(stdout), displayLine, n);
                        lowerStr(buf, n); bzero(displayLine, MAXLINE);

                        if (strcmp(buf, "y\n") == 0) // if client wants to continue the previous game
                        {
                            activeRecords[i].gameState = CLI_SERV_PLAY; //start the game
                            activeRecords[i].startTime = time(NULL);
                            
                            //Prompt the client to guess
                            n = snprintf(buf, sizeof(buf), "\n%s[%.24s]\n\n[Attempt No.%d] %s", 
                                DIVIDER, timestamp(), activeRecords[i].attemptsNo + 1, PROMPT_GUESS);
                            Writen(sockfd, buf, n);

                            //display client's information and random number generated for the client
                            n = snprintf(displayLine, sizeof(displayLine), "\nPlayer: %s\nLocal address: %s\nRandom number generated: %d\n",
                                activeRecords[i].username, activeRecords[i].address, activeRecords[i].randomNumber);
                            Writen(fileno(stdout), displayLine, n);
                        }
                        else if (strcmp(buf, "n\n") == 0) // if client wants to restart the previous game
                        {
                            //overwrite the previous game data
                            activeRecords[i].gameState = CLI_SERV_PLAY; //start the game
                            activeRecords[i].startTime = time(NULL);
                            activeRecords[i].randomNumber = (rand() % 99) + 1;
                            activeRecords[i].lowerBound = 1;
                            activeRecords[i].upperBound = 99;
                            activeRecords[i].attemptsNo = 0;
                            
                            //Prompt client to guess number
                            n = snprintf(buf, sizeof(buf), "\n%s[%.24s]\n\n[Attempt No.%d] %s", 
                                DIVIDER, timestamp(), activeRecords[i].attemptsNo + 1, PROMPT_GUESS);
                            Writen(sockfd, buf, n);

                            //display client's information and random number generated for the client
                            n = snprintf(displayLine, sizeof(displayLine), "\nPlayer: %s\nLocal address: %s\nRandom number generated: %d\n",
                                activeRecords[i].username, activeRecords[i].address, activeRecords[i].randomNumber);
                            Writen(fileno(stdout), displayLine, n);
                        }
                        else //invalid response from client
                        {
                            n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s] [System Message:]\n%sInvalid response from %s.\n", 
                                DIVIDER, timestamp(), DIVIDER, activeRecords[i].username);
                            Writen(fileno(stdout), displayLine, n);

                            n = snprintf(buf, sizeof(buf), "\n%s[%.24s]\n\nInvalid input.\n\nEnter Y to continue previous game, N to start a new game: ", 
                                DIVIDER, timestamp());
                            Writen(sockfd, buf, n);
                        }
                        strcpy(activeRecords[i].preBuf, buf);
                        
                        bzero(displayLine, MAXLINE); 
                        n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s] [Server's reply to %s:] %s\n%s", 
                            DIVIDER, timestamp(), activeRecords[i].username, buf, DIVIDER);
                        Writen(fileno(stdout), displayLine, n);
                        break;

                    case CLI_SERV_PLAY: //Client's guess received

                        activeRecords[i].closeFlag = CF_CONNRESET_LATE; //store disconnect record if client terminates abruptly
                        activeRecords[i].attemptsNo++;

                        //display client's information, random number generated for the client, client's attempt count and guess made by client 
                        n = snprintf(displayLine, sizeof(displayLine), "[%.24s] [Player's Response:]\n%sPlayer: %s\nLocal address: %s\nRandom number generated: %d\nAttempt count: %d\nGuess made: %s",
                            timestamp(), DIVIDER, activeRecords[i].username, activeRecords[i].address, activeRecords[i].randomNumber, activeRecords[i].attemptsNo, buf);
                        Writen(fileno(stdout), displayLine, n);
                        lowerStr(buf, n);

                        if (strstr(buf, "quit") != NULL) //if client requests to quit the game
                        {
                            bzero(displayLine, MAXLINE);
                            n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s] [System Message:]\n%s%s [%s] decides to give up and quit.\n",
                                DIVIDER, timestamp(), DIVIDER, activeRecords[i].username, activeRecords[i].address);
                            Writen(fileno(stdout), displayLine, n);

                            n = snprintf(buf, sizeof(buf), "#END\n%s[%.24s]\n\nThe answer is %d. Bye %s! See you next time.\n", 
                                DIVIDER, timestamp(), activeRecords[i].randomNumber, activeRecords[i].username);
                            Writen(sockfd, buf, n); str_cut(buf, 4);

                            bzero(displayLine, MAXLINE); 
                            n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s] [Server's reply to %s:] %s\n%s", 
                                DIVIDER, timestamp(), activeRecords[i].username, buf, DIVIDER);
                            Writen(fileno(stdout), displayLine, n);

                            activeRecords[i].gameState = CLI_CLOSE; //going to terminate client's connection
                            activeRecords[i].closeFlag = CF_QUIT; //connection closed due to quit condition
                            break;
                        }

                        int guess = checkGuess(buf); //validate client's guess

                        if (guess == -1) //invalid guess
                        {
                            activeRecords[i].attemptsNo--; 

                            n = snprintf(buf, sizeof(buf), "\n%s[%.24s]\n\nInvalid guess.\n%s\n", 
                                DIVIDER, timestamp(), PROMPT_GUESS);
                            Writen(sockfd, buf, n);

                            bzero(displayLine, MAXLINE); 
                            n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s] [Server's reply to %s:] %s\n%s", 
                                DIVIDER, timestamp(), activeRecords[i].username, buf, DIVIDER);
                            Writen(fileno(stdout), displayLine, n);
                        }
                        else //if guess is valid, evaluate guess and inform client appropriately
                        {
                            if (guess == activeRecords[i].randomNumber) //Correct guess
                            {
                                if (activeRecords[i].attemptsNo < activeRecords[i].bestAttemptsNo) //Better best attempt no.
                                {
                                    activeRecords[i].bestAttemptsNo = activeRecords[i].attemptsNo;
                                    gameUpdate(pastRecords, activeRecords[i], &gcount); //update client's best attempt no.
                                    gameWrite(pastRecords, gcount);
                                }

                                bzero(displayLine, MAXLINE);
                                n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s] [System Message:]\n%s%s has made the correct guess in %d attempts.\n%s's best record: [%d attempts]\n",
                                    DIVIDER, timestamp(), DIVIDER, activeRecords[i].username, activeRecords[i].attemptsNo, activeRecords[i].username, activeRecords[i].bestAttemptsNo);
                                Writen(fileno(stdout), displayLine, n);

                                n = snprintf(buf, sizeof(buf), "\n%s[%.24s]\n\nCongratulations! %s, you have made the correct guess in %d attempts!\nYour best record is [%d attempts].\nAre you ready to play again?\n\n%s",
                                    DIVIDER, timestamp(), activeRecords[i].username, activeRecords[i].attemptsNo, activeRecords[i].bestAttemptsNo, PROMPT_START);
                                Writen(sockfd, buf, n);

                                for (int j = 0; j <= maxi; j++) //broadcast winner's information to all clients
                                {
                                    sockfd = activeRecords[j].sockfd;

				    //all clients with bestAttemptsNo higher (scored more poorly) than the score of the current winner 
                                    //will receive the broadcast message
                                    if ((sockfd >= 0) && (sockfd != activeRecords[i].sockfd) && (activeRecords[i].attemptsNo < activeRecords[j].bestAttemptsNo))
                                    {
                                        bzero(broadcastMsg, sizeof(broadcastMsg));
                                        n = snprintf(broadcastMsg, sizeof(broadcastMsg), 
                                            "\n%s[%.24s]\n\nAnnouncement: %s[%s] wins the game after %d attempts!\nTry to beat %s's best record [%d attempts]!\n\nPlease resume your input here: ", 
                                            DIVIDER, timestamp(), activeRecords[i].username, activeRecords[i].address, activeRecords[i].attemptsNo, activeRecords[i].username, activeRecords[i].bestAttemptsNo);
                                        Writen(sockfd, broadcastMsg, n);
                                    }
                                }

                                activeRecords[i].gameState = SERV_WAIT_2; //allow client to play again
                                activeRecords[i].closeFlag = CF_CONNRESET_EARLY; //does not store disconnect record
                            }
                            else if (abs(guess - activeRecords[i].randomNumber) <= 3) //client's guess is ±3 from correct guess
                            {
                                n = snprintf(buf, sizeof(buf), "\n%s[%.24s]\n\n%s, you are almost there.\nTry again. (Tips: %s)\n\n[Attempt No.%d] %s", 
                                    DIVIDER, timestamp(), activeRecords[i].username, range(&activeRecords[i], guess), activeRecords[i].attemptsNo + 1, PROMPT_GUESS);
                                Writen(sockfd, buf, n);
                            }
                            else if (guess > activeRecords[i].randomNumber)
                            {
                                n = snprintf(buf, sizeof(buf), "\n%s[%.24s]\n\n%s, your guess is too high.\nTry again. (Tips: %s)\n\n[Attempt No.%d] %s", 
                                    DIVIDER, timestamp(), activeRecords[i].username, range(&activeRecords[i], guess), activeRecords[i].attemptsNo + 1, PROMPT_GUESS);
                                Writen(sockfd, buf, n);
                            }
                            else if (guess < activeRecords[i].randomNumber)
                            {
                                n = snprintf(buf, sizeof(buf), "\n%s[%.24s]\n\n%s, your guess is too low.\nTry again. (Tips: %s)\n\n[Attempt No.%d]. %s", 
                                    DIVIDER, timestamp(), activeRecords[i].username, range(&activeRecords[i], guess), activeRecords[i].attemptsNo + 1, PROMPT_GUESS);
                                Writen(sockfd, buf, n);
                            }
                            //display response
                            n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s] [Server's reply to %s:] %s\n%s", 
                                DIVIDER, timestamp(), activeRecords[i].username, buf, DIVIDER);
                            Writen(fileno(stdout), displayLine, n);
                            strcpy(activeRecords[i].preBuf, buf);
                        }
                        break;

                    case CLI_CLOSE: //terminate client's connection

                        Close(sockfd); 
                        FD_CLR(sockfd, &allset);
                        activeRecords[i].sockfd = -1; 

                        switch (activeRecords[i].closeFlag)
                        {
                            case CF_CONNRESET_LATE: //store disconnect record if client terminates abruptly
  
                                  disconnUpdate(disconnRecords, activeRecords[i], &dcount); //add new disconnect record
                                  disconnWrite(disconnRecords, dcount);
                        
                            case CF_CONNRESET_EARLY: //does not store disconnect record

                                n = snprintf(displayLine, sizeof(displayLine), "[%.24s] [System Message:]\n%sConnection from %s [%s] terminated prematurely.\n%s",
                                    timestamp(), DIVIDER, activeRecords[i].username, activeRecords[i].address, DIVIDER);
                                Writen(fileno(stdout), displayLine, n);
                                
                            default:
                                break;
                        }
                }
                if (--nready <= 0) 
                    break;				
            }

            elapsedTime = (long int)(time(NULL) - activeRecords[i].idleStartTime);

            if (elapsedTime >= KEEP_ALIVE_PERIOD) //terminate client's connection after client being idle for 120 seconds
            {
                n = snprintf(buf, sizeof(buf), "\n%s[%.24s]\n\n%s, you have been idle for %ld seconds.\nConnection terminated due to keep-alive timeout.\n",
                    DIVIDER, timestamp(), activeRecords[i].username, elapsedTime);
                Writen(sockfd, buf, n);

                n = snprintf(displayLine, sizeof(displayLine), "[%.24s] [Server's reply to %s:] %s\n%s",
                    timestamp(), activeRecords[i].username, buf, DIVIDER);
                Writen(fileno(stdout), displayLine, n);
                
                Close(sockfd); //terminate client's connection
                FD_CLR(sockfd, &allset);
                activeRecords[i].sockfd = -1;

                bzero(displayLine, MAXLINE);
                n = snprintf(displayLine, sizeof(displayLine), "[%.24s] [System Message:]\n%sConnection from %s [%s] terminated due to keep-alive timeout.\n%s",
                    timestamp(), DIVIDER, activeRecords[i].username, activeRecords[i].address, DIVIDER);
                Writen(fileno(stdout), displayLine, n); 

                if (activeRecords[i].closeFlag == CF_CONNRESET_LATE) //store disconnect record if client terminates abruptly
                {
                    disconnUpdate(disconnRecords, activeRecords[i], &dcount); //add new disconnect record
                    disconnWrite(disconnRecords, dcount);
                }
            }
            else if ((elapsedTime >= KEEP_ALIVE_PERIOD / 2) && (activeRecords[i].warningBit == 0)) //send warning message to client after client being idle for 60 seconds
            {
                n = snprintf(buf, sizeof(buf), "\n%s[%.24s]\n\n[WARNING] %s, you have been idle for %ld seconds.\nConnection will be terminated after another %ld seconds.\n%s[%.24s] [Repeat Message:]%s",
                    DIVIDER, timestamp(), activeRecords[i].username, elapsedTime, KEEP_ALIVE_PERIOD / 2, DIVIDER, timestamp(), activeRecords[i].preBuf);
                Writen(sockfd, buf, n);

                n = snprintf(displayLine, sizeof(displayLine), "[%.24s] [Server's reply to %s:] %s\n%s",
                    timestamp(), activeRecords[i].username, buf, DIVIDER);
                Writen(fileno(stdout), displayLine, n);

                activeRecords[i].warningBit = 1; //warning message sent and will be resent after warning bit is cleared
            }         
        }//end of for (check client data)
        
        if (FD_ISSET(fileno(stdin), &rset)) 
        { 
            bzero(buf, MAXLINE);
            bzero(displayLine, MAXLINE);

            if ((n = Read(fileno(stdin), buf, MAXLINE)) == 0)
              break;
             
            lowerStr(buf, n);
            
            if(strcmp(buf, "a\n") == 0) //show active records
            {
                rcount = 0;
                n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s]\n\n%15s | %15s |  %8s | %13s | %12s | %13s | %12s | %s\n", 
                             DIVIDER, timestamp(), "Username", "Address", "rand no.", "Attempt Count", "Best Score", "Time spent(s)", "Idle time(s)", "Status");
                Writen(fileno(stdout), displayLine, n); 
                
                for (i = 0; i <= maxi; i++) 
                {	
                    if (activeRecords[i].sockfd >= 0)
                    {
                        if (activeRecords[i].startTime == (time_t)(-1))
                            timeSpent = (long int)(-999);
                        else
                            timeSpent = (long int)(time(NULL) - activeRecords[i].startTime);
                        
                        n = snprintf(displayLine, sizeof(displayLine), "%15s | %15s |  %8d | %13d | %12d | %13ld | %12ld | %s\n", 
                            activeRecords[i].username, activeRecords[i].address, activeRecords[i].randomNumber, activeRecords[i].attemptsNo, 
                            activeRecords[i].bestAttemptsNo % 100, timeSpent, (long int)(time(NULL) - activeRecords[i].idleStartTime), 
                            getGameState(activeRecords[i].gameState));
                         Writen(fileno(stdout), displayLine, n); 
                         rcount++;
                    }    
                }
                bzero(displayLine, MAXLINE);
                n = snprintf(displayLine, sizeof(displayLine), "\nThere are %d active game record(s).\n%s", rcount, DIVIDER);
              Writen(fileno(stdout), displayLine, n);
            }
            else if(strcmp(buf, "d\n") == 0) //show disconnect records
            {
                rcount = 0;
                n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s]\n\n%15s | %8s | %s\n", 
                    DIVIDER, timestamp(), "Username", "rand no.", "Attempt Count");
                Writen(fileno(stdout), displayLine, n); 

                for(i = 0; i < dcount; i++)
                {
                     n = snprintf(displayLine, sizeof(displayLine), "%15s | %8d | %d\n", 
                         disconnRecords[i].username, disconnRecords[i].randomNumber, disconnRecords[i].attemptsNo);
                     Writen(fileno(stdout), displayLine, n); 
                     rcount++;
                }
                bzero(displayLine, MAXLINE);
                n = snprintf(displayLine, sizeof(displayLine), "\nThere are %d disconnected client(s).\n%s", rcount, DIVIDER);
                Writen(fileno(stdout), displayLine, n);
            }
            else if(strcmp(buf, "p\n") == 0) //show past records
            {
                rcount = 0;
                n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s]\n\n%15s | %s\n", 
                    DIVIDER, timestamp(), "Username", "Best Score");
                Writen(fileno(stdout), displayLine, n); 
                 
                for(i = 0; i < gcount; i++)
                {
                     n = snprintf(displayLine, sizeof(displayLine), "%15s | %d\n",  pastRecords[i].username, pastRecords[i].bestAttemptsNo);
                     Writen(fileno(stdout), displayLine, n); 
                     rcount++;
                }
                bzero(displayLine, MAXLINE);
                n = snprintf(displayLine, sizeof(displayLine), "\nThere are %d past game record(s).\n%s", rcount, DIVIDER);
                Writen(fileno(stdout), displayLine, n);
            }
            else if (strcmp(buf, "sleep1\n") == 0) //pause server for 72 seconds (to demonstrate unreachable server)
            {
                n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s] [System Message:] [Server starts to sleep for %ld seconds.]\n%s",
                    DIVIDER, timestamp(), (long int)(KEEP_ALIVE_PERIOD * 0.6), DIVIDER);
                Writen(fileno(stdout), displayLine, n);
                
                sleep(KEEP_ALIVE_PERIOD * 0.6);

                bzero(displayLine, MAXLINE);
                n = snprintf(displayLine, sizeof(displayLine), "[%.24s] [System Message:] [Server wakes up and resumes working.]\n%s", 
                    timestamp(), DIVIDER);
                Writen(fileno(stdout), displayLine, n);
            }
            else if (strcmp(buf, "sleep2\n") == 0) //pause server for 144 seconds (to demonstrate unreachable server)
            {
                n = snprintf(displayLine, sizeof(displayLine), "%s[%.24s] [System Message:] [Server starts to sleep for %ld seconds.]\n%s",
                    DIVIDER, timestamp(), (long int)(KEEP_ALIVE_PERIOD * 1.2), DIVIDER);
                Writen(fileno(stdout), displayLine, n);

                sleep(KEEP_ALIVE_PERIOD * 1.2);

                bzero(displayLine, MAXLINE);
                n = snprintf(displayLine, sizeof(displayLine), "[%.24s] [System Message:] [Server wakes up and resumes working.]\n%s",
                    timestamp(), DIVIDER);
                Writen(fileno(stdout), displayLine, n);
            }
            else if(strcmp(buf, "\n") == 0);   
            else //show valid user input
            {
                n = snprintf(displayLine, sizeof(displayLine), "%sList of valid inputs: \n%s\n%s\n%s\n%s%ld seconds\n%s%ld seconds\n%s\n%s",
                    DIVIDER,
                    "a - Show active game records",
                    "d - Show disconnect game records",
                    "p - Show past game records",
                    "sleep1 - Pause the server for ", (long int)(KEEP_ALIVE_PERIOD * 0.6),
                    "sleep2 - Pause the server for ", (long int)(KEEP_ALIVE_PERIOD * 1.2),
                    "help - Show valid inputs",
                    DIVIDER);                 
                Writen(fileno(stdout), displayLine, n);    
            }
        }//end of if (input is readable)
        
        elapsedTime = (long int)(time(NULL) - lastBackupTime);

        if (elapsedTime >= BACKUP_INTERVAL) //perform backup every 60 seconds
        {
            saveActiveRecords(activeRecords, maxi);
            n = snprintf(displayLine, sizeof(displayLine), "[%.24s] [System Message:] [Performed Backup.]\n%s", timestamp(), DIVIDER);
            Writen(fileno(stdout), displayLine, n); 
            lastBackupTime = time(NULL);          
        }  
    }//end of for (infinite loop)
}//end of main
