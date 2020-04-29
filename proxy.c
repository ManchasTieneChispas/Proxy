/* @author William Giraldo (wgiraldo)
 *
 * Starter code for proxy lab.
 * Feel free to modify this code in whatever way you wish.
 */

/* Some useful includes to help you get started */

#include "csapp.h"

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

#define HOSTLEN 256
#define SERVLEN 8
#define READLEN 4096

/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

/* Typedef for convenience */
typedef struct sockaddr SA;

/* Information about a connected client. This is adapted from TINY server */
typedef struct {
    struct sockaddr_in addr; // Socket address
    socklen_t addrlen;       // Socket address length
    int connfd;              // Client connection file descriptor
    char host[HOSTLEN];      // Client host
    char serv[SERVLEN];      // Client service (port)
} client_info;

/* URI parsing results. Adapted from TINY server */
typedef enum { PARSE_ERROR, PARSE_STATIC, PARSE_DYNAMIC } parse_result;

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20191101 Firefox/63.0.1";

/* This code is adapted from TINY server (tiny.c)
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, const char *errnum, const char *shortmsg,
                 const char *longmsg) {
    char buf[MAXLINE];
    char body[MAXBUF];
    size_t buflen;
    size_t bodylen;

    /* Build the HTTP response body */
    bodylen = snprintf(body, MAXBUF,
                       "<!DOCTYPE html>\r\n"
                       "<html>\r\n"
                       "<head><title>Proxy Error</title></head>\r\n"
                       "<body bgcolor=\"ffffff\">\r\n"
                       "<h1>%s: %s</h1>\r\n"
                       "<p>%s</p>\r\n"
                       "<hr /><em>The PRoxyLab Proxy</em>\r\n"
                       "</body></html>\r\n",
                       errnum, shortmsg, longmsg);
    if (bodylen >= MAXBUF) {
        return; // Overflow!
    }

    /* Build the HTTP response headers */
    buflen = snprintf(buf, MAXLINE,
                      "HTTP/1.0 %s %s\r\n"
                      "Content-Type: text/html\r\n"
                      "Content-Length: %zu\r\n\r\n",
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

/* This code pas parts adapted from TINY server (tiny.c)
 * parse_uri - parse URI into filename and CGI args
 *
 * uri - The buffer containing URI. Must contain a NUL-terminated string.
 * filename - The buffer into which the filename will be placed.
 * cgiargs - The buffer into which the CGI args will be placed.
 * NOTE: All buffers must hold MAXLINE bytes, and will contain NUL-terminated
 * strings after parsing.
 *
 * Returns the appropriate parse result for the type of request.
 */
parse_result parse_uri(char *uri, char *filename, char *cgiargs) {
    /* Assume URI starts with / */
    if (uri[0] != '/') {
        return PARSE_ERROR;
    }

    /* Check if the URI contains "cgi-bin" */
    if (strncmp(uri, "/cgi-bin/", strlen("/cgi-bin/")) ==
        0) {                           /* Dynamic content */
        char *args = strchr(uri, '?'); /* Find the CGI args */
        if (!args) {
            *cgiargs = '\0'; /* No CGI args */
        } else {
            /* Format the CGI args */
            if (snprintf(cgiargs, MAXLINE, "%s", args + 1) >= MAXLINE) {
                return PARSE_ERROR; // Overflow!
            }

            *args = '\0'; /* Remove the args from the URI string */
        }

        /* Format the filename */
        if (snprintf(filename, MAXLINE, ".%s", uri) >= MAXLINE) {
            return PARSE_ERROR; // Overflow!
        }

        return PARSE_DYNAMIC;
    }

    /* Static content */
    /* No CGI args */
    *cgiargs = '\0';

    /* Make a valiant effort to prevent directory traversal attacks */
    if (strstr(uri, "/../") != NULL) {
        return PARSE_ERROR;
    }

    /* Check if the client is requesting a directory */
    bool is_dir = uri[strnlen(uri, MAXLINE) - 1] == '/';

    /* Format the filename; if requesting a directory, use the home file */
    if (snprintf(filename, MAXLINE, ".%s%s", uri, is_dir ? "home.html" : "") >=
        MAXLINE) {
        return PARSE_ERROR; // Overflow!
    }

    return PARSE_STATIC;
}

/* The following code has parts adapted from TINY server (tiny.c)
 *
 * read_responsehdrs - read HTTP response headers.
 *
 * Returns true if an error occurred, or false otherwise.
 */
bool read_responsehdrs(client_info *client, rio_t *rp) {
    char buf[MAXLINE];
    char name[MAXLINE];
    char value[MAXLINE];

    while (true) {
        if (rio_readlineb(rp, buf, sizeof(buf)) <= 0) {
            return true;
        }

        /* Check for end of request headers */
        if (strcmp(buf, "\r\n") == 0) {
            return false;
        }

        /* Parse header into name and value */
        if (sscanf(buf, "%[^:]: %[^\r\n]", name, value) != 2) {
            /* Error parsing header */
            clienterror(client->connfd, "400", "Bad Request",
                        "Proxy could not parse request headers");
            return true;
        }
    }
    return false;
}

/* The following code has parts adapted from TINY server (tiny.c)
 *
 * read_requesthdrs - read HTTP request headers. If there is a host header,
 * then it puts it into host. If there is any headers besides Host,
 * User-Agent, Connection, and Proxy-Connection, then it combines them and
 * places them into rest
 *
 * Returns true if an error occurred, or false otherwise.
 */
bool read_requesthdrs(client_info *client, rio_t *rp, char *host, char *rest) {
    char buf[MAXLINE];
    char name[MAXLINE];
    char value[MAXLINE];
    size_t prev_write = 0;

    while (true) {
        if (rio_readlineb(rp, buf, sizeof(buf)) <= 0) {
            return true;
        }

        /* Check for end of request headers */
        if (strcmp(buf, "\r\n") == 0) {
            return false;
        }

        /* Parse header into name and value */
        if (sscanf(buf, "%[^:]: %[^\r\n]", name, value) != 2) {
            /* Error parsing header */
            clienterror(client->connfd, "400", "Bad Request",
                        "Proxy could not parse request headers");
            return true;
        }

        if (strcmp(name, "Host") == 0) {
            snprintf(host, MAXLINE, "%s: %s\r\n", name, value);
        }

        if (strcmp(name, "Host") != 0 && strcmp(name, "User-Agent") != 0 &&
            strcmp(name, "Connection") != 0 &&
            strcmp(name, "Proxy-Connection") != 0) {
            prev_write += snprintf(rest + prev_write, MAXBUF - prev_write,
                                   "%s: %s\r\n", name, value);
        }
    }
    return host;
}

int get_conn_info(client_info *client, char *uri, char *hostname, char *port,
                  char *dir) {
    char url[MAXLINE];

    // split hostname and directory
    int res = sscanf(uri, "http://%[^/]/%s", url, dir);
    if (res == 1) {
        dir[0] = '\0';
    } else if (res != 2) {
        clienterror(client->connfd, "400", "malformed uri",
                    "proxy could not parse the uri");
        return -1;
    }

    // split url and port
    res = sscanf(url, "%[^:]:%s", hostname, port);
    if (res == 1) {
        snprintf(port, MAXLINE, "80");
    } else if (res != 2) {
        clienterror(client->connfd, "400", "malformed url",
                    "Proxy could not parse the URL");
        return -1;
    }

    return 0;
}

/* The following code contains pieces adapted from TINY server (tiny.c)
 *
 * serve takes in a client_info*. It then creates a connection to the clients
 * requested server, and then returns the information from this server to the
 * client.
 *
 * Requires that client contains valid information
 */
void *serve(void *vargp) {
    // get client var, detach thread
    client_info *client = (client_info *)vargp;
    pthread_detach(pthread_self());

    // Get some extra info about the client (hostname/port)
    int res = getnameinfo((SA *)&client->addr, client->addrlen, client->host,
                          sizeof(client->host), client->serv,
                          sizeof(client->serv), 0);
    if (res == 0) {
        printf("Accepted connection from %s:%s\n", client->host, client->serv);
    } else {
        fprintf(stderr, "getnameinfo failed: %s\n", gai_strerror(res));
    }

    rio_t rio;
    rio_readinitb(&rio, client->connfd);

    /* Read request line */
    char buf[MAXLINE];
    if (rio_readlineb(&rio, buf, sizeof(buf)) <= 0) {
        return NULL;
    }

    /* Parse the request line and check if it's well-formed */
    char method[MAXLINE];
    char uri[MAXLINE];
    char version;

    /* sscanf must parse exactly 3 things for request line to be well-formed */
    /* version must be either HTTP/1.0 or HTTP/1.1 */
    if (sscanf(buf, "%s %s HTTP/1.%c", method, uri, &version) != 3 ||
        (version != '0' && version != '1')) {
        clienterror(client->connfd, "400", "Bad Request",
                    "Proxy received a malformed request");
        return NULL;
    }

    /* Check that the method is GET */
    if (strcmp(method, "GET") != 0) {
        clienterror(client->connfd, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return NULL;
    }

    char host_header[MAXBUF];
    char other_headers[MAXBUF];
    host_header[0] = '\0';
    /* Check if reading request headers caused an error, and read Host header
       into host_header, as well as any other extraneous headers  */
    if (read_requesthdrs(client, &rio, host_header, other_headers)) {
        return NULL;
    }

    /* Determine connection port, hostname and directory*/
    char port[MAXLINE];
    char dir[MAXLINE];
    char hostname[MAXLINE];
    if ((res = get_conn_info(client, uri, hostname, port, dir)) < 0) {
        return NULL;
    }

    /* Establish connection with server */
    int serverfd;
    if ((serverfd = open_clientfd(hostname, port)) < 0) {
        clienterror(client->connfd, "400", "Proxy cannot reach destination",
                    "Proxy could not conacnt destination server");
        return NULL;
    }

    rio_t s_rio;
    rio_readinitb(&s_rio, serverfd);

    /* Create Host key:value if not passed by client */
    if (host_header[0] == '\0') {
        snprintf(host_header, MAXLINE, "Host: %s:%s", hostname, port);
    }

    /* Create HTTP requst with headers */
    char get_req[MAXBUF];
    size_t req_length =
        snprintf(get_req, sizeof(get_req),
                 "GET /%s HTTP/1.0\r\n"
                 "%s"
                 "User-Agent: %s\r\n"
                 "Connection: close\r\n"
                 "Proxy-Connection: close\r\n"
                 "%s\r\n",
                 dir, host_header, header_user_agent, other_headers);

    printf("Resonse Headers: %s\n", get_req);

    /* Send the request to the server */
    if (rio_writen(serverfd, get_req, req_length) < 0) {
        fprintf(stderr, "Error writing to server\n");
        close(serverfd);
        return NULL;
    }

    // use calloc to 0 out
    char *res_buf = Calloc((MAX_OBJECT_SIZE) + MAXBUF, 1);

    int bytes_in;
    while ((bytes_in = rio_readnb(&s_rio, res_buf, READLEN)) != 0) {
        if (rio_writen(client->connfd, res_buf, bytes_in) < 0) {
            fprintf(stderr, "Error writing to server\n");
            close(serverfd);
            return NULL;
        }
        memset(res_buf, 0, bytes_in);
    }

    // cleanup fds and client
    close(serverfd);
    close(client->connfd);
    free(client);
    free(res_buf);

    return NULL;
}

int main(int argc, char **argv) {

    Signal(SIGPIPE, SIG_IGN);

    /*check if a port was passed */
    if (argc != 2) {
        printf("Please pass a port to wait for connections on\n");
        exit(1);
    }

    int listenfd = open_listenfd(argv[1]);
    if (listenfd < 0) {
        printf("Failed to listen on port %s\n", argv[1]);
    }

    int clientfd;
    while (1) {
        // allocate space for client struct on heap
        client_info *client = Malloc(sizeof(client_info));

        // accept connection
        clientfd = accept(listenfd, (SA *)&client->addr, &client->addrlen);

        // if valid clientfd, then create a thread and serve
        if (clientfd >= 0) {
            client->connfd = clientfd;
            pthread_t tid;
            pthread_create(&tid, NULL, &serve, (void *)client);
        }
    }
    return 0;
}
