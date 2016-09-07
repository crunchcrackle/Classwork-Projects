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
#define FINGER_SIZE 9 //m=8 but we start from 1 

pthread_mutex_t command_lock;
pthread_mutex_t phase_count_lock;

int command_continue;
int num_active;
int phase1_count;
int phase2_count;
int num_joins;
int num_finds;

int file_write; //KC
FILE *file; //KC
char file_name[64];

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
	
	for(i = FINGER_SIZE-1; i > 0; i--){
		if(fingers[i].node > node_number && fingers[i].node < id){
			return fingers[i].node;
		}
	}

	return node_number;
}

int findPredecessor(int caller, int node_number, int node_successor, int id, struct finger_table_entry *fingers){
	int count = 0;
	int start_number = node_number;
	
	while(!(node_number < id && node_successor >= id)){ //id not inbetween them
		//Send request closest finger message
		char buf1[64];
		char buf2[64];
       	char send_buf[64];
		int new_node, new_successor;
		if(caller != node_number){//start_number != node_number){
        	sprintf(send_buf, "%s %i", "closestfinger", id); 
        	int fd = sendMessage(node_number, send_buf);
		
			//recv 
			//printf("r0 %i\n", node_number);
			if(recv(fd, buf1, 64, 0) == -1){
				perror("recv0\n");
        	}
			close(fd);
			sscanf(buf1, "%i", &new_node);
		}
		else{
			new_node = closestPrecedingFinger(node_number, id, fingers);
		}
		
		if(new_node == node_number){ //won't make progress, just return
			return new_node;
		}
		if(start_number != new_node && new_node != caller){
			int successor_fd = sendMessage(new_node, "successor");

			
			if(recv(successor_fd, buf2, 64, 0) == -1){
				perror("recv1\n");
			}
			close(successor_fd);
			//Send get successor message	
			sscanf(buf2, "%i", &new_successor);

		}
		else{
			new_successor = node_successor;
		}
		node_number = new_node;
		node_successor = new_successor;
		//if(node_successor == 0){		//maybe?
			//return node_number;
		//}
				
		count++;
	}
	return node_number; //node_successor; //Just return successor	actually, don't
}

int findSuccessor(int caller, int node_number, int node_successor, int id, struct finger_table_entry *finger){
	int successor;
	char buf[64];
	int predecessor = findPredecessor(caller, node_number, node_successor, id, finger);
	if(node_number == predecessor){	//Don't send message to self
		//printf("findSuccessor1: caller %i, id %i, result %i\n", node_number, id, node_successor);
		return node_successor;
	}

	int fd = sendMessage(predecessor, "successor");

	if(recv(fd, buf, 64, 0) == -1){
		perror("recvfindsucc\n");
	}
	sscanf(buf, "%i", &successor);

	//printf("findSuccessor2: caller %i, result %i \n", node_number, successor);
	return successor; //predecessor;
}

//Return file descriptor to receive response
int sendMessage(int dstNode, char *msg){
	int sockfd, numbytes;
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];
    char port_num[5];

    pthread_mutex_lock(&phase_count_lock);
    if(num_joins > 0){  //exp
        phase1_count++;
    }
    else if(num_finds > 0){
        phase2_count++;
    }
    pthread_mutex_unlock(&phase_count_lock);

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
	//printf("client: connecting to %s\n", s);

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

	for(i = 1; i < FINGER_SIZE; i++){
		finger[i].interval_start = (node_num +((int) pow(2.0, (double) (i-1)))) % 256;
	}

	//Ask node 0 for its successor
	int fd = sendMessage(0, "successor");
	//recv 
	//printf("r2\n");
	if(recv(fd, buf1, 64, 0) == -1){
        perror("recv2\n");
    }
	close(fd);
	sscanf(buf1, "%i", &node0_successor);
	//printf("FIND SUCCESSOR interval %i node0 succ %i\n", finger[1].interval_start, node0_successor);
	finger[1].node = findSuccessor(-1, 0, node0_successor ,finger[1].interval_start, finger );
	*node_successor = finger[1].node;
	
	//Ask node_successor for its predecessor
	//fd = sendMessage(*node_successor, "predecessor");
	fd = sendMessage(finger[1].node, "predecessor"); //use finger[1] instead
	//printf("r3\n");
	if (recv( fd, buf2, 64, 0 ) == -1)
	{
		perror("recv3\n");
	} 
	close(fd);
	sscanf( buf2, "%i", &successor_predecessor);
	*node_predecessor = successor_predecessor;
	
	sprintf( send_buf, "setpredecessor %i", node_num );
	//fd = sendMessage(*node_successor, send_buf);
	fd = sendMessage(finger[1].node, send_buf); //use finger[1] instead
	close(fd);
	for( i=1 ; i<FINGER_SIZE-1; i++ )
	{
		if( finger[i+1].interval_start >= node_num && 
			finger[i+1].interval_start < finger[i].node )
		{
			finger[i+1].node = finger[i].node;
		}
		else
		{
			finger[i+1].node = findSuccessor(-1, 0, node0_successor, finger[i+1].interval_start, finger );

			// Ensure ith finger node is not in front of new joining node.
			//if( !( finger[i+1].node >= finger[i+1].interval_start && node_num < node_num ) )
			//{
			//	finger[i+1].node = node_num;
			//}
		}
	}
}

