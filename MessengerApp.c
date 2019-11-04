#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#define SIZE_OF_BUFFER 2000					// Length of the buffer
#define SIZE_OF_SENTER_LIST 1000			// # of different (supported) senders
#define SIZE_OF_SENT_LIST 200				// (initial) size of Sent List for each Sender
#define SIZE_OF_AEM_LIST 6					// Length of the AEM LIST
#define MAX 512
#define PORT 2288
#define SA struct sockaddr

/* Message struct, consists of the Sender's AEM, the Receiver's AEM, the
 * Timestamp of the message creation and the Message Body.
 */
struct Message {
	uint32_t AEM_Sender;
	uint32_t AEM_Receiver;
	uint64_t Timestamp;
	char MsgBody[256];
};
/* Sender struct, consists of the Sender's ID, the total (for counting the
 * number of Messages stored) and a dynamic array to store
 * all the messages which already has been sent to this Sender.
 */
struct Sender {
	uint32_t Sender_ID;
	int total;
	struct Message *Sent;
};
// Global Variables used for ease//
struct sigaction sa;
struct Sender SendersList[SIZE_OF_SENTER_LIST];
struct Message Circular_Buf[SIZE_OF_BUFFER];
uint32_t AEM_LIST[SIZE_OF_AEM_LIST] = {8844, 8877, 8997, 8941, 8934, 8880};

// Declaring the global variables //
pthread_mutex_t mutex_buf, mutex_list;
int Rear = 0, currSize = 0, allMsgs = 0, currBufferSize = 0, currRandomMsg = 0;
FILE *fptrServer, *fptrClient;
// Declaring all functions //
uint32_t 		ParseIp(char* str);
void 			createIp(uint32_t AEM, char* ip);
int 			RandomNumber(int lower, int upper);
void 			MsgToString(struct Message* Msg, char* str);
struct Message 	StringToMsg(char* str);
void 			AddToBuffer(struct Message Msg);
void 			ClassifyMessages(struct Message Msg);
void 			MarkMessageAsSent(struct Message Msg, int idxCurrReceiver);
void 			SendMessages(char* SentToIp);
int 			findIdxOfSender(uint32_t Receiver);
int 			WiFiSend(char* SentToIp, char* buff);
void* 			handleInterrupt();
void 			timer_handler(int signum);
void* 			Server_Init();
void* 			Client_Init();

int main() {
	pthread_mutex_init(&mutex_buf, NULL);
	pthread_mutex_init(&mutex_list,NULL);
	pthread_t thread_S, thread_C, thread_NewMsg;
	int Server, Client, Creator;

	Server = pthread_create(&thread_S, NULL, Server_Init, NULL);
	if (Server) {
		printf("Error:unable to create Server thread, %d\n", Server);
		exit(-1);
	}

	Client = pthread_create(&thread_C, NULL, Client_Init, NULL);
	if (Client) {
		printf("Error:unable to create Client thread, %d\n", Client);
		exit(-1);
	}
	Creator = pthread_create(&thread_NewMsg, NULL, handleInterrupt, NULL);
	if (Creator) {
		printf("Error:unable to create Creator thread, %d\n", Creator);
		exit(-1);
	}

	pthread_join(thread_S, NULL);
	pthread_join(thread_C, NULL);
	pthread_join(thread_NewMsg, NULL);

	pthread_mutex_destroy(&mutex_buf);
	pthread_mutex_destroy(&mutex_list);
	printf("Finish!\n");
}
/* Take as input an AEM and creates the appropriate IP */
void createIp(uint32_t AEM, char* ip) {
	if (AEM < 1000 || AEM > 9999) return;
	uint8_t num0 = AEM / 100;
	uint8_t num1 = AEM % 100;
	sprintf(ip, "10.0.%hhu.%hhu", num0, num1);
}
/* Take as input an Ip and return the associated AEM */
uint32_t ParseIp(char* str){
	uint32_t AEM;
	uint8_t	XX, YY;

	sscanf(str,"10.0.%hhu.%hhu", &XX, &YY);
	AEM = (XX * 100) + YY;
	return AEM;
}
/* Generate a random number in [lower, upper] */
int RandomNumber(int lower, int upper){
	srand(time(0));
	return (rand() % (upper - lower + 1)) + lower;
}
/* Take as input a Message and transform it into a string, acceptable to send*/
void MsgToString(struct Message* Msg, char* str){
	sprintf(str, "%u_%u_%llu_", Msg->AEM_Sender, Msg->AEM_Receiver, Msg->Timestamp);
	sprintf(str + strlen(str), Msg->MsgBody);
}
/* Take as input a string and seperate the components to create a Message */
struct Message StringToMsg(char* str) {
	struct Message Msg;
	Msg.AEM_Sender   = atoi(strtok(str,  "_"));
	Msg.AEM_Receiver = atoi(strtok(NULL, "_"));
	Msg.Timestamp    = atoi(strtok(NULL, "_"));
	strcpy(Msg.MsgBody, strtok(NULL, "_"));
	return Msg;
}
/* Checks if 2 Messages are the same (at every field) */
int isDuplicate(struct Message Msg, struct Message list){
	return (list.AEM_Sender == Msg.AEM_Sender) && (list.AEM_Receiver == Msg.AEM_Receiver)
			&& list.Timestamp == Msg.Timestamp    && (strcmp(list.MsgBody, Msg.MsgBody) == 0) ;
}
/* Take as input a Message. Checks if the Message its a duplicate of an
 * existing message of the buffer.
 * If yes, return.
 * Otherwise, check if the buffer is full. (If yes, zero the index variable).
 * Then, add the message to the buffer. Finally, after the 1st zeroing of
 * the index variable, the BufferSize is the whole Buffer. (Otherwise, the
 * index variable determines the BufferSize.)
 */
