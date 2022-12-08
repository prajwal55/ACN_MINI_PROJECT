#include <time.h>
#include <stdio.h>

enum cli_state_t
{
    CLI_SENT,
    CLI_RCVD
};

enum game_state_t
{ 
    SERV_WAIT_1,
    SERV_WAIT_2,
    SERV_WAIT_3,
    CLI_SERV_PLAY, 
    CLI_CLOSE 
};

enum close_flag_t
{
    CF_CONNRESET_EARLY,
    CF_CONNRESET_LATE,
    CF_NOTREADY,
    CF_QUIT
};

struct GameRecord
{
    int sockfd;
    char username[30];
    char address[20];
    char preBuf[MAXLINE];
    enum game_state_t gameState;
    enum close_flag_t closeFlag;
    int warningBit;
    int randomNumber;
    int lowerBound;
    int upperBound;
    int attemptsNo;
    int bestAttemptsNo;
    time_t idleStartTime;
    time_t startTime;
};

struct PastGameRecord
{
    char username[30];
    int bestAttemptsNo;
};

struct DisconnectGameRecord
{
    char username[30];
    int randomNumber;
    int lowerBound;
    int upperBound;
    int attemptsNo;
};

//***************************
//  function definitions
//***************************

//Function to remove the characters in str before begin 
int str_cut(char *str, int begin)
{
    int oriLen = strlen(str);
    memmove(str, str + begin, oriLen - begin + 1);
    return oriLen - begin;
}

//Function to check whether the string str contains the prefix pre
bool prefix(const char *pre, const char *str)
{
    return strncmp(pre, str, strlen(pre)) == 0;
}

//Function to return current time in string
char* timestamp()
{
    time_t now = time(NULL);
    return(ctime(&now));
}

//Function to return game state in string
char *getGameState(enum game_state_t gameState)
{
    char *buf = malloc(sizeof(char)*30);

    switch(gameState)
    {
        case SERV_WAIT_1: strcpy(buf, "SERV_WAIT_1"); break;
        case SERV_WAIT_2: strcpy(buf, "SERV_WAIT_2"); break;
        case SERV_WAIT_3: strcpy(buf, "SERV_WAIT_3"); break;
        case CLI_SERV_PLAY: strcpy(buf, "CLI_SERV_PLAY"); break;
        case CLI_CLOSE: strcpy(buf, "CLI_CLOSE");
    }
    return buf;
}

//Function to validate client's guess
int checkGuess(char input[])
{
    int number;
    int isValid = sscanf(input, "%d", &number);

    if (isValid != 1)
        return -1;
    else if (number < 1 || number > 99)
        return -1;
    else
        return number;
}

//Function to change string to lower case
void lowerStr(char str[], int n)
{
    for (int i = 0; i < n; i++)
    {
        str[i] = tolower(str[i]);
    }
}

//Function to change lower bound and upper bound of client's guess range
char *range(struct GameRecord *gameRecord, int guess)
{
    char *buf = malloc(sizeof(char)*10);

    if (guess < gameRecord->randomNumber && guess >= gameRecord->lowerBound)
        gameRecord->lowerBound = guess + 1;
    else if (guess > gameRecord->randomNumber && guess <= gameRecord->upperBound)
        gameRecord->upperBound = guess - 1;

    snprintf(buf, sizeof(char)*10, "[%d - %d]", gameRecord->lowerBound, gameRecord->upperBound);
    return buf;
}

//Function to read past game records from past_game_record.txt
int gameRead(struct PastGameRecord* pastRecords)
{
    FILE* fptr;
    int count = 0;
    char username[30];

    //Create file named past_game_record.txt, if not exist
    fptr = fopen("past_game_record.txt", "a");
    fclose(fptr);
    fptr = fopen("past_game_record.txt", "r");

    while (fgets(username, 30, fptr) != NULL)
    {
        sscanf(username, "%s\n", pastRecords[count].username);
        fscanf(fptr, "%d\n", &pastRecords[count++].bestAttemptsNo);
    }
    fclose(fptr);
    return count;
}

//Function to obtain client's previous best attempt no. from past game records, if exists
int getBestAttemptsNo(const struct PastGameRecord* pastRecords, const char* username, int count)
{
    for (int i = 0; i < count; ++i)
    {
        if (strncmp(username, pastRecords[i].username, strlen(username)) == 0)
        {
            return pastRecords[i].bestAttemptsNo;
        }
    }
    return 100; //This should be sufficiently large
}

//Function to update client's best attempt no.
void gameUpdate(struct PastGameRecord* pastRecords, struct GameRecord activeRecord, int* count)
{
    int i;

    for (i = 0; i < *count; i++)
    {
        if (strncmp(activeRecord.username, pastRecords[i].username, strlen(activeRecord.username)) == 0)
        {
            pastRecords[i].bestAttemptsNo = activeRecord.bestAttemptsNo;
            return;
        }
    }
    ++(*count);
    strcpy(pastRecords[i].username, activeRecord.username);
    pastRecords[i].bestAttemptsNo = activeRecord.bestAttemptsNo;
}

//Function to write past game records to past_game_record.txt
void gameWrite(const struct PastGameRecord* pastRecords, int count)
{
    FILE* fptr;
    fptr = fopen("past_game_record.txt", "w");

    for (int i = 0; i < count; i++)
    {
        fprintf(fptr, "%s\n", pastRecords[i].username);
        fprintf(fptr, "%d\n", pastRecords[i].bestAttemptsNo);
    }
    fclose(fptr);
}