void update_finger_table( int node_number, int node_predecessor, int s, int i, struct finger_table_entry* finger )
{
	int fd;
	int tempFinger;
	char send_buf[64];

	// Check is s is updating finger table with s.
	// This is one of the bugs in the Chord paper.
	if( node_number == s )
		return;

	
	if(finger[i].node == 0){
		tempFinger = 256;
	}
	else{
		tempFinger = finger[i].node;
	}
	if((s >= node_number && s < tempFinger && s >= finger[i].interval_start ) || (finger[i].interval_start > node_number && finger[i].interval_start <= s && num_active == 2) )
	{
		
		finger[i].node = s;
		// Tell node_predecessor to update its finger table with (s, i) are parameters
		sprintf( send_buf, "setfingertable %i %i", s, i);
		if(node_predecessor != node_number){
			fd = sendMessage( node_predecessor, send_buf); //TODO
			close(fd);
		}
	}
	if(finger[i].node == 256)
		finger[i].node = 0;
}

void update_others( int node_num, int node_successor, struct finger_table_entry *finger )
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
	//printf("r4\n");
	if(recv(fd, buf1, 64, 0) == -1){
        perror("recv4444\n");
    }
	close(fd);
	sscanf(buf1, "%i", &node0_successor);

	for( i=1 ; i < FINGER_SIZE ; i++ )
	{

		id = (node_num - ((int) pow(2.0, (double)(i-1)))); //%256; //Handle negative ID?
		id = id + 1; // Fixes shadow bug in original algorithm.
		if(id < 0)
			id = 256 + id;
	//		continue;
			//id = 256 + id;
		
		p = findPredecessor(node_num, 0, node0_successor, id, finger);
		
		// Tell p to update it's finger table with parameters (n, i)
		//set_finger_table(p, n, i);
		//printf("update_others nod_num %i\n", node_num);
		sprintf( send_buf, "setfingertable %i %i", node_num, i);
		fd = sendMessage(p, send_buf); //maybe wait for confirmation message?
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
		
		update_others(node_num, finger[1].node, finger); //finger[1].node

		
		for( i = *node_predecessor+1; i <= node_num ; i++ )
			keys[i] = '1';

		//for(i = finger[1].node; i <= node_num; i++){

		//}
		
		sprintf( send_buf, "%s %i %i", "removekeys", (*node_predecessor)+1, node_num);
		//fd = sendMessage( *node_predecessor, send_buf );
		fd = sendMessage(finger[1].node, send_buf);
		close(fd);
	}
	else
	{
		for( i=1 ; i < FINGER_SIZE ; i++ ){
			finger[i].node = node_num;
			//finger[i].interval_start = 0;
	        finger[i].interval_start = (node_num +((int) pow(2.0, (double) (i-1)))) % 256;

		}
		//node_predecessor = (int*) malloc(sizeof(int));//node_num;
		*node_predecessor = node_num;
		
	}
	int retval = *node_predecessor;
	return retval;//*node_predecessor;
}

void leave(int node_num, int node_successor, int node_predecessor, struct finger_table_entry* finger)
{
	int i;
	int fd;
	int successor_predecessor;
	int pred_of;
	int remove_from;
	char buf[64];
	char send_buf[64];


	if( node_num = node_successor )
		return;

	fd = sendMessage(finger[1].node, "predecessor");
	if (recv( fd, buf, 64, 0 ) == -1)
	{
		perror("recv\n");
	} 
	close(fd);
	sscanf( buf, "%i", &successor_predecessor);

	sprintf( send_buf, "setpredecessor %i", node_predecessor );
	fd = sendMessage( successor_predecessor, send_buf);
	close(fd);

	for( i = 1 ; i <= FINGER_SIZE ; i++ )
	{
		buf[0] = '\0';
		send_buf[0] = '\0';
		pred_of = node_num - (int) pow( 2, (double)(1-i) ) + 1;
		fd = sendMessage( pred_of, "predecessor");
		if(recv( fd, buf, 64, 0) == -1)
		{
			perror("recv\n");
		}
		close(fd);
		sscanf( buf, "%i", &remove_from );
		sprintf( send_buf, "removenode %i %i %i", node_num, i, finger[1].node );
		sendMessage( remove_from, send_buf );
	}
	
	send_buf[0] = '\0';
	sprintf( send_buf, "%s %i %i", "addkeys", (node_predecessor)+1, node_num);
	fd = sendMessage(finger[1].node, send_buf);
	close(fd);
	
}