void AddToBuffer(struct Message Msg){
// Do not let another Thread (only Server OR Creator)
	pthread_mutex_lock(&mutex_buf);
// Check for duplicates at existing messages //
	for (int i = 0; i < currBufferSize; i++){
// If duplicate was found do not continue check the other messages, return from function //
		if (isDuplicate(Msg, Circular_Buf[i])) {
			pthread_mutex_unlock(&mutex_buf);
			return;
		}
	}
// if Full, then overwrite rear and delete
	if (Rear == SIZE_OF_BUFFER){
		Rear = 0;
	}
	Circular_Buf[Rear] = Msg;
	Rear++;
// Until BUffer is full and overwrite Rear, the condition will be false and currBufferSize = Rear.
// then, at the zeroing of Rear (1999 > 0) condition will be always true.
	currBufferSize = (currBufferSize > Rear) ? SIZE_OF_BUFFER : Rear;
	pthread_mutex_unlock(&mutex_buf);
	return;
}
/* Take as input a Message. Checks if the Sender was an old or a new one.
 * For the old ones, checks if the Message is a duplicate.
 * 	If YES, then return.
 * 	Otherwrise, check if Sent array is full. (If YES, realloc more space).
 * 	Then, classify the Message as Sent to this Sender.
 * For the new ones, create a new Sender and classify the Message as Sent
 * to this (new) Sender. Finally, increase the size of the Sender's List.
 */
void ClassifyMessages(struct Message Msg){
// currSize -> Current Size of the Sender's List
// total -> Each time, the current size of each (Struct Message) Sent (for each Sender)
// Do not let another Thread (only Server OR Client OR Creator)
	pthread_mutex_lock(&mutex_list);
	for (int i = 0; i < currSize; i++){								// For each sender
		if (Msg.AEM_Sender == SendersList[i].Sender_ID){
			for (int count = 0; count < SendersList[i].total; count++){ 	// For each message of them
// If duplicate was found do not continue check the other messages, return from function //
				if (isDuplicate(Msg, SendersList[i].Sent[count])) {
					pthread_mutex_unlock(&mutex_list);
					return;
				}
			}
// If the programm reaches here, then add the message to Sender Sent Array. //
// int ceilVall = (a / b) + ((a % b) != 0)
			int size = (int) (SendersList[i].total / SIZE_OF_SENT_LIST) +
					         ((SendersList[i].total % SIZE_OF_SENT_LIST) != 0);
			if (SendersList[i].total == size * SIZE_OF_SENT_LIST && size != 0) {
				SendersList[i].Sent = (struct Message *)realloc(SendersList[i].Sent, sizeof(struct Message) * (size+1) * SIZE_OF_SENT_LIST);
			}
			SendersList[i].Sent[SendersList[i].total] = Msg;
			SendersList[i].total++;
			pthread_mutex_unlock(&mutex_list);
			return;
		}
	}
// If the programm reaches here, then the Sender ID wasn't found (or SendersList was empty) //
	struct Sender NewSender;
	NewSender.Sender_ID = Msg.AEM_Sender;
	NewSender.total = 0;
	NewSender.Sent = (struct Message *)malloc(sizeof(struct Message)*SIZE_OF_SENT_LIST);
	SendersList[currSize] = NewSender;
	SendersList[currSize].Sent[SendersList[currSize].total] = Msg;
	SendersList[currSize].total++;
	currSize++;
	pthread_mutex_unlock(&mutex_list);
	return;
}
/* Take as input a Message and the index of the Current Receiver.Checks if
 * Sent array is full. (If YES, realloc more space). Then, mark the Message
 * as Sent to this Receiver.
 */
