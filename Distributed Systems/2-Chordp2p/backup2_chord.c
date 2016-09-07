#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>

#define CMD_LENGTH 15
#define BACKLOG 10
#define PORT 3940
#define MAXDATASIZE 64
#define MAX_NUM_NODES 256
#define FINGER_SIZE 8

pthread_mutex_t command_lock;
int command_continue;

int file_write; //KC
FILE *file; //KC

struct finger_table_entry{
	int interval_start;
	int interval_end;
	int node;	
};

struct node_thread_data{
	int node_num;
	int node_successor;
	int node_predecessor;
	char keys[MAX_NUM_NODES];
};

void *get_in_addr(struct sockaddr *sa){
    if(sa->sa_family == AF_INET){
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int closestPrecedingFinger(int node_number, int id, struct finger_table_entry *fingers){
	int i;
	for(i = FINGER_SIZE; i >= 0; i--){ //TODO finger size
		if(fingers[i].node >= node_number && fingers[i].node <= id){
			return fingers[i].node;
		}
	}
	return node_number;
}

int findPredecessor(int node_number, int node_successor, int id){
	while(!(node_number <= id && node_successor >= id)){ //id not inbetween them
		//Send request closest finger message
		char buf1[64];
		char buf2[64];
       		char send_buf[64];
        	sprintf(send_buf, "%s %i", "closestfinger", id); 
        	int fd = sendMessage(node_number, send_buf);
		int new_node, new_successor;
		//recv 
		if(recv(fd, buf1, 64, 0) == -1){
            perror("recv\n");
        }
		close(fd);
		sscanf(buf1, "%i", &new_node);
		int successor_fd = sendMessage(new_node, "successor");
		if(recv(successor_fd, buf2, 64, 0) == -1){
			perror("recv\n");
		}
		close(successor_fd);
		//Send get successor message	
		sscanf(buf2, "%i", &new_successor);
		node_number = new_node;
		node_successor = new_successor;		
	}
	return node_successor; //Just return successor	
}

int findSuccessor(int node_number, int node_successor, int id){
	int predecessor = findPredecessor(node_number, node_successor, id);
	return predecessor;
}

//Return file descriptor to receive response
int sendMessage(int dstNode, char *msg){
	int sockfd, numbytes;
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];
    char port_num[5];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	
	//Convert node number to port number, convert port number to string
	dstNode += PORT;
	sprintf(port_num, "%i", dstNode);	
	
	if ((rv = getaddrinfo("localhost", port_num, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
			p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("client: connect");
			continue;
		}
		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}
	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
            s, sizeof s);
	printf("client: connecting to %s\n", s);

	freeaddrinfo(servinfo);

	if(send(sockfd, msg, MAXDATASIZE, 0) == -1){
		perror("send");
	}

	return sockfd;
}


int initializeServer(int node_num){//char *port_num){
	int sockfd, max_clients, num_connections, clientfd;
	struct addrinfo hints, *servinfo, *p;
	int yes = 1;
	int rv, rc, new_fd;
	long t;
	char port_num[5];
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	node_num += PORT;
	sprintf(port_num, "%i", node_num);

	if((rv = getaddrinfo(NULL, port_num, &hints, &servinfo)) != 0){
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(rv));
	return 1;
	}

	for(p = servinfo; p != NULL; p = p->ai_next){
		if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
			perror("server: socket");
			continue;
		}
		if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1){
			perror("setsockopt");
			exit(1);
		}
		if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
			close(sockfd);
			perror("server: bind");
			continue;
		}
		break;
	}

	if(p == NULL){
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}

	freeaddrinfo(servinfo);

	if(listen(sockfd, BACKLOG) == -1){
		perror("listen");
		exit(1);
	}

	return sockfd;
}