void remove_node( int node_num, int node_predecessor, int i, int replace, struct finger_table_entry* finger )
{
	if( finger[i].node == node_num )
	{
		char buf[64];
		finger[i].node = replace;
		sprintf( buf, "removenode %i %i %i", node_num, i, replace );
		sendMessage( node_predecessor, buf );
	}
}

void show(int node_num, char *keys){  //KC
    int i;
    if(file_write == 0)
        printf("%i ", node_num);
    else{
        file = fopen(file_name, "a");
        fprintf(file, "%i ", node_num);
    }
    for(i = 0; i < MAX_NUM_NODES; i++){
        if(keys[i] == '1'){
            if(file_write == 0)
                printf("%i ", i);
            else
                fprintf(file, "%i ", i);
        }
    }
    if(file_write == 0)
        printf("\n");
    else{
        fprintf(file, "\n");
        fclose(file);
    }
}



void showall(int node_num, int successor, char *keys){
    int fd;
    char buf[64];
    sprintf(buf, "showall %i", node_num);
    show(node_num, keys);
    if(successor != 0){
        fd = sendMessage(successor, buf);
        
        if(recv(fd, buf, 64, 0) == -1){
        //  perror("recv");
        }
    }
}



int find(int node_num, int node_successor, int key, char *keys, struct finger_table_entry *finger){
	//if(keys[key] == '1'){
	//	return node_num;
	//}
	//TODO find message, while(not found){ findsuccessor}
    return findSuccessor(node_num, node_num, node_successor, key, finger);
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
		//printf("server: got connection from %s\n", s);
		//printf("r5\n");	
		if(recv(new_fd, buf, 64, 0) == -1){	
			perror("recv5\n");
		}

		//printf("Node %i received: %s\n", thread_data->node_num, buf);

		if(strstr(buf, "join") != NULL){
			int pred;
			pred = join(thread_data->node_num, &(thread_data->node_predecessor), &(thread_data->node_successor), 
									thread_data->keys ,finger);
			thread_data->node_predecessor = pred;
			if(thread_data->node_num != 0)
				sendMessage(-1, "finished");
		}
		else if(strstr(buf, "leave") != NULL){
			leave( thread_data->node_num, finger[1].node, thread_data->node_predecessor, finger); //finger[1].node
		}
		else if( strstr(buf, "successor") ){
			sprintf( send_buf, "%i", finger[1].node); //use finger[1] instead//thread_data->node_successor );
			//printf("send buf %s, successor %i\n", send_buf, finger[1].node);
			if(send(new_fd, send_buf, 64, 0) == -1){
				perror("send");
			}
		}
       /* else if( strstr(buf, "predecessor") ){
            printf("PREDDESSSSSSSSSSSSSSSSSSSSS\n");
            sprintf( send_buf, "%i", thread_data->node_predecessor );
            if(send(new_fd, send_buf, 64, 0) == -1){
                perror("send");
            }
        }*/
		else if ( strstr(buf, "setpredecessor") != NULL){
			sscanf( buf, "%s %i", cmd, &i);
			thread_data->node_predecessor = i;
		}
        else if( strstr(buf, "predecessor") ){
			sprintf( send_buf, "%i", thread_data->node_predecessor );
            if(send(new_fd, send_buf, 64, 0) == -1){
                perror("send");
            }
        }
        else if( strstr(buf, "removenode") != NULL ){
        	sscanf( buf, "%i %i %i", &i, &j, &k );
        	remove_node( i, thread_data->node_predecessor, j, k, finger );
        }	
		else if ( strstr(buf, "setfingertable") ){
			sscanf( buf, "%s %i %i", cmd, &k, &i );
			//printf("set finger k%i\n", k);
			update_finger_table( thread_data->node_num, thread_data->node_predecessor, k, i, finger );
		}
		else if (strstr(buf, "removekeys") ){
			sscanf( buf, "%s %i %i", cmd, &k, &i );
			for( j=k ; j<=i ; j++ )
				thread_data->keys[j] = '0';

			//printf("removed %i k%i i%i \n", thread_data->node_num, k, i);
		}
		else if( strstr(buf, "addkeys") ){
			sscanf( buf, "%s %i %i", cmd, &k, &i );
			for( j=k ; j<=i ; j++ )
					thread_data->keys[j] = '1';
		}
		else if(strstr(buf, "find") != NULL){ //KC
            		sscanf(buf, "%s %i", cmd, &k);
            		int find_result = find(thread_data->node_num, finger[1].node, k, thread_data->keys, finger);
            		//printf("Location: %i\n", find_result);
            		//Notify coordinator
					sendMessage(-1, "finished");
    	}
       	else if(strstr(buf, "closestfinger") != NULL){ //KC
           		int id;
           		sscanf(buf, "%s %i", cmd, &id);
           		int finger_result = closestPrecedingFinger(thread_data->node_num, id, finger);
           		char send_buf[64];
           		sprintf(send_buf, "%i",finger_result);
           		if(send(new_fd, send_buf, 64, 0) == -1){
               		perror("send");
    	       	}
        }
        else if(strstr(buf, "showall") != NULL){
            showall(thread_data->node_num, finger[1].node, thread_data->keys);
            char send_buf[64];
            sprintf(send_buf, "%s", "temp");
            //if(send(new_fd, send_buf, 64, 0) == -1)
            if(thread_data->node_num == 0){
                sendMessage(-1, "finished");
            }else if(send(new_fd, send_buf, 64, 0) == -1){
                perror("send");
            }
        }	
        else if(strstr(buf, "show") != NULL){
            show(thread_data->node_num, thread_data->keys);
			sendMessage(-1, "finished");
        }
		else if(strstr(buf, "finger") != NULL){
			int i;
			for(i = 1; i < FINGER_SIZE; i++){
				printf("finger%i: %i, start %i\n", i, finger[i].node, finger[i].interval_start);
			}
			sendMessage(-1, "finished");
		}
		else{
			printf("bad message\n");
		}


		close(new_fd);
		//printf("finished\n");
		//sendMessage(-1, "finished"); //Notify coordinator that command is finished
	}
}

