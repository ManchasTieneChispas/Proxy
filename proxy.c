/* @author William Giraldo (wgiraldo)
 *
 * Starter code for proxy lab.
 * Feel free to modify this code in whatever way you wish.
 */

/* Some useful includes to help you get started */

#include "csapp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <assert.h>

#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

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

/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
#define MAX_CACHE_SIZE (1024*1024)
#define MAX_OBJECT_SIZE (100*1024)

/* Typedef for convenience */
typedef struct sockaddr SA;

/* Information about a connected client. This is adapted from TINY server */
typedef struct {
    struct sockaddr_in addr;    // Socket address
    socklen_t addrlen;          // Socket address length
    int connfd;                 // Client connection file descriptor
    char host[HOSTLEN];         // Client host
    char serv[SERVLEN];         // Client service (port)
} client_info;

/* URI parsing results. Adapted from TINY server */
typedef enum {
    PARSE_ERROR,
    PARSE_STATIC,
    PARSE_DYNAMIC
} parse_result;

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
    if (strncmp(uri, "/cgi-bin/", strlen("/cgi-bin/")) == 0) { /* Dynamic content */
        char *args = strchr(uri, '?');  /* Find the CGI args */
        if (!args) {
            *cgiargs = '\0';    /* No CGI args */
        } else {
            /* Format the CGI args */
            if (snprintf(cgiargs, MAXLINE, "%s", args + 1) >= MAXLINE) {
                return PARSE_ERROR; // Overflow!
            }

            *args = '\0';   /* Remove the args from the URI string */
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
    if (snprintf(filename, MAXLINE, ".%s%s",
                 uri, is_dir ? "home.html" : "") >= MAXLINE) {
        return PARSE_ERROR; // Overflow!
    }

    return PARSE_STATIC;
}

/* The following code has parts adapted from TINY server (tiny.c)
 *
 * read_requesthdrs - read HTTP request headers
 *
 * Returns true if an error occurred, or false otherwise.
 */
bool read_requesthdrs(client_info *client, rio_t *rp) {
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
                        "Tiny could not parse request headers");
            return true;
        }

        /* Convert name to lowercase */
        for (size_t i = 0; name[i] != '\0'; i++) {
            name[i] = tolower(name[i]);
        }

        printf("%s: %s\n", name, value);
    }
}


/* The following code contains pieces adapted from TINY server (tiny.c)
 *
 * serve takes in a client_info*. It then creates a connection to the clients
 * requested server, and then returns the information from this server to the
 * client.
 *
 * Requires that client contains valid information
 */
void serve(client_info *client) {
  // Get some extra info about the client (hostname/port)
  int res = getnameinfo(
          (SA *) &client->addr, client->addrlen,
          client->host, sizeof(client->host),
          client->serv, sizeof(client->serv),
          0);
  if (res == 0) {
      printf("Accepted connection from %s:%s\n", client->host, client->serv);
  }
  else {
      fprintf(stderr, "getnameinfo failed: %s\n", gai_strerror(res));
  }

  rio_t rio;
  rio_readinitb(&rio, client->connfd);

  /* Read request line */
  char buf[MAXLINE];
  if (rio_readlineb(&rio, buf, sizeof(buf)) <= 0) {
      return;
  }

  printf("%s", buf);

  /* Parse the request line and check if it's well-formed */
  char method[MAXLINE];
  char uri[MAXLINE];
  char version;

  /* sscanf must parse exactly 3 things for request line to be well-formed */
  /* version must be either HTTP/1.0 or HTTP/1.1 */
  if (sscanf(buf, "%s %s HTTP/1.%c", method, uri, &version) != 3
          || (version != '0' && version != '1')) {
      clienterror(client->connfd, "400", "Bad Request",
                  "Proxy received a malformed request");
      return;
  }

  /* Check that the method is GET */
  if (strcmp(method, "GET") != 0) {
      clienterror(client->connfd, "501", "Not Implemented",
                  "Proxy does not implement this method");
      return;
  }

  /* Check if reading request headers caused an error */
  if (read_requesthdrs(client, &rio)) {
      return;
  }

  // /* Parse URI from GET request */
  // char filename[MAXLINE], cgiargs[MAXLINE];
  // parse_result result = parse_uri(uri, filename, cgiargs);
  // if (result == PARSE_ERROR) {
  //     clienterror(client->connfd, "400", "Bad Request",
  //                 "Proxy could not parse the request URI");
  //     return;
  // }

  /* Determine connection port */
  int port_int;
  int res;
  char port[MAXLINE];
  char url[MAXLINE];
  if((res = sscanf("%s:%d", &url, &port_int)) == 1) {
    snprintf(port, sizeof(port), "%d", 80);
  } else if(res == 2) {
    snprintf(port, sizeof(port), "%d", port_int);
  } else {
    clienterror(client->connfd, "400", "malformed uri");
    return;
  }

  /* Establish connection with server */
  int serverfd;
  if((serverfd = open_clientfd(url, port)) < 0) {
    clienterror(client->connfd, "400", "Proxy cannot reach destination");
    return;
  }

}

int main(int argc, char** argv) {
    printf("%s\n", header_user_agent);

    /*check if a port was passed */
    if(argc != 2) {
      printf("Please pass a port to wait for connections on\n");
      exit(1);
    }

    int listenfd = open_listenfd(argv[1]);
    if(listenfd < 0) {
      printf("Failed to listen on port \n", argv[1]);
    }

    /*Create space for client info. This section adapted from TINY server */
    client_info client_data;
    client_info *client = &client_data;

    client->connfd = accept(listenfd, (SA*)&client->addr, &client->addrlen);
    if(client->connfd < 0) {
      printf("Accept error: %s\n", strerror(errno));
      exit(1);
    }

    serve(client);

    return 0;
}