void MarkMessageAsSent(struct Message Msg, int idxCurrReceiver){
// int ceilVall = (a / b) + ((a % b) != 0)
	int size = (int) (SendersList[idxCurrReceiver].total / SIZE_OF_SENT_LIST) +
			         ((SendersList[idxCurrReceiver].total % SIZE_OF_SENT_LIST) != 0);
	if (SendersList[idxCurrReceiver].total == size * SIZE_OF_SENT_LIST && size != 0) {
		SendersList[idxCurrReceiver].Sent = (struct Message *)realloc(SendersList[idxCurrReceiver].Sent, sizeof(struct Message) * (size+1) * SIZE_OF_SENT_LIST);
	}
	SendersList[idxCurrReceiver].Sent[SendersList[idxCurrReceiver].total] = Msg;
	SendersList[idxCurrReceiver].total++;
	return;
}
/* Function to generate the Random Message*/
void timer_handler(int signum){
// No input arguments can be given, so -almost- all the variables are global //
// For each interrupt, get Linux Timestamp //
	static uint8_t running = 0;

	if (running == 0) {
		running = 1;
		struct timeval t;
		gettimeofday(&t, NULL);
		uint64_t currTimeStamp = t.tv_sec;
	// Create the random message //
		struct Message RandomMsg =
		{
				8861,
				AEM_LIST[RandomNumber(0, SIZE_OF_AEM_LIST-1)],
				currTimeStamp
		};
		sprintf(RandomMsg.MsgBody, "This is a random message #%d!", currRandomMsg++);
	// Add the message to the buffer //
		AddToBuffer(RandomMsg);
	// Classify this message //
		ClassifyMessages(RandomMsg);
		running = 0;
	}
}
/* Function to initialize the interrupt, which is associated with Creator Thread */
void* handleInterrupt(){
	struct itimerval timer;
	// Set timer_handler as the signal handler for SIGALRM //
	memset (&sa, 0, sizeof (sa));
	sa.sa_handler = &timer_handler;
	sigaction (SIGALRM, &sa, NULL);
	// Initialize itimer to count each time for (Time_Sec_Sleep + Time_uSec_Sleep) //
	timer.it_interval.tv_sec = RandomNumber(60, 300);
	printf("Interrupt per %ld\n", timer.it_interval.tv_sec);
	timer.it_interval.tv_usec = 0;
	timer.it_value = timer.it_interval;
	setitimer (ITIMER_REAL, &timer, NULL);
	while (1){
		pause();
	}
	return NULL;
}
/* Take as input the AEM of a Sender or a Receiver and return the index of him in Senders
 * List. IF the AEM isnt listed, then a NewSender will be created and the his index will
 * be returned.
 */
int findIdxOfSender(uint32_t Receiver){
	int idxCurrReceiver;
		for (int i = 0; i < currSize; i++){
			if (Receiver == SendersList[i].Sender_ID){
				idxCurrReceiver = i;
				return idxCurrReceiver;
			}
		}
// If the programm reaches here, then the Sender ID wasn't found (or SendersList was empty) //
		struct Sender NewSender;
		NewSender.Sender_ID = Receiver;
		NewSender.total = 0;
		NewSender.Sent = (struct Message *)malloc(sizeof(struct Message)*SIZE_OF_SENT_LIST);
		SendersList[currSize] = NewSender;
		idxCurrReceiver = currSize;
		currSize++;
		return idxCurrReceiver;
}
/* Take as input the IP of the  Receiver. Checks if the Receiver its Me.
 * 	If YES, return.
 * Then, search if the Receiver was a Sender before and saves his index
 * at the SendersList. If it is the 1st time communicating a new Sender
 * will be created.
 * ~For the new Senders, the Sent array is empty so the whole buffer must
 * be send to them (and mark them as Sent).
 * ~For the old Senders (now Receivers) each message of the buffer must be
 * checked if it is a duplicate with a message of the Sent array.
 *  If YES, breaks the loop and check the next message of the buffer.
 *  Otherwise, send the message (and mark it as Sent).
 */
