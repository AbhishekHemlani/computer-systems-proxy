/* @author Abhishek Hemlani ahemlani@andrew.cmu.edu
 * This file acts as a web proxy and takes in a HTTP URL and parses it and
 sends a request to a server. It then forwards the response
 to the client 

 proxy final: this proxy implements concurrent thread processes
 and creates a cache to store responses to client HtTp requests
 * .
 */

/* Some useful includes to help you get started */

#include "csapp.h"
#include "http_parser.h"
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)
#define HOSTLEN 256
#define SERVLEN 8


typedef struct sockaddr SA;


typedef struct { //struct definition copied from tiny.c
    struct sockaddr_in addr;    // Socket address
    socklen_t addrlen;          // Socket address length
    int connfd;                 // Client connection file descriptor
    char host[HOSTLEN];         // Client host
    char serv[SERVLEN];         // Client service (port)
} client_info;

//this cache_elem struct acts as a node in the cache's linked list
//stores key, value, size of message, and pointer to next element
typedef struct cache_elem{ 
    char* uri;
    char* msg;
    size_t obj_size;
    struct cache_elem* next_elem;

} cache_elem;
//this is the list struct for the cache
//it stores the head and tail pointers, and size of the cache
typedef struct{
    cache_elem* head;
    cache_elem* tail;
    size_t size;
} cache_list; 

cache_list* start = NULL; //global pointer to the cache
pthread_mutex_t lock;

// cache functions 


/**
 * @brief this function deletes the tail of the cache, 
 * and if the head is NULL it returns
 * this function has no parameters but refers to the global 
 * cache pointer
*/
void evict(){
    if(start->head == NULL){
        return;
    }
    cache_elem* temp = start->head;
    while(temp->next_elem != start->tail){ //iterates through the linked list until the tail 
        temp = temp->next_elem;
    }

    //deletes the tail from the list
    start->size = start->size - start->tail->obj_size; 
    free(start->tail);
    temp->next_elem = NULL;
    start->tail = temp;
    

}

/***
 * @brief this function inserts a new node/cache_block into the beginning
 * of the cache linked list
 * @param in this function takes the url as a key, the msg as the value, and the message size as obj_size
 * @return this function doesnt return 
*/
void cache_insert( const char* uri, char* msg, size_t obj_size){
    if( obj_size > MAX_OBJECT_SIZE){ //if the message size is too big, it returns 
        return;
    }


    while( start->size + obj_size > MAX_CACHE_SIZE){ //if the cache_size + msg_size is too big, it keeps evicting 
        evict();
    }
        //allocates new block 
        cache_elem* new_block = malloc(sizeof(cache_elem));
        new_block->uri = (char*) malloc(strlen(uri)+1);
        memcpy(new_block->uri, uri, strlen(uri)+1); //copy the message to the block struct
        new_block->msg = (char*) malloc(obj_size); //copy the message to the block struct 
        memcpy(new_block->msg, msg, obj_size);
        new_block->obj_size =obj_size;

        //insert the new block at the head, and sets the head to the new block
        new_block->next_elem = start->head;
        start->head = new_block;
        if(start->tail == NULL){ //if cache is empty, set the tail to the new_block
            start->tail = new_block;
        }
        start->size += obj_size;

}
/**
 * @brief this function searches the cache for a cache_block that matches the url given 
 * @param in this function takes in a url(key) to search through the cache
 * @return if the url exists, it returns the node it belongs to, otherwise NULL 
*/
cache_elem* cache_lookup( const char* uri){
    if(start->size == 0){ //if cache empty = return NULL 
        return NULL;
    }
    cache_elem* prev = NULL;//stores the prev cache_block
    cache_elem* temp = start->head;//stores current 
    while(temp != NULL){
        //if block found
        if(strcmp(temp->uri, uri) ==0 ){
            //if block is at head do nothing
            if(temp == start->head){
                return temp; 
            }
            //if block not at head, remove it 
            prev->next_elem = temp->next_elem;
            //insert the node in the head
            temp->next_elem = start->head;
            start->head = temp;
            if(start->tail == temp){
                start->tail = prev;
            }
            return temp;
        }
        prev = temp;
        temp = temp->next_elem;
    }
    //block not found 
    return NULL;
}

/**
 * @brief this is a print cache function that prints
 *  out the url of the nodes in the linked list
*/
void print_cache(){
    cache_elem* temp = start->head;
    while(temp != NULL){
        sio_printf("\nelem is %s\n", temp->uri);
        temp = temp->next_elem;
    }
    sio_printf("\n***********\n");

}
/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20230411 Firefox/63.0.1";