void init_finger_table( int node_num, int* node_successor, int* node_predecessor, struct finger_table_entry* finger)
{
	//Finger table is always initialized by asking node 0.
	int successor_predecessor;
	int i;
	int node0_successor;
	char buf1[64];
	char buf2[64];
	char send_buf[64];
	char buf3[64];

	//Ask node 0 for its successor
	int fd = sendMessage(0, "successor");
	//recv 
	if(recv(fd, buf1, 64, 0) == -1){
        perror("recv\n");
    }
	close(fd);
	sscanf(buf1, "%i", &node0_successor);

	finger[1].node = findSuccessor( 0, node0_successor ,finger[1].interval_start );

	//Ask node_successor for its predecessor
	fd = sendMessage(*node_successor, "predecessor");
	if (recv( fd, buf2, 64, 0 ) == -1)
	{
		perror("recv\n");
	} 
	close(fd);
	sscanf( buf2, "%i", &successor_predecessor);
	*node_predecessor = successor_predecessor;

	sprintf( send_buf, "setpredecessor %i", node_num );
	fd = sendMessage(*node_successor, send_buf);
	// recv?
	close(fd);
	for( i=1 ; i<FINGER_SIZE ; i++ )
	{
		if( finger[i+1].interval_start >= node_num && 
			finger[i+1].interval_start < finger[i].node )
		{
			finger[i+1].interval_start = finger[i].node;
		}
		else
		{
			finger[i+1].node = findSuccessor( 0, node0_successor, finger[i+1].interval_start );
		}
	}
}

void update_finger_table( int node_number, int node_predecessor, int s, int i, struct finger_table_entry* finger )
{
	int fd;
	char send_buf[64];

	if( s >= node_number && s < finger[i].node )
	{
		finger[i].node = s;
		// Tell node_predecessor to update its finger table with (s, i) are parameters
		sprintf( send_buf, "setfingertable %i %i", s, i);
		fd = sendMessage( node_predecessor, send_buf);
		close(fd);
	}
}

void update_others( int node_num, int node_successor )
{
	int node0_successor;
	int i;
	int p;
	int id;
	char buf1[64];
	char send_buf[64];

	//Ask node 0 for its successor
	int fd = sendMessage(0, "successor");
	//recv 
	if(recv(fd, buf1, 64, 0) == -1){
        perror("recv\n");
    }
	close(fd);
	sscanf(buf1, "%i", &node0_successor);

	for( i=1 ; i <= FINGER_SIZE ; i++ )
	{
		id = node_num - pow(2.0, (double)(i-1));
		p = findPredecessor( 0, node0_successor, id);
		// Tell p to update it's finger table with parameters (n, i)
		//set_finger_table(p, n, i);
		sprintf( send_buf, "setfingertable %i %i", node_num, i);
		fd = sendMessage(p, send_buf);
		close(fd);
	}
}

int join(int node_num, int* node_successor, int* node_predecessor , char* keys ,struct finger_table_entry* finger)
{
	int i;
	int fd;
	char send_buf[64];
	if( node_num != 0)
	{
		init_finger_table(node_num, node_successor, node_predecessor, finger);
		update_others(node_num, *node_successor);
		for( i = *node_predecessor+1; i <= node_num ; i++ )
			keys[i] = 1;

		sprintf( send_buf, "removekeys %i %i", (*node_predecessor)+1, node_num);
		fd = sendMessage( *node_predecessor, send_buf );
		close(fd);
	}
	else
	{
		for( i=1 ; i <= FINGER_SIZE ; i++ )
			finger[i].node = node_num;
		*node_predecessor = node_num;
	}
}
void show(int node_num, char *keys){  //KC
    int i;
    if(file_write == 0)
        printf("%i ", node_num);
    else
        fprintf(file, "%i ", node_num);
    for(i = 0; i < MAX_NUM_NODES; i++){
        if(keys[i] == '1'){
            if(file_write == 0)
                printf("%c ", keys[i]);
            else
                fprintf(file, "%c ", keys[i]);
        }
    }
    if(file_write == 0)
        printf("\n");
    else
        fprintf(file, "\n");
}



int find(int node_num, int node_successor, int key){
    return findSuccessor(node_num, node_successor, key);
}