//Function to read disconnect game records from disconnect_game_record.txt
int disconnRead(struct DisconnectGameRecord *disconnRecords)
{
    FILE* fptr;
    int count = 0;
    char username[30];

    //Create file named disconnect_game_record.txt, if not exist
    fptr = fopen("disconnect_game_record.txt", "a");
    fclose(fptr);
    fptr = fopen("disconnect_game_record.txt", "r");

    while (fgets(username, 30, fptr) != NULL)
    {
        sscanf(username, "%s\n", disconnRecords[count].username);
        fscanf(fptr, "%d\n", &disconnRecords[count].randomNumber);
        fscanf(fptr, "%d\n", &disconnRecords[count].lowerBound);
        fscanf(fptr, "%d\n", &disconnRecords[count].upperBound);
        fscanf(fptr, "%d\n", &disconnRecords[count++].attemptsNo);
    }
    fclose(fptr);
    return count;
}

//Function to obtain client's disconnect game record, if exists
int getDisconnRecord(struct DisconnectGameRecord* disconnRecords, const char* username, int *count, int *randomNumber, int *lowerBound, int *upperBound, int *attemptsNo)
{
    int flag = 0;

    for (int i = 0; i < *count; ++i)
    {
        if (strncmp(username, disconnRecords[i].username, strlen(username)) == 0)
        {
            if (flag == 0) //only for first disconnect game record
            {
                *randomNumber = disconnRecords[i].randomNumber;
                *lowerBound = disconnRecords[i].lowerBound;
                *upperBound = disconnRecords[i].upperBound;
                *attemptsNo = disconnRecords[i].attemptsNo;
            }

            for (int j = i; j < *count - 1; ++j) 
            {
                strcpy(disconnRecords[j].username, disconnRecords[j + 1].username);
                disconnRecords[j].randomNumber = disconnRecords[j + 1].randomNumber;
                disconnRecords[j].lowerBound = disconnRecords[j + 1].lowerBound;
                disconnRecords[j].upperBound = disconnRecords[j + 1].upperBound;
                disconnRecords[j].attemptsNo = disconnRecords[j + 1].attemptsNo;
            }
            --(*count); ++flag; --i; //continue from this index, i
        }
    }
    return flag;
}

//Function to add new disconnect game record 
void disconnUpdate(struct DisconnectGameRecord* disconnRecords, struct GameRecord activeRecord, int* count)
{
    strcpy(disconnRecords[*count].username, activeRecord.username);
    disconnRecords[*count].randomNumber = activeRecord.randomNumber;
    disconnRecords[*count].lowerBound = activeRecord.lowerBound;
    disconnRecords[*count].upperBound = activeRecord.upperBound;
    disconnRecords[(*count)++].attemptsNo = activeRecord.attemptsNo;
}

//Function to write disconnect game records to disconnect_game_record.txt
void disconnWrite(const struct DisconnectGameRecord* disconnRecords, int count)
{
    FILE* fptr;
    fptr = fopen("disconnect_game_record.txt", "w");

    for (int i = 0; i < count; i++)
    {
        fprintf(fptr, "%s\n", disconnRecords[i].username);
        fprintf(fptr, "%d\n", disconnRecords[i].randomNumber);
        fprintf(fptr, "%d\n", disconnRecords[i].lowerBound);
        fprintf(fptr, "%d\n", disconnRecords[i].upperBound);
        fprintf(fptr, "%d\n", disconnRecords[i].attemptsNo);
    }
    fclose(fptr);
}

//Function to save the active records into a text file
void saveActiveRecords(const struct GameRecord* activeRecords, int count)
{
    FILE* fptr;
    fptr = fopen("active_game_record.txt", "w");

    for (int i = 0; i <= count; i++)
    {
        if (activeRecords[i].sockfd >= 0 && activeRecords[i].gameState != SERV_WAIT_2)
        {
            fprintf(fptr, "%s\n", activeRecords[i].username);
            fprintf(fptr, "%d\n", activeRecords[i].randomNumber);
            fprintf(fptr, "%d\n", activeRecords[i].lowerBound);
            fprintf(fptr, "%d\n", activeRecords[i].upperBound);
            fprintf(fptr, "%d\n", activeRecords[i].attemptsNo);
        }
    }
    fclose(fptr);
}

//Function to copy records from active_game_record.txt 
//and append to disconnect_game_record.txt
void appendActiveRecords()
{
    FILE *fptr_activeRecords;
    FILE *fptr_disconnRecords;
    char buf[30]; 
    
    fptr_activeRecords = fopen("active_game_record.txt", "r");
    fptr_disconnRecords = fopen("disconnect_game_record.txt", "a");
    
    if(fptr_activeRecords == NULL) 
        return;
        
    while(fgets(buf, sizeof(buf), fptr_activeRecords) != NULL) 
        fprintf(fptr_disconnRecords, "%s", buf);
        
    fclose(fptr_activeRecords);
    fclose(fptr_disconnRecords);
    
    //clear content of active_game_record.txt
    fptr_activeRecords = fopen("active_game_record.txt", "w");
    fclose(fptr_activeRecords);
}