/* To compile: gcc sircd.c rtlib.c rtgrading.c csapp.c -lpthread -osircd */

#include "rtlib.h"
#include "rtgrading.h"
#include "csapp.h"
#include "sircd.h"
#include <stdlib.h>

/* Macros */
#define MAX_MSG_TOKENS 100
#define MAX_MSG_LEN 8192
#define SERVER_PORT 20102
#define MAX_CLIENT_SIZE FD_SETSIZE
#define MAX_CHANNEL_SIZE 10

/* Global variables */
u_long curr_nodeID;
rt_config_file_t   curr_node_config_file;  /* The config_file  for this node */
rt_config_entry_t *curr_node_config_entry; /* The config_entry for this node */
client_t *client_list[MAX_CLIENT_SIZE]; /* The client list */
channel_t *channel_list[MAX_CHANNEL_SIZE]; /* The channel list */
int client_count; /* The client count */
int channel_count = 0; /* The client count */

/* Function prototypes */
void init_node( int argc, char *argv[]);
size_t get_msg( char *buf, char *msg );
int tokenize( char const *in_buf, char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1] );
void add_client(int connfd, pool * p);
void check_clients(pool * p);
void init_pool(int listenfd, pool * p);
void exe_command_join(int connfd,char * cn);
int is_command(char * first);
int exe_command(int connfd, char * msg);
void exe_command_nick(int connfd,char * nickname);
void exe_command_user(int connfd,char * un,char * hn,char * sn,char * rn);
void exe_command_join(int connfd,char * cn);
void exe_command_quit(int connfd);
void exe_command_part(int connfd,char * cn);
void exe_command_list(int connfd);
void exe_command_privmsg_single(int connfd,char *to,char *m);
void exe_command_privmsg_multi(int connfd,char *to,char *m);
void exe_command_who(int connfd,char * cn);
int check_nickname(char * nn);
int check_channel(char * cn);
void MOTD(client_t *client);
client_t *get_client_by_connfd(int connfd);
client_t *get_client_by_name(char *name);


/* Main */
int main( int argc, char *argv[] )
{
    int listenfd, connfd, clientlen = sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    static pool pool;

    init_node(argc, argv);
    printf("I am node %lu and I listen on port %d for new users\n", curr_nodeID, curr_node_config_entry->irc_port);
    
    /* Open listenfd with the port */
    listenfd = Open_listenfd(SERVER_PORT);
    /* Init pool with listenfd */
    init_pool(listenfd, &pool);
    
    while (1) {
        /* Wait for listening/connected descriptor(s) to become ready */
        pool.ready_set = pool.read_set;
        pool.nready = Select(pool.maxfd+1, &pool.ready_set, NULL, NULL, NULL);
        
        /* Add new client to pool */
        if (FD_ISSET(listenfd, &pool.ready_set)) {
            connfd = Accept(listenfd, (struct sockaddr *)&clientaddr, (socklen_t *)&clientlen);
            add_client(connfd, &pool);
        }
        
        /* Echo a text line from each ready connected descriptor */
        check_clients(&pool);
    }
}
/*
 * void init_pool(int sockfd, pool * p)
 *
 * init pool with sockfd
 */
void init_pool(int listenfd, pool * p)
{
    int i;
    p->maxi = -1;
    for (i=0; i< FD_SETSIZE; i++)
        p->clientfd[i].clientfd = -1;
    
    /* Initially, listenfd is only member of select read set */
    p->maxfd = listenfd;
    FD_ZERO(&p->read_set);
    FD_SET(listenfd, &p->read_set);}

/*
 * void add_client(int isock, struct sockaddr * addr, pool * pool)
 *
 * add new client to the pool
 */
void add_client(int connfd, pool * p)
{
    int i;
    p->nready--;
    for (i = 0; i < FD_SETSIZE; i++) { /* Find an empty element */
        if (p->clientfd[i].clientfd < 0) {
            /* Add connected descriptor to the pool */
            p->clientfd[i].clientfd = connfd;
            Rio_readinitb(&p->clientrio[i], connfd);
            
            /* Add the descriptor to descriptor set */
            FD_SET(connfd, &p->read_set);
            
            /* Update max descriptor and pool highwater mark */
            if (connfd > p->maxfd)
                p->maxfd = connfd;
            if (i > p->maxi)
                p->maxi = i;
            break;
        }
    }
    /* malloc a new client to store the local message */
    client_t *client = Malloc(sizeof(client_t));
	*(client->nickname) = NULL;
	*(client->username) = NULL;
	client->clientfd = connfd;
	client->channel_id = -1;
	client->is_nick_set = 0;
	client->is_user_set = 0;
    
    client_list[connfd] = client;
    client_count++;
    printf("add a new client successfully\n");
    /* Couldn't find an empty slot */
    if (i == FD_SETSIZE) 
        unix_error("add_client error: Too many clients\n");
}