void SendMessages(char* SentToIp){
	pthread_mutex_lock(&mutex_list);
	char buff[MAX], sentence[1024];

	uint32_t Receiver = ParseIp(SentToIp);
// DONT FORWARD ALL THE MESSAGES TO ME
	if (Receiver == 8861) {
		pthread_mutex_unlock(&mutex_list);
		return;
	}
// Find if the receiver was a sender before or a new one
	int idxCurrReceiver = findIdxOfSender(Receiver);
// Check if Sent is empty, then send all buffer //
	if (SendersList[idxCurrReceiver].total == 0){
		for(int count = 0; count < currBufferSize; count++){
			if (Circular_Buf[count].AEM_Receiver != 8861){
				char TmpMsg[1024];
				bzero(buff, sizeof(buff));  // Clear buffer
				MsgToString(&Circular_Buf[count], TmpMsg);
				strcpy(buff, TmpMsg);
				printf("Message to Be Sent %s\n",buff);
				if (WiFiSend(SentToIp, buff) == -1){
				//if (send(sockfd, buff, sizeof(buff), MSG_CONFIRM) == -1){
					printf("Error occured on sending ... \n");
				} else {
				// Open the file
					fptrClient = fopen("LogFileClient","a");
					if (fptrClient == NULL) {
						printf("Error creating logFileClient!\n");
					}
	// Erases the data of the 'MAX' bytes of the memory, starting at the locaiton pointed to by buff
					bzero(sentence, 1024);
					// Save it to file //
					sprintf(sentence, "%s_%s", SentToIp, buff);
					fprintf(fptrClient, "%s\n", sentence);
					fclose(fptrClient);
					MarkMessageAsSent(Circular_Buf[count], idxCurrReceiver);
				}
			}
		}
		pthread_mutex_unlock(&mutex_list);
		return;
	}
// Check each message of Sent for duplicates
	int totalMessages = SendersList[idxCurrReceiver].total;
	for (int j = 0; j < currBufferSize; j++){
// for each message of the buffer
		int flag = 1;
		for (int i = 0; i < totalMessages; i++){
			if (isDuplicate(SendersList[idxCurrReceiver].Sent[i], Circular_Buf[j])
					|| (Circular_Buf[j].AEM_Receiver == 8861)) {
				flag = 0;
				break;
			}
		}
		if (flag){
	// If the program reach here the message must be sent
			char TmpMsg[1024];
			bzero(buff, sizeof(buff));  // Clear buffer
			MsgToString(&Circular_Buf[j], TmpMsg);
			strcpy(buff, TmpMsg);
			printf("Message to Be Sent %s\n",buff);
			if (WiFiSend(SentToIp, buff) == -1){
			printf("Error occured on sending ... \n");
			} else {
			// Open the file
				fptrClient = fopen("LogFileClient","a");
				if (fptrClient == NULL) {
					printf("Error creating logFileClient!\n");
				}
				// Erases the data of the 'MAX' bytes of the memory, starting at the locaiton pointed to by buff
				bzero(sentence, 1024);
				// Save it to file //
				sprintf(sentence, "%s_%s", SentToIp, buff);
				fprintf(fptrClient, "%s\n", sentence);
				fclose(fptrClient);
				MarkMessageAsSent(Circular_Buf[j], idxCurrReceiver);
			}
		}
	}
	pthread_mutex_unlock(&mutex_list);
	return;
}
/* Function to send the buff to the IP. At first creates the socket and tries to connect
 * to the server socket. Send the buff (aka the Message) and close the socket. IF any
 * error occured, return -1 to inform the parent function.
 */
int WiFiSend(char* SentToIp, char* buff){
	int sockfd;
	struct sockaddr_in servaddr;
// socket create and varification
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		printf("socket creation failed...\n");
		return -1;
	}
	else
		printf("Client Socket successfully created..\n");
// Manage Ip //
	bzero(&servaddr, sizeof(servaddr));
// assign IP, PORT
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr(SentToIp); //192.168.0.2
	servaddr.sin_port = htons(PORT);