/**
 * @brief this function prints out an error message to the client
 * copied from tiny.c
 * @param in this takes in a file descriptor, error num, and a short msg for the client
 * @return an error message to the client
*/
void clienterror(int fd, const char *errnum, const char *shortmsg,
                 const char *longmsg) {
    char buf[MAXLINE];
    char body[MAXBUF];
    size_t buflen;
    size_t bodylen;

    /* Build the HTTP response body */
    bodylen = snprintf(body, MAXBUF,
            "<!DOCTYPE html>\r\n" \
            "<html>\r\n" \
            "<head><title>Tiny Error</title></head>\r\n" \
            "<body bgcolor=\"ffffff\">\r\n" \
            "<h1>%s: %s</h1>\r\n" \
            "<p>%s</p>\r\n" \
            "<hr /><em>The Tiny Web server</em>\r\n" \
            "</body></html>\r\n", \
            errnum, shortmsg, longmsg);
    if (bodylen >= MAXBUF) {
        return; // Overflow!
    }

    /* Build the HTTP response headers */
    buflen = snprintf(buf, MAXLINE,
            "HTTP/1.0 %s %s\r\n" \
            "Content-Type: text/html\r\n" \
            "Content-Length: %zu\r\n\r\n", \
            errnum, shortmsg, bodylen);
    if (buflen >= MAXLINE) {
        return; // Overflow!
    }

    /* Write the headers */
    if (rio_writen(fd, buf, buflen) < 0) {
        fprintf(stderr, "Error writing error response headers to client\n");
        return;
    }

    /* Write the body */
    if (rio_writen(fd, body, bodylen) < 0) {
        fprintf(stderr, "Error writing error response body to client\n");
        return;
    }
}

/**
 * @brief this function reads through  the headers in the client HTTP request
 * and checks that they are valid and have end headers and request headers
 * @param in takes in the client struct, rio struct, and parser
 * @return true if there is an error, false otherwise
*/
bool read_requesthdrs(client_info *client, rio_t *rp, parser_t *parser) {
    char buf[MAXLINE];

    // Read lines from socket until final carriage return reached
    while (true) {
        if (rio_readlineb(rp, buf, sizeof(buf)) <= 0) {
            return true;
        }

        /* Check for end of request headers */
        if (strcmp(buf, "\r\n") == 0) {
            return false;
        }

        // Parse the request header with parser
	    parser_state parse_state = parser_parse_line(parser, buf);
        if (parse_state != HEADER) {
	        clienterror(client->connfd, "400", "Bad Request",
			"Proxy could not parse request headers");
	        return true;
	    }
    }
}
/**
 * @brief this function iterates through all the parsed headers and finds
 * additional headers specified by the client
 * @param in takes in the total message length, the total string, the message, parser, and client file descriptor
*/
void check_header(size_t str_len, char* str, char* msg, parser_t *parser, int client_fd){
    header_t* head;
    size_t msg_len;
    //sio_printf("\nhere2\n");
    head=  parser_retrieve_next_header(parser);
    while(head != NULL){
        
       //sio_printf("\nthe header is %s\n", head->name);
        //if the header is not of the initial specified ones, print it out 
        if(strcmp(head->name,"Host") != 0 && strcmp(head->name, "User-Agent") != 0 && strcmp(head->name,"Connection")!=0 && strcmp(head->name, "Proxy-Connection") != 0){
            
            msg_len = sprintf(msg, "%s: %s\r\n",head->name, head->value);
            strcat(str, msg);
            str_len+=msg_len;
            //rio_writen(client_fd, msg, msg_len);

            
        }
            head =  parser_retrieve_next_header(parser);

    }//append the message to the final message string 
     msg_len = sprintf(msg,"\r\n");
    strcat(str, msg);
    str_len += msg_len;
    rio_writen(client_fd, str, str_len);
    
    //sio_printf("str inside func is %s\n", msg);

}
/**
 * @brief serve function copied from tiny.c -> forwards the client's HTTP
 * request to the server, and sends the server's response to the client
*/
void serve(client_info *client) { 
    rio_t rio;
    rio_readinitb(&rio, client->connfd);
    char buf[MAXLINE];
    if (rio_readlineb(&rio, buf, sizeof(buf)) <= 0) {
        return;
    }

    /* Parse the request line and check if it's well-formed */
    parser_t *parser = parser_new();

    parser_state parse_state = parser_parse_line(parser, buf);
  


    if (parse_state != REQUEST) {
        parser_free(parser);
	    clienterror(client->connfd, "400", "Bad Request",
		    "Proxy received a malformed request");
            fprintf(stderr,"method was not request");
	    return;
    }
    

     const char *method, *path, *host, *port, *url; //parses the host, path, method, etc. from first HTTP request line
    parser_retrieve(parser, METHOD, &method);
    parser_retrieve(parser, PATH, &path);
    parser_retrieve(parser, HOST, &host);
    parser_retrieve(parser, PORT, &port);
    parser_retrieve(parser, URI, &url);

    if(parser_retrieve(parser, PATH, & path) < 0){
        parser_free(parser);
        return;

    }


    int client_fd = open_clientfd(host, port);
    if(client_fd < 0){
        parser_free(parser);
        return;
    }
     if(parser_retrieve(parser, PORT, &port) == -1){
        clienterror(client->connfd, "Port", "Not parsed","Proxy does not parse");
        fprintf(stderr,"other error");
        parser_free(parser);
        close(client_fd);
        return;
    }
    //if no port is specified, use default port
    if(parser_retrieve(parser, PORT, &port) == -1){
        port = "80";
    }


    /* Check that the method is GET */
    if (strcmp(method, "GET") != 0) {
	    parser_free(parser);
        fprintf(stderr,"method was not GET");
    	clienterror(client->connfd, "501", "Not Implemented","Proxy does not implement");
        close(client_fd);
    	return;
    }
    //check the headers are correctly formatted
   if (read_requesthdrs(client, &rio, parser)) {
        parser_free(parser);
        close(client_fd);
	    return;
   }

   //pthread_mutex_init(&lock, NULL);

   pthread_mutex_lock(&lock);
   //check if the uri is in the cache if yes, then we return directly to server
   cache_elem* node = cache_lookup( url);
    pthread_mutex_unlock(&lock);

   if(node!=NULL){
        
        //sio_printf("here with msg %s\n", node->msg);
        rio_writen(client->connfd, node->msg, node->obj_size);
        
   } 
   else{    
    //else we write a message, insert into cache, and send to server

        header_t* head = parser_lookup_header(parser, "Host");

        char msg[MAXLINE];
        char str[MAXLINE];
        str[0] = '\0';
        size_t msg_len;
        size_t str_len = 0;
        //check if host is specified
        if(head != NULL){
        
        msg_len = sprintf(msg,
                "%s %s HTTP/1.0\r\n" \
                "Host: %s:%s \r\n" \
                "User-Agent: %s\r\n" \
                "Connection: close\r\n" \
                "Proxy-Connection: close\r\n", \
                method, path, host,port,header_user_agent );
                //iterate further to forward other heders
            str_len += msg_len; 
        }
        else{
            
            msg_len = sprintf(msg,
                "%s %s HTTP/1.0\r\n" \
                "Host: %s:%s\r\n" \
                "User-Agent: %s\r\n" \
                "Connection: close\r\n" \
                "Proxy-Connection: close\r\n", \
                method, path, host, port,header_user_agent );
            str_len += msg_len; 

        }
        strcat(str, msg);
        
        check_header(str_len, str, msg, parser, client_fd );
   
        //reads from server and writes to client
        rio_t rep; 
        char res_buf[MAXLINE];
        char cache_msg[MAX_OBJECT_SIZE];
        ssize_t len;
        ssize_t msg_size = 0;
        rio_readinitb(&rep, client_fd);
        while((len = rio_readnb(&rep, res_buf, sizeof(res_buf))) > 0 ) { //returns num of bytes
            rio_writen(client->connfd, res_buf, len);//re
            if(msg_size + len <= MAX_OBJECT_SIZE){ //if message is in correct size range
                memcpy(cache_msg + msg_size,res_buf,len);
            }
            msg_size+=len;


        }    
        //insert into cache
        pthread_mutex_lock(&lock);
         if(cache_lookup(url) == NULL){//checks that no additional duplicates exist
            cache_insert( url, cache_msg, msg_size);
         }
       pthread_mutex_unlock(&lock);
            

    
   }

    parser_free(parser);
    close(client_fd);
}