/*
 * void check_clients(pool * p)
 *
 * check the buffer of all the clients in the pool
 */
void check_clients(pool * p)
{
    int i, connfd, n;
    char buf[MAX_MSG_LEN];
    char msg[MAX_MSG_LEN];
    char temp[1024]; /* send back buffer */
    char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1];
    rio_t rio;
    
    for (i = 0; (i <= p->maxi) && (p->nready > 0); i++) {
        connfd = p->clientfd[i].clientfd;
        rio = p->clientrio[i];
        
        /* Read from the ready client */
        if ((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))) {
           
            p->nready--;
            n = Rio_readlineb(&rio, buf, MAX_MSG_LEN);
            if (n != 0) {
                get_msg(buf,msg);
                printf("%s\n",msg);
                tokenize(msg,tokens);
                if (is_command((char *)&tokens[0]) > 0) {
                    if (exe_command(connfd,(char *)msg) < 0) {
                        sprintf(temp, "%s execute failed. Please Try again\n", tokens[0]);
                        Write(connfd, temp, strlen(temp));
                    }
                } else {
                    /* invalid command, write back */
					sprintf(temp, "ERR UNKNOWN COMMAND\n");
					Write(connfd, temp, strlen(temp));
                }
            }
            
            /* EOF detected, remove descriptor from pool */
            else {
                printf("EOF\n");
                Close(connfd);
                FD_CLR(connfd, &p->read_set);
                p->clientfd[i].clientfd = -1;
            }
        }
    }
}
/*
 *  int is_commend(char * first )
 *
 *  check the validation of the first commend 
 *
 */
int is_command(char *first)
{
    if ((strcmp(first, "NICK")) == 0) {
        return 1;
    }
    else if (strcmp(first, "USER") == 0) {
        return 1;
    }
    else if (strcmp(first, "QUIT") == 0) {
        return 1;
    }
    else if (strcmp(first, "JOIN") == 0) {
        return 1;
    }
    else if (strcmp(first, "PART") == 0) {
        return 1;
    }
    else if (strcmp(first, "LIST") == 0) {
        return 1;
    }
    else if (strcmp(first, "WHO") == 0) {
        return 1;
    }
    else if (strcmp(first, "PRIVMSG") == 0) {
        return 1;
    }
    else {
        return -1;
    }
}
/*
 *
 *
 *
 */
