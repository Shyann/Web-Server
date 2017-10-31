#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "zlib.h"

/* buffer size for zlib routines */
#define CHUNK 256000

/* error handling from linux man pages */
#define handle_error(msg) \
		do { perror(msg); exit(EXIT_FAILURE); } while (0)

/* Function declarations */	
void* handle_client(void *sock);
char *strcasestr(const char *haystack, const char *needle);
char *getcwd(char *buf, size_t size);

char* port;
char folder[1024];

int main(int argc, char** argv) {	

	/* error handling for improper arguments */	
	if(argc < 2) { printf("Usage: ./http_server <port>\n"); exit(1); }

	port = argv[1];

	/* attempt to get web server directory */	
	if (getcwd(folder, sizeof(folder)) != NULL)
		strcat(folder,"/public_html");
	 else 
		handle_error("Error retreiving directory");
	
	/* create TCP socket capable of accepting IPv6 addresses */
	int server_sock = socket(AF_INET6, SOCK_STREAM, 0);
	if(server_sock == -1) 
		handle_error("Error creating socket");

	/* IPv6 data structure */
	struct sockaddr_in6 addr;
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(atoi(port)); // order most significant bytes first
	addr.sin6_addr = in6addr_any;
	
	/* bind socket to local address */
	if ( bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1)
		handle_error("Error binding to port");

	/* data structure for remote addresses */
	struct sockaddr_in remote_addr;
	unsigned int socklen = sizeof(remote_addr); 

	/* put socket in listen state */
	if (listen(server_sock,5) == -1)
		handle_error("Error listening for connection");

	while(1) {
		int sock;
		/* Accept first connection in queue */
		sock = accept(server_sock, (struct sockaddr*)&remote_addr, &socklen);
		if (sock == -1)
			handle_error("Error accepting connection");
			
		pthread_t client;
		/* create new pthread to handle client*/
		pthread_create(&client,0,handle_client,(void*)(intptr_t)sock);
	}

	/* shut down socket send and receive functions */
	shutdown(server_sock,SHUT_RDWR);
}

/* check if file exists */
int file_exists(char* path) {
	struct stat filestat;
	return !stat(path,&filestat);
}

/* check if path is directory */
int is_directory(char* path) {
	struct stat filestat;
	if(!stat(path,&filestat))
		return filestat.st_mode & S_IFDIR;
	else return 0;
}

/* read file and send to socket */
void send_file(int sock, char* path) {
	char buffer[1400];
	int in;
	FILE *data=fopen(path,"r");
	
	while((in=fread(buffer,1,sizeof(buffer),data))) {
		send(sock,buffer,in,0);
	}
	fclose(data);
}

/* send part of file to socket */
void send_file_partial(int sock, char* path, int lbound, int ubound) {
	char buffer[1400];
	char partial[1400];

	if (ubound <= sizeof(buffer)){		
		FILE *data=fopen(path,"r");
		fread(buffer,1,sizeof(buffer),data);
		/* copy buffer from lower bound to upper bound and send with a new line */
		memcpy(partial,buffer + lbound,(ubound-lbound));
		send(sock,partial,ubound,0); send(sock,"\n",2,0);
		fclose(data);
	} else {
		handle_error("Stack smashing");
	}
}

/* deflate function used for compression taken from zlib website */
int def(int sock, char* resp) {
	FILE *data = fopen(resp, "r");

	int flush;
	unsigned have;
	z_stream strm;
	unsigned char in[CHUNK];
	unsigned char out[CHUNK];

	/* allocate the deflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	deflateInit(&strm, -1);

	/* compress until end of file */
	strm.avail_in = fread(in, 1, sizeof(in), data);
	if (ferror(data)) {
		(void)deflateEnd(&strm);
		return Z_ERRNO;
	}

	flush = feof(data) ? Z_FINISH : Z_NO_FLUSH;
	strm.next_in = in;

	/* run deflate() on input until output buffer not full and send.
	   finish compression if all of source has been read in */
	do {
		strm.avail_out = CHUNK;
		strm.next_out = out;
		deflate(&strm, flush);
		have = CHUNK - strm.avail_out;
		send(sock, out, have, 0);
	} while (strm.avail_out == 0);

	/* clean up and return */
	(void)deflateEnd(&strm);
	return Z_OK;
}

/* return content type for file; for use in headers */
char* content_type_for_path(char* path) {
	if(strcasestr(path,".html")) 
		return "text/html";
	else if(strcasestr(path,".txt")) 
		return "text/plain";
	else if(strcasestr(path,".gif")) 
		return "image/gif";
	else if(strcasestr(path,".jpg")) 
		return "image/jpeg";
 	else if(strcasestr(path,".png")) 
		return "image/png";				
	else if(strcasestr(path,".ico"))
		return "image/x-icon";
	else
		return "text/plain";
}