// connect the client socket to server socket
	if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) == -1) {
		printf("connection with the server failed...\n");
		close(sockfd);
		return -1;
	}
	else {
		printf("connected to the server..\n");
	// Send all the unsend Messages //
		if (send(sockfd, buff, strlen(buff), MSG_CONFIRM) == -1){
			close(sockfd);
			return -1;
		}else {
			printf("All good with the message!\n");
			close(sockfd);
			return 0;
		}
	}
}
/* Function to handle my Server. At first, the socket is created, binded to an Ip and
 * then the Server is listening for Clients. For each new Client, read the message,
 * update the logFile, add it to buffer (if it isnt a duplicate) and classify it.
 * At each send, close the socket of the client and return to passive mode.
 */
void* Server_Init(){
	int sockfd, connfd = 0;
	struct sockaddr_in servaddr, cli;
	socklen_t len = sizeof(cli);
	// socket create and verification
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		printf("socket creation failed...\n");
		return NULL;
	}
	else {
		printf("Thread Socket successfully created..\n");
	}
	bzero(&servaddr, sizeof(servaddr));
// assign IP, PORT
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(PORT);
// Binding newly created socket to given IP and verification, bind at success return 0
	if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
		printf("socket bind failed...\n");
		return NULL;
	}
	else
		printf("Socket successfully binded..\n");

	// Now server is ready to listen and verification
	if ((listen(sockfd, 2000)) != 0) {
		printf("Listen failed...\n");
		return NULL;
	}
	else
		printf("Server listening..\n");
// Accept the data packet from client and verification
	for(;;) {
		connfd = accept(sockfd, (SA*)&cli, &len);
		if (connfd < 0) {
			printf("server acccept failed...\n");
		}
		else {
			printf("server acccept the client...\n");
		// Open the file
			fptrServer = fopen("LogFileServer.txt","a");
			if (fptrServer == NULL) {
				printf("Error creating logFileServer!\n");
			}
		// Get the message
			char buff[MAX], sentence[1024], str[INET_ADDRSTRLEN];
		// Erases the data of the 'MAX' bytes of the memory, starting at the locaiton pointed to by buff
			bzero(buff, MAX);
			bzero(sentence, 1024);
		// read the message from client and copy it in buffer
			allMsgs++;
			recv(connfd, buff, sizeof(buff), 0);
		// Get timestamp of receiving the Message //
			struct timeval t;
			gettimeofday(&t, NULL);
			uint64_t currTimeStamp = t.tv_sec;
			int flag = 1, idxSender;
		// Get AEM from IP //
			inet_ntop(AF_INET, &(cli.sin_addr), str, INET_ADDRSTRLEN);
			printf("From IP AEM: %s\n", str);
			printf("From Server Side %s\n", buff);
		// Save it to file //
			sprintf(sentence, "%s_%s__%d_@%llu", str, buff, allMsgs, currTimeStamp);
			fprintf(fptrServer, "%s\n", sentence);
			fclose(fptrServer);
		// Find index of Sender //
			idxSender = findIdxOfSender(ParseIp(str));
		// Create Message from struct //
			struct Message InMsg = StringToMsg(buff);
			int totalMessages = SendersList[idxSender].total;
			// Check each message of Sent for duplicates
			for (int i = 0; i < totalMessages; i++){
				if (isDuplicate(SendersList[idxSender].Sent[i], InMsg)){
					flag = 0;
					break;
				}
			}
			if (flag) {
// If the program reach here the message must be marked as Sent to the "Ip Sender"
				pthread_mutex_lock(&mutex_list);
				MarkMessageAsSent(InMsg, idxSender);
				pthread_mutex_unlock(&mutex_list);
			}
		// Add to the Buffer
			AddToBuffer(InMsg);
		// Classify Message
			ClassifyMessages(InMsg);
		// Close the socket of the Client
			close(connfd);
		}
	}
	return NULL;
}
/* Function to handle the Client. For each AEM, ping the assosciated IP, and if the
 * IP is present then send all the Messages to this IP.
 * After searching all the AEMs, sleep for a period time and then continue.
 */
void *Client_Init(){
	while (1) {
		if (currBufferSize){							// If there is a Msg to Send
			for (int i = 0; i < SIZE_OF_AEM_LIST; i++){
				char buffer[70], Ip[20];
				createIp(AEM_LIST[i], Ip);
				sprintf(buffer, "ping -c1 -w1 %s > /dev/null", Ip);
				printf("buffer = %s \n", buffer);
				if (system(buffer) == 0) {
					SendMessages(Ip);
				}
			}
		}
		sleep(60);
	}
	return NULL;
}