int exe_command(int connfd, char * msg)
{
    char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1];
    
    tokenize(msg,tokens);
    
    char * temp = tokens[0];
    char tempback[1024];
    if ((strcmp(temp, "NICK")) == 0) {
        if(strlen(tokens[1]) == 0){
            sprintf(tempback, "Command:NICK <nickname>\n");
            Write(connfd, tempback, strlen(tempback));
        }else{
           exe_command_nick(connfd,tokens[1]);
            printf("nick success\n");
        }
        /* clean tokens */
        memset(tokens[0],'\0',sizeof(tokens[0]));
        memset(tokens[1],'\0',sizeof(tokens[0]));
        return 1;
    }
    else if (strcmp(temp, "USER") == 0) {
        if(strlen(tokens[1]) == 0||strlen(tokens[2]) == 0|| strlen(tokens[3]) == 0|| strlen(tokens[4]) == 0){
            sprintf(tempback, "Command:USER <username> <hostname> <servername> <realname>\n");
            Write(connfd, tempback, strlen(tempback));
        }else{
            exe_command_user(connfd,tokens[1],tokens[2],tokens[3],tokens[4]);
        }
        /* clean tokens */
        memset(tokens[0],'\0',sizeof(tokens[0]));
        memset(tokens[1],'\0',sizeof(tokens[1]));
        memset(tokens[2],'\0',sizeof(tokens[2]));
        memset(tokens[3],'\0',sizeof(tokens[3]));
        memset(tokens[4],'\0',sizeof(tokens[4]));
        printf("tokens[3] = %s\n",tokens[3]);
        return 1;
    }
    else if (strcmp(temp, "QUIT") == 0) {
        exe_command_quit(connfd);
        /* clean tokens */
        memset(tokens[0],'\0',sizeof(tokens[0]));
        return 1;
    }
    else if (strcmp(temp, "JOIN") == 0) {
        if(strlen(tokens[1]) == 0){
            sprintf(tempback, "Command:JOIN <channel_id>\n");
            Write(connfd, tempback, strlen(tempback));
        }else{
            int k;
            for (k = 1; k < MAX_MSG_TOKENS; k++) {
                if ( strlen(tokens[k]) != 0) {
                    exe_command_join(connfd, tokens[k]);
                } else {
                    break;
                }
            }
        }
        /* clean tokens */
        memset(tokens[0],'\0',sizeof(tokens[0]));
        memset(tokens[1],'\0',sizeof(tokens[0]));
        return 1;
    }
    else if (strcmp(temp, "PART") == 0) {
        if(strlen(tokens[1]) == 0){
            sprintf(tempback, "Command:PART <channel_id>\n");
            Write(connfd, tempback, strlen(tempback));
        }else{
            int k;
            for (k = 1; k < MAX_MSG_TOKENS; k++) {
                if ( strlen(tokens[k]) != 0) {
                    printf("------\n");
                    exe_command_part(connfd, tokens[k]);
                } else {
                    break;
                }
            }
        }
        /* clean tokens */
        memset(tokens[0],'\0',sizeof(tokens[0]));
        memset(tokens[1],'\0',sizeof(tokens[0]));
        return 1;
        return 1;
    }
    else if (strcmp(temp, "LIST") == 0) {
        exe_command_list(connfd);
        /* clean tokens */
        memset(tokens[0],'\0',sizeof(tokens[0]));
        return 1;
    }
    else if (strcmp(temp, "PRIVMSG") == 0) {
        char m[1024];
        printf("Begin PRIVMSG>>>>>>>>>\n");
        int k;
        if((strlen(tokens[1]) == 0) || (strlen(tokens[2]) == 0)){
            sprintf(tempback, "Command:PRIVMSG <channel_name>/<nick_name> <message>\n");
            Write(connfd, tempback, strlen(tempback));
        }else{
            strcat(m,tokens[2]);
            for (k=3; k<1024; k++) {
                if (strlen(tokens[k]) != 0) {
                    strcat(m," ");
                    strcat(m,tokens[k]);
                }else{
                    break;
                }
            }
            exe_command_privmsg_multi(connfd,tokens[1],m);
            memset(m,'\0',sizeof(m));
        }
        //clean tokens
        int q;
        for (q = 0; q < k; q++) {
            memset(tokens[q],'\0',sizeof(tokens[q]));
        }
        return 1;
    }
    else if (strcmp(temp, "WHO") == 0) {
        if(strlen(tokens[1]) == 0){
            sprintf(tempback, "Command:WHO <channelname>\n");
            Write(connfd, tempback, strlen(tempback));
        }else{
            exe_command_who(connfd,tokens[1]);
        }
        /* clean tokens */
        memset(tokens[0],'\0',sizeof(tokens[0]));
        memset(tokens[1],'\0',sizeof(tokens[0]));
        return 1;
    }
    return -1;
}
/*
 * void exe_command_nick(int connfd,char * nickname)
 *
 * set the nickname of the client
 */
void exe_command_nick(int connfd,char * nn)
{
    char tempback[1024];
    client_t * client = get_client_by_connfd(connfd);
    
    
    if (check_nickname(nn) == 0) {
        client->is_nick_set = 1;
        strcpy(client->nickname, nn);
        // IF the client has been registed, call MOTD
        if ((client->is_nick_set == 1)&&(client->is_user_set==1)) {
            //sprintf(tempback, "Change nickname from %s to %s\n", client->nickname,nn);
            //Write(connfd, tempback, strlen(tempback));
            MOTD(client);
        }
            
    }else{
        sprintf(tempback, "nickname:%s has been used\n", nn);
        Write(connfd, tempback, strlen(tempback));
    }
}
/*
 * void exe_command_user(int connfd,char * username,char * hostname,char * realname)
 *
 * set the nickname of the client
 */