void *nodeThread(void *data){
	char buf[64];
	char send_buf[64];
	char cmd[10];
	int i, k, j;
	socklen_t sin_size;
	struct finger_table_entry finger[FINGER_SIZE];	// Initialzed upon join
    struct sockaddr_storage their_address;
	char s[INET6_ADDRSTRLEN];
	int sockfd, new_fd;
	struct node_thread_data *thread_data;
	int predecessor; //TODO initialize
	thread_data = (struct node_thread_data*) data;	
	sockfd = initializeServer(thread_data->node_num);
	
	
	while(1){
		sin_size = sizeof their_address;
		new_fd = accept(sockfd, (struct sockaddr *) &their_address, &sin_size);
		if(new_fd == -1){
			perror("accept");
			continue;
		}
		inet_ntop(their_address.ss_family, get_in_addr((struct sockaddr *) &their_address), s, sizeof s);
		printf("server: got connection from %s\n", s);
		
		if(recv(new_fd, buf, 64, 0) == -1){
			perror("recv\n");
		}

		printf("Node received: %s\n", buf);

		if(strstr(buf, "hello") != NULL){ //Just for testing
			printf("finished sent %s\n", buf);
		}
		else if(strstr(buf, "join") != NULL){
			join(thread_data->node_num, &(thread_data->node_predecessor), &(thread_data->node_successor), 
									thread_data->keys ,finger);
		}
		else if( strstr(buf, "successor") ){
			sprintf( send_buf, "%i", thread_data->node_successor );
			if(send(new_fd, send_buf, strlen(send_buf)+1, 0) == -1){
				perror("send");
			}
		}
		else if( strstr(buf, "predecessor") ){
			sprintf( send_buf, "%i", thread_data->node_predecessor );
			if(send(new_fd, send_buf, strlen(send_buf)+1, 0) == -1){
				perror("send");
			}
		}
		else if ( strstr(buf, "setpredecessor") ){
			sscanf( buf, "%i", &i);
			thread_data->node_predecessor = i;
		}
		else if ( strstr(buf, "setfingertable") ){
			sscanf( buf, "%i %i", &k, &i );
			update_finger_table( thread_data->node_num, thread_data->node_predecessor, k, i, finger );
		}
		else if (strstr(buf, "removekeys") ){
			sscanf( buf, "%i %i", &k, &i );
			for( j=k ; j<=i ; j++ )
				thread_data->keys[j] = '0';
		}
		else if(strstr(buf, "find") != NULL){ //KC
            		sscanf(buf, "%s %i", cmd, &k);
            		int find_result = find(thread_data->node_num, finger[1].node, k); //maybe finger[0]
            		printf("Location: %i\n", find_result);
            		//Notify coordinator
    	}
       	else if(strstr(buf, "closestfinger") != NULL){ //KC
           		int id;
           		sscanf(buf, "%s %i", cmd, &id);
           		int finger_result = closestPrecedingFinger(thread_data->node_num, id, finger);
           		char send_buf[64];
           		sprintf(send_buf, "%i", &finger_result);
           		if(send(new_fd, send_buf, 64, 0) == -1){
               		perror("send");
    	       	}
        }
        else if(strstr(buf, "show") != NULL){
            show(thread_data->node_num, thread_data->keys);
        }


		close(new_fd);
		sendMessage(-1, "finished"); //Notify coordinator that command is finished
	}
}

int joinStart(int node_num){
	pthread_t node_thread;
	struct node_thread_data *thread_data = (struct node_thread_data*) malloc(sizeof(struct node_thread_data));
	thread_data->node_num = node_num;
	pthread_create(&node_thread, NULL, nodeThread, (void*) thread_data);
}