int joinStart(int node_num){
	int i;
	pthread_t node_thread;
	struct node_thread_data *thread_data = (struct node_thread_data*) malloc(sizeof(struct node_thread_data));
	thread_data->node_num = node_num;
	if(node_num == 0){
		for(i = 0; i < MAX_NUM_NODES; i++){
			thread_data->keys[i] = '1';
		}
	}
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
		//printf("server: got connection from %s\n", s);
		//printf("r6\n");
		if(recv(new_fd, buf, 64, 0) == -1){
            perror("recv6\n");
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

	srand(time(NULL));
	pthread_mutex_init(&command_lock, NULL);
	pthread_mutex_init(&phase_count_lock, NULL);

	phase1_count = 0;
	phase2_count = 0;
	command_continue = 0;

   	if(argc == 3){      //KC
       	/*printf("open\n");
		sscanf(argv[2], "%s", file_name);
       	/file = fopen(argv[2], "w");
       	if(file == NULL){
           		exit(1);
       	}*/
       	file_write = 0;
		num_joins = atoi(argv[1]);
		num_finds = atoi(argv[2]);
   	}
   	else{
       	exit(1);
   	}


	active_nodes[0] = '1';
	//thread_data->keys[0] = '1';
	for(i = 1; i < MAX_NUM_NODES; i++){
		active_nodes[i] = '0';
	//	thread_data->keys[i] = '1';
	}
	pthread_create(&coordinator_server_thread, NULL, coordinatorServerThread, NULL);	
	//pthread_create(&node_thread, NULL, nodeThread, (void*) thread_data);
	joinStart(0);
	sleep(2);
	sendMessage(0, "join");
	num_active = 1;
	//int sockfd = initializeServer(-1);
	while(1){
		char user_input[CMD_LENGTH];
		char cmd[8];
		char buf[64];
		int node, key, fd;
	
        if(num_joins > 0){          //exp
            node = rand()%256;
            while(active_nodes[node] != '0')
                node = rand()%256;
            sprintf(user_input, "%s %i", "join", node);
            num_joins--;
        }
        else if(num_finds > 0){
            node - rand()%256;
            while(active_nodes[node] != '1')
                node = rand()%256;
            key = rand()%256;
            sprintf(user_input, "%s %i %i", "find", node, key);
            num_finds--;
        }
        else{
            break;
        }


		if(strstr(user_input, "join") != NULL){
			sscanf(user_input, "%s %i", cmd, &node);
			if(active_nodes[node] == '1'){
				printf("Node %i already joined\n", node);
				continue;
			}
			else{
				//fd = sendMessage(0, "hello");
				active_nodes[node] = '1';
				num_active++;
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
				num_active--;
				sendMessage(node, "leave");
			}
		}
		else if(strstr(user_input, "show") != NULL && strstr(user_input, "all") != NULL){
			sendMessage(0, "showall");
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
		else if(strstr(user_input, "finger") != NULL){
			sscanf(user_input, "%s %i", cmd, &node);
			sprintf(buf, "%s %i", "finger", node);
			sendMessage(node, buf);
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
		command_continue = 0;
		pthread_mutex_unlock(&command_lock);
		
		//TODO add close fd
	}
	printf("phase1 count: %i\n", phase1_count);
	printf("phase2 count: %i\n", phase2_count);
	return 0;
}