void exe_command_user(int connfd,char * un,char * hn,char * sv,char * rn)
{
    char tempback[1024];
    client_t * client = get_client_by_connfd(connfd);
    
    if (client->is_user_set == 1) {
        sprintf(tempback, "Username has been set\n");
        Write(connfd, tempback, strlen(tempback));
    }else{
        strcpy(client->username, un);
        strcpy(client->hostname, hn);
        strcpy(client->servername,sv);
        strcpy(client->realname, rn);
        client->is_user_set = 1;
        // IF the client has been registed, call MOTD
        if ((client->is_nick_set == 1)&&(client->is_user_set==1)) {
            MOTD(client);
        }
        //sprintf(tempback, "Set username:%s hostname:%s realname:%s\n",un,hn,rn);
        //Write(connfd, tempback, strlen(tempback));
    }
}
/*
 * void exe_command_quit(int connfd)
 *
 * Delete the session of client connfd
 */
void exe_command_quit(int connfd)
{
    client_t * client = get_client_by_connfd(connfd);
    printf("QUIT\n");
    Close(connfd);
    client -> clientfd = -1;
}
/*
 * void exe_command_join(int connfd,char * cn)
 *
 * Join a specific channel
 */
void exe_command_join(int connfd,char * cn)
{
    int id;
    channel_t * channel;
    client_t * client = get_client_by_connfd(connfd);
    if ((id = check_channel(cn)) == -1) {
        //create a new channel
        channel = Malloc(sizeof(channel_t));
        strcpy(channel->channel_name,cn);
        channel->is_on = 1;
        channel->client_count = 1;
        int j;
        // initialize the channel
        for (j = 0; j<FD_SETSIZE; j++) {
            channel->connected_clients[j] = 0;
        }
        channel->connected_clients[connfd] = connfd;
        int k;
        // allocate this new channel to the channel list
        for (k=0; k<MAX_CHANNEL_SIZE; k++) {
            if(channel_list[k] == NULL){
                channel_list[k] = channel;
                channel->channel_id = k;
                break;
            }
        }
        channel_count++;
        
    }else{
        // Add the client to a existed channel
        channel = channel_list[id];
        channel->client_count++;
        channel->connected_clients[connfd] = connfd;
    }
    char tempback[1024];
	sprintf(tempback,":%s JOIN %s\n",client->nickname,cn);
	
	char hostname[1024];
	gethostname(hostname, sizeof(hostname));
    
	sprintf(tempback,"%s:%s 353 %s = %s : %s\n", tempback, hostname, client->nickname, cn, client->nickname);
	sprintf(tempback,"%s:%s 366 %s %s :End of /NAMES list\n", tempback, hostname, client->nickname, cn);
    int k;
	for(k=0; k<FD_SETSIZE; k++){
		if(channel->connected_clients[k] != 0){
			Write(channel->connected_clients[k], tempback, strlen(tempback));
		}
	}
}
/*
 * void exe_command_part(int connfd,char * cn)
 *
 * leave the specific channel
 */
void exe_command_part(int connfd,char * cn)
{
    int id;
    char tempback[1024];
    channel_t * channel;
    client_t * client = get_client_by_connfd(connfd);
    if ((id = check_channel(cn)) != -1) {
        if (check_client_in_channel(connfd,id) == 1) {
            channel = channel_list[id];
            channel->client_count--;
            channel->connected_clients[connfd] = 0;
            if(channel->client_count == 0){
                //there is no clients in this channel,so delete it
                channel_list[id] = NULL;
            }else{
                //broadcast the quit message to all the other clients in this channel
                sprintf(tempback,":%s!++++++++@%s QUIT :\n",client->nickname,channel->channel_name);
                Write(connfd, tempback, strlen(tempback));
                int i;
                for(i=0; i<FD_SETSIZE; i++){
                    if(channel->connected_clients[i] != 0){
                        Write(channel->connected_clients[i], tempback, strlen(tempback));
                    }
                }
            }
        }else{
            sprintf(tempback,"You are not in Channel %s.\n",cn);
            Write(connfd,tempback,strlen(tempback));
        }
    }else{
        sprintf(tempback,"Channel %s does not exist.\n",cn);
        Write(connfd,tempback,strlen(tempback));
    }
    
}
/*
 * void exe_command_who(int connfd,char * cn)
 *
 * List all the client in a channel
 */