void *coordinatorServerThread(void *data){
	socklen_t sin_size;
    struct sockaddr_storage their_address;
    char s[INET6_ADDRSTRLEN];
    int sockfd, new_fd;
	sockfd = initializeServer(-1);
	while(1){
		char buf[64];
		sin_size = sizeof their_address;
		new_fd = accept(sockfd, (struct sockaddr *) &their_address, &sin_size);
		if(new_fd == -1){
			perror("accept");
			continue;
		}
		inet_ntop(their_address.ss_family, get_in_addr((struct sockaddr *) &their_address), s, sizeof s);
		printf("server: got connection from %s\n", s);
	
		if(recv(new_fd, buf, 64, 0) == -1){
            perror("recv\n");
        }
		
		if(strstr(buf, "finished") != NULL){
			pthread_mutex_lock(&command_lock);
			command_continue = 1;
			pthread_mutex_unlock(&command_lock);			
		}
		close(new_fd);		
	}

}

int main(int argc, char *argv[]){
		
	char active_nodes[MAX_NUM_NODES];
	int i;
	pthread_t node_thread;
	pthread_t coordinator_server_thread;
	struct node_thread_data *thread_data = (struct node_thread_data*) malloc(sizeof(struct node_thread_data));
	thread_data->node_num = 0;
	thread_data->node_successor = 0;
	thread_data->node_predecessor = 0;

	pthread_mutex_init(&command_lock, NULL);
	command_continue = 0;

   	if(argc == 3){      //KC
       	printf("open\n");
       	file = fopen(argv[2], "w");
       	if(file == NULL){
           		exit(1);
       	}
       	file_write = 1;
   	}
   	else{
       	file_write = 0;
   	}


	active_nodes[0] = '1';
	thread_data->keys[0] = '1';
	for(i = 1; i < MAX_NUM_NODES; i++){
		active_nodes[i] = '0';
		thread_data->keys[i] = '1';
	}
	pthread_create(&coordinator_server_thread, NULL, coordinatorServerThread, NULL);	
	pthread_create(&node_thread, NULL, nodeThread, (void*) thread_data);

	//int sockfd = initializeServer(-1);
	while(1){
		char user_input[CMD_LENGTH];
		char cmd[8];
		char buf[64];
		int node, key, fd;
	
		printf("Enter command: ");
		fgets(user_input, CMD_LENGTH, stdin);

		if(strstr(user_input, "join") != NULL){
			sscanf(user_input, "%s %i", cmd, &node);
			if(active_nodes[node] == '1'){
				printf("Node %i already joined\n", node);
				continue;
			}
			else{
				//fd = sendMessage(0, "hello");
				active_nodes[node] = '1';
				joinStart(node);
				sleep(2); //Sleep to give node server enough time to set up before sending it the join command... pretty ugly, but should work
				sendMessage(node, "join");
			}
		}
		else if(strstr(user_input, "find") != NULL){
			sscanf(user_input, "%s %i %i", cmd, &node, &key);
			if(active_nodes[node] == '0'){
				printf("Node %i does not exist\n", node);
				continue;
			}
			else{
				//printf("find");
                		sprintf(buf, "%s %i", "find", key);
                		sendMessage(node, buf);

			}
		}
		else if(strstr(user_input, "leave") != NULL){
			sscanf(user_input, "%s %i", cmd, &node);
			if(active_nodes[node] == '0'){
				printf("Node %i does not exist\n", node);
				continue;
			}
			else{	
				active_nodes[node] = '0';
				printf("leave\n");
			}
		}
		else if(strstr(user_input, "show") != NULL && strstr(user_input, "all") != NULL){
			printf("show all\n");
		}
		else if(strstr(user_input, "show") != NULL){
			sscanf(user_input, "%s %i", cmd, &node);
			if(active_nodes[node] == '0'){
				printf("Node %i does not exist\n", node);
				continue;
			}
			else{
				//printf("show\n");
                		sprintf(buf, "%s %i", "show", node);
                		sendMessage(node, buf);
			}
		}
		else{
			printf("invalid command, try again\n");
			continue;
		}

		//Wait until the command is finished
		pthread_mutex_lock(&command_lock);
		while(command_continue == 0){
			pthread_mutex_unlock(&command_lock);
		}
		pthread_mutex_lock(&command_lock);
		command_continue = 0;
		pthread_mutex_unlock(&command_lock);
		
		//TODO add close fd
	}
	return 0;
}
