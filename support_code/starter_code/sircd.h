typedef struct s_client{
    int clientfd;
	char nickname[16];
	char username[16];
	char hostname[16];
    char realname[16];
    char servername[16];
	int channel_id;
	int is_nick_set;
	int is_user_set;
} client_t;

typedef struct s_channel{
    int channel_id;
    char channel_name[16];
    int connected_clients[FD_SETSIZE];
	int is_on;
	int client_count;
} channel_t;


typedef struct s_pool {
	int maxfd; 		// largest descriptor in sets
	fd_set read_set; 	// all active read descriptors
	fd_set write_set; 	// all active write descriptors
	fd_set ready_set;	// descriptors ready for reading
    int maxi;         // largest index
	int nready;		// return of select()， number of ready fd
    
    client_t clientfd[FD_SETSIZE]; // set of client fd
	rio_t clientrio[FD_SETSIZE]; // set of read buffers
} pool;