void exe_command_who(int connfd,char * cn)
{
    int id = check_channel(cn);
    char tempback[1024];
    char hostname[1024];
	gethostname(hostname, sizeof(hostname));
    
    if (id != -1) {
        client_t * client = get_client_by_connfd(connfd);
        channel_t * channel = channel_list[id];
        sprintf(tempback,":%s 352 %s %s please look out: ",hostname,client->nickname, channel->channel_name);
		int i;
		client_t *temp = NULL;
		for(i=0; i<FD_SETSIZE; i++){
			if(channel->connected_clients[i] != 0){
				temp = get_client_by_connfd(i);
				sprintf(tempback,"%s %s",tempback,temp->nickname);
			}
		}
		sprintf(tempback,"%s H :0 The MOTD\n",tempback);
		sprintf(tempback,"%s:%s 315 %s %s :End of /WHO list\n", tempback, hostname, client->nickname, channel->channel_name);
		Write(connfd, tempback, strlen(tempback));
    
    }else{
        sprintf(tempback,"Channel %s does not exist.\n",cn);
        Write(connfd,tempback,strlen(tempback));
    }
}
/*
 * void exe_command_list(int connfd)
 *
 * List all the channel connected
 */
void exe_command_list(int connfd)
{
    char hostname[1024];
	gethostname(hostname,sizeof(hostname));
	client_t *client = get_client_by_connfd(connfd);
	char tempback[1024];
	sprintf(tempback,":%s 321 %s Channel :Users Name\n",hostname, client->nickname);
	
	int i;
	for(i=0; i<MAX_CHANNEL_SIZE; i++){
		if(channel_list[i] != NULL){
			sprintf(tempback,"%s:%s 322 %s %s :%d\n", tempback, hostname, client->nickname, channel_list[i]->channel_name, channel_list[i]->client_count);
            
		}
	}
	sprintf(tempback,"%s:%s 323 %s :End of /LIST\n",tempback,hostname,client->nickname);
	Write(connfd, tempback, strlen(tempback));
}
/*
 *
 *
 *
 */
void exe_command_privmsg_multi(int connfd,char * target, char * m)
{
    char *token;
    for(token = strtok(target, ","); token != NULL; token = strtok(NULL, ",")) {
        exe_command_privmsg_single(connfd,token,m);
    }
}
/*
 * void exe_command_privmsg(int connfd,char *target,char *m)
 *
 * Send a message to a specific client or channel
 */
void exe_command_privmsg_single(int connfd, char * target, char * m)
{
    int id;
    char tempback[1024];
    if (check_nickname(target) == 1) {
        // send message to a client
        printf("send message to a client\n");
        client_t * target_client = get_client_by_name(target);
        client_t * current_client = get_client_by_connfd(connfd);
        sprintf(tempback,":%s PRIVMSG %s :%s\n", current_client->nickname, target_client->nickname, m);
        Write(target_client->clientfd,tempback,strlen(tempback));
    }
    else if((id = check_channel(target)) != -1){
        // send message to a channel
        printf("send message to a channel\n");
        int i;
        channel_t *target_channel = channel_list[id];
        for(i=0; i<FD_SETSIZE; i++){
            //boardcast the message to all the clients in the channel
            if((target_channel->connected_clients[i] != 0) && (target_channel->connected_clients[i] != connfd)){
                sprintf(tempback,":%s PRIVMSG %s :%s\n",client_list[connfd]->nickname,target_channel->channel_name, m);
                Write(target_channel->connected_clients[i], tempback, strlen(tempback));
            }
        }
        
    }else{
        // no coressponding clients or channels
        sprintf(tempback,"No coressponding clients or channels\n");
        Write(connfd,tempback,strlen(tempback));
    }
}
/*
 * client_t *get_client_by_connfd(int connfd)
 *
 * Get client in the client list by connfd
 */
client_t *get_client_by_connfd(int connfd)
{
    return client_list[connfd];
}
/*
 * client_t *get_client_by_name(char * name)
 *
 * Get client in the client list by connfd
 */
client_t *get_client_by_name(char *name)
{
    int i;
    for (i = 0; i < MAX_CLIENT_SIZE; i++){
		if(client_list[i] != NULL){
			if(strcmp(client_list[i]->nickname, name) == 0){
                return client_list[i];
            }
		}
	}
    return NULL;
}
/*
 * int check_nickname(char * nn)
 *
 * check whether the nickname is existed
 */
int check_nickname(char * nn)
{
    int i;
	for (i = 0; i < MAX_CLIENT_SIZE; i++){
		if(client_list[i] != NULL){
			if(strcmp(client_list[i]->nickname, nn) == 0){
                return 1;
            }
		}
	}
  	return 0;
}
/*
 * int check_channel(char * cn)
 *
 * check whether the channel existed
 */