/**
 *@brief this function creates detached threads that reap themselves
 @param in this function takes in any generic type, in this case takes
 the client file descriptor
 @return this function returns NULL 
*/
void *thread(void *vargp)
{   
    client_info client;
    client.addrlen = sizeof(client.addr);
    client.connfd= *((int*)vargp);
    pthread_detach(pthread_self());
    free(vargp);
    serve(&client);
    close(client.connfd);
    return NULL;
}

int main(int argc, char **argv) {
    Signal(SIGPIPE, SIG_IGN);

    int listenfd;
    int* connfd;
    pthread_t tid;
    start = malloc(sizeof(cache_list));
    start->head = NULL;
    start->tail = NULL;
    start->size = 0; 

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // Open listening file descriptor
    listenfd = open_listenfd(argv[1]);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port: %s\n", argv[1]);
        exit(1);
    }

    while (1) {
        /* Allocate space on the stack for client info */
        client_info client_data;
        client_info *client = &client_data;

        /* Initialize the length of the address */
        client->addrlen = sizeof(client->addr);

        /* accept() will block until a client connects to the port */
        client->connfd = accept(listenfd,
                (SA *) &client->addr, &client->addrlen);

        if (client->connfd < 0) {
            perror("accept");
            continue;
        }

        connfd = malloc(sizeof(int));
        *connfd = client->connfd;
         pthread_mutex_init(&lock, NULL);
        pthread_create(&tid, NULL, thread, connfd);


    }
    return 0;
}