void display_directory(int sock, char *path,char *fullpath,char* response){
	DIR * dir;
	int len = strlen(fullpath)-strlen("index.html");
	char pathBuff[255];
	char tmpBuff[255];
	strncpy(pathBuff, fullpath, len);
	pathBuff[len] = '\0';
	dir = opendir(pathBuff);
	if(!dir){
		fprintf(stderr, "pathBuff: %s\n", pathBuff);
		fprintf(stderr, "Cannot open directory '%s': %s\n", fullpath, strerror(errno));
		exit(EXIT_FAILURE);
	}

	sprintf(response, "<!DOCTYPE HTML><html>\r\n"
			"<title>Index of %s</title>\r\n"
			"<body>\r\n"
			"<h2>Index of %s</h2>\r\n"
			"<hr>\r\n"
			"<ul>\r\n", path, path);
	send(sock,response,strlen(response),0);
	while(1){
		struct dirent * entry;
		entry=readdir(dir);
		if(!entry)
			break;
		strcpy(tmpBuff, pathBuff);
		strcat(tmpBuff, entry->d_name);

		if(is_directory(tmpBuff))
			sprintf(response, "<li><a href=\"%s/\">%s/</a>\r\n", entry->d_name, entry->d_name);
		else
			sprintf(response, "<li><a href=\"%s\">%s</a>\r\n", entry->d_name, entry->d_name);
		
		send(sock,response,strlen(response),0);
		
	}
	close((uintptr_t)dir);
	sprintf(response, "</ul>\r\n"
			"<hr>\r\n"
			"</body>\r\n"
			"</html>\r\n");
	send(sock,response,strlen(response),0);
}

/* method for dealing with new clients */
void *handle_client(void* arg) {
	int sock = (int)(uintptr_t)arg;
	
	char request[2000], response[10000], path[255], fullpath[255];
	
	/* header requests */
	int is_range = 0, is_range_ok = 0, is_head = 0, is_dir = 0, compress_files = 0;

	/* receive request till end */
	int recv_accum = 0;
	while(!strstr(request,"\r\n\r\n")) {
		/* number of bytes received */
		int recv_count = recv(sock, request+recv_accum, sizeof(request)-recv_accum, 0);
		if(recv_count == -1)
			handle_error("Receive failed");
		recv_accum+=recv_count;
	}

	/* print http request with headers */
	printf("%s", request);

	int lbound; int ubound;	
	/* parse range request for lower bound and upper bound */
	if (strstr(request, "Range: bytes=") != NULL) {
		is_range = 1;
		char *ptr;
		strtok_r (request, "=", &ptr);
		lbound = atoi(strtok(ptr, "-")); lbound *= 8;
		ubound = atoi(strtok(NULL, "-")); ubound *= 8;
	}

	/* check if valid request */
	if( (!sscanf(request,"GET %[^ ] HTTP/1.1",path)) && (!sscanf(request,"HEAD %[^ ] HTTP/1.1",path)) ){
		sprintf(response,"HTTP/1.1 400 Error: Bad Request\r\n\r\n");
	} else {
		sprintf(fullpath,"%s%s",folder,path);

		/* check if head request */
		if (sscanf(request,"HEAD %[^ ] HTTP/1.1",path)) is_head = 1;

		/* check if path is directory and use index file if it is */
		if(is_directory(fullpath)) {
			printf("Directory requested; appending index.html\n");
			sprintf(fullpath+strlen(fullpath),"index.html");
			is_dir = 1;
			is_range_ok = 0;
		}

		/* check if header allows compression */
		if ( (strstr(request, "Accept-Encoding:") != NULL) &&  (strstr(request, "deflate") != NULL) )
			compress_files = 1;
		
		/* print 404 error if file does not exist */
		if(!file_exists(fullpath) && !(strstr(fullpath, "index.html"))) {	
			sprintf(response,"HTTP/1.1 404 Not Found.\r\n"
								 "Content-Type: text/html\r\n\r\n"
						         "<html><body><h1>404 Not Found: The requested URL was not found on this server.</h1>"
						         "</body></html>");
		} else {
			/* determing appropriate content-type header */
			char *content_type=content_type_for_path(fullpath);

			/* valid range request */
			if((is_range) && (ubound > lbound)){
				sprintf(response,"HTTP/1.1 206 Partial Content\r\nContent-Type: %s\r\n\r\n",content_type);
				is_range_ok = 1;
			/* invalid range request */
			} else if (is_range) {
				sprintf(response,"HTTP/1.1 416 Requested Range Not Satisfiable\r\nContent-Type: %s\r\n\r\n",content_type);
			/* request is not a directory and files can be compressed */
			} else if (!is_dir && compress_files) {
				sprintf(response,"HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
				"Accept-Ranges: bytes\r\nContent-Encoding: deflate\r\n\r\n",content_type);
			} else {
				sprintf(response,"HTTP/1.1 200 OK\r\nContent-Type: %s\r\n\r\n",content_type);
			}

		}
	}
	
	printf("Response: %s\n",response);

	/* send response header */
	send(sock,response,strlen(response),0);

	/* don't send response for head requests or if file doesn't exist */
	if (!(is_head) && file_exists(fullpath)) {
		/* compress files which aren't ranges or directories and request supports it */
		if(!(is_range) && !(is_dir) && compress_files){
			def(sock, fullpath);
		/* don't compress directories or when invalid header */
		} else if(!(is_range)){
			send_file(sock, fullpath);
		/* send partial file for range requests */
		} else if(is_range && is_range_ok) {
			send_file_partial(sock,fullpath,lbound,ubound);		
		}
	} else {
		if(strstr(fullpath, "index.html"))
			display_directory(sock,path,fullpath,response);
	}

	close(sock);
	return 0;
}