int check_channel(char * cn)
{
    int i;
	for (i = 0; i < MAX_CHANNEL_SIZE; i++){
		if(channel_list[i] != NULL){
			if(strcmp(channel_list[i]->channel_name, cn) == 0){
                return channel_list[i]->channel_id;
            }
		}
	}
  	return -1;
}
/*
 *
 *
 *
 */
int check_client_in_channel(int connfd,int channel_id)
{
    channel_t *channel = channel_list[channel_id];
    int i;
    for (i=0; i<FD_SETSIZE; i++) {
        if ((channel->connected_clients[i]) == connfd) {
            return 1;
        }
    }
    return -1;
}
/*
 * void MOTD(client_t *client)
 *
 * Return the MOTD when a client has been registed.
 */
void MOTD(client_t *client)
{
    char hostname[1024];
    char tempback[1024];
    
    gethostname(hostname, sizeof(hostname));
    sprintf(tempback,":%s 375 %s :- <server> Message of the day -.\n", hostname, client->nickname);
	sprintf(tempback,"%s:%s 372 %s :- <text> \n",tempback, hostname, client->nickname);
	sprintf(tempback,"%s:%s 376 %s :End of /MOTD command.\n", tempback, hostname, client->nickname);
	Write(client->clientfd, tempback, strlen(tempback));
    
}
/*
 * void init_node( int argc, char *argv[] )
 *
 * Takes care of initializing a node for an IRC server
 * from the given command line arguments
 */
void init_node( int argc, char *argv[] )
{
    int i;

    /* Check the parament length */
    if( argc < 3 )
    {
        printf( "%s <nodeID> <config file>\n", argv[0] );
        exit( 0 );
    }

    /* Parse nodeID */
    curr_nodeID = atol( argv[1] );

    /* Store  */
    rt_parse_config_file(argv[0], &curr_node_config_file, argv[2] );

    /* Get config file for this node */
    for( i = 0; i < curr_node_config_file.size; ++i )
        if( curr_node_config_file.entries[i].nodeID == curr_nodeID )
             curr_node_config_entry = &curr_node_config_file.entries[i];

    /* Check to see if nodeID is valid */
    if( !curr_node_config_entry )
    {
        printf( "Invalid NodeID\n" );
        exit(1);
    }
}


/*
 * size_t get_msg( char *buf, char *msg )
 *
 * char *buf : the buffer containing the text to be parsed
 * char *msg : a user malloc'ed buffer to which get_msg will copy the message
 *
 * Copies all the characters from buf[0] up to and including the first instance
 * of the IRC endline characters "\r\n" into msg.  msg should be at least as
 * large as buf to prevent overflow.
 *
 * Returns the size of the message copied to msg.
 */
size_t get_msg(char *buf, char *msg)
{
    char *end;
    int  len;

    /* Find end of message */
    end = strstr(buf, "\r\n");

    if( end )
    {
        len = end - buf + 2;
    }
    else
    {
        /* Could not find \r\n, try searching only for \n */
        end = strstr(buf, "\n");
	if( end )
	    len = end - buf + 1;
	else
	    return -1;
    }

    /* found a complete message */
    memcpy(msg, buf, len);
    msg[end-buf] = '\0';
    return len;	
}

/*
 * int tokenize( char const *in_buf, char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1] )
 *
 * A strtok() variant.  If in_buf is a space-separated list of words,
 * then on return tokens[X] will contain the Xth word in in_buf.
 *
 * Note: You might want to look at the first word in tokens to
 * determine what action to take next.
 *
 * Returns the number of tokens parsed.
 */
int tokenize( char const *in_buf, char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1] )
{
    int i = 0;
    const char *current = in_buf;
    int  done = 0;

    /* Possible Bug: handling of too many args */
    while (!done && (i<MAX_MSG_TOKENS)) {
        char *next = strchr(current, ' ');

	if (next) {
	    memcpy(tokens[i], current, next-current);
	    tokens[i][next-current] = '\0';
	    current = next + 1;   /* move over the space */
	    ++i;

	    /* trailing token */
	    if (*current == ':') {
	        ++current;
		strcpy(tokens[i], current);
		++i;
		done = 1;
	    }
	} else {
	    strcpy(tokens[i], current);
	    ++i;
	    done = 1;
	}
    }

    return i;
}