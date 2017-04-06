/* 
 * File: sws.c
 * Author: Alex Brodsky
 * Purpose: This file contains the implementation of a simple web server.
 *          It consists of two functions: main() which contains the main 
 *          loop accept client connections, and serve_client(), which
 *          processes each client request.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>

#include "network.h"

#define MAX_HTTP_SIZE 8192                 /* size of buffer to allocate */
#define MAX_QUEUE_SIZE 100                 /* size of ready queue */
#define MAX_FILE_SIZE 8192                 /* size of a quantum */
#define MAX_THREADS 100

int thread_count = 0; /* Keeps track of the number of threads to be generated */
int scheduler;        /* Global variable to get scheduler algorithm */
int seq_count = 0;    /* Keeps track of the current file in the sequence */

/* RCB */
struct RCB {
  int seq;           /* Sequence number*/
  int fd;            /* File descriptor */
  FILE *handle;      /* File handle */
  int bytes_rem;     /* Remaining bytes to be sent */
  int quant;         /* Quantum or maximum number of bytes to be sent */
};

struct RCB ready_queue[MAX_QUEUE_SIZE];  /* This initializes an empty ready queue for 100 RCBS */

/* This function takes a file handle to a client, reads in the request, 
 *    parses the request, and sends back the requested file.  If the
 *    request is improper or the file is not available, the appropriate
 *    error is sent back.
 * Parameters: 
 *             fd : the file descriptor to the client connection
 * Returns: None
 */
static void serve_client( int fd ) {
  static char *buffer;                              /* request buffer */
  char *req = NULL;                                 /* ptr to req file */
  char *brk;                                        /* state used by strtok */
  char *tmp;                                        /* error checking ptr */
  FILE *fin;                                        /* input file handle */
  int len;                                          /* length of data read */

  if( !buffer ) {                                   /* 1st time, alloc buffer */
    buffer = malloc( MAX_HTTP_SIZE );
    if( !buffer ) {                                 /* error check */
      perror( "Error while allocating memory" );
      abort();
    }
  }

  memset( buffer, 0, MAX_HTTP_SIZE );
  if( read( fd, buffer, MAX_HTTP_SIZE ) <= 0 ) {    /* read req from client */
    perror( "Error while reading request" );
    abort();
  } 

  /* standard requests are of the form
   *   GET /foo/bar/qux.html HTTP/1.1
   * We want the second token (the file path).
   */
  tmp = strtok_r( buffer, " ", &brk );              /* parse request */
  if( tmp && !strcmp( "GET", tmp ) ) {
    req = strtok_r( NULL, " ", &brk );
  }
 
  if( !req ) {                                      /* is req valid? */
    len = sprintf( buffer, "HTTP/1.1 400 Bad request\n\n" );
    write( fd, buffer, len );                       /* if not, send err */
  } else {                                          /* if so, open file */
    req++;                                          /* skip leading / */
    fin = fopen( req, "r" );                        /* open file */
    if( !fin ) {                                    /* check if successful */
      len = sprintf( buffer, "HTTP/1.1 404 File not found\n\n" );  
      write( fd, buffer, len );                     /* if not, send err */
    } else {                                        /* if so, send file */
      len = sprintf( buffer, "HTTP/1.1 200 OK\n\n" );/* send success code */
      write( fd, buffer, len );

      do {                                          /* loop, read & send file */
        len = fread( buffer, 1, MAX_HTTP_SIZE, fin );  /* read file chunk */
        if( len < 0 ) {                             /* check for errors */
            perror( "Error while writing to client" );
        } else if( len > 0 ) {                      /* if none, send chunk */
          len = write( fd, buffer, len );
          if( len < 1 ) {                           /* check for errors */
            perror( "Error while writing to client" );
          }
        }
      } while( len == MAX_HTTP_SIZE );              /* the last chunk < 8192 */
      fclose( fin );
    }
  }
  close( fd );                                     /* close client connectuin*/
}

/* This function will take in file descriptors, then create an RCB and add it to a
 * queue and will then process the files using the round robin scheduler 
 */
void round_robin( int fd ) {
  struct stat fileStat;
  /* Create a new RCB in the queue */
  ready_queue[seq_count].fd = fd;
  ready_queue[seq_count].seq = seq_count;
  ready_queue[seq_count].quant = MAX_FILE_SIZE;

  seq_count++;

  /* Loop through the queue */
  for ( int i=0; i<seq_count; i++ ) {
    /* If the file is within the limit */
    if ( fstat( fd, &fileStat) > 0) {
      if ( fileStat.st_size <= 8192 ) {
        serve_client(fd);
      }
    }
    else {
        serve_client(fd);
    }
  }
}
void short_job_first( int fd ) {
  serve_client(fd);
}
void mlfb( int fd ) {
  serve_client(fd);
}
/* This function is where the program starts running.
 *    The function first parses its command line parameters to determine port #
 *    Then, it initializes, the network and enters the main loop.
 *    The main loop waits for a client (1 or more to connect, and then processes
 *    all clients by calling the seve_client() function for each one.
 * Parameters: 
 *             argc : number of command line parameters (including program name
 *             argv : array of pointers to command line parameters
 * Returns: an integer status code, 0 for success, something else for error.
 */
int main( int argc, char **argv ) {
  int port = -1;                                    /* server port # */
  int fd;                                           /* client file descriptor */
  int threads = 0;
  /* check for and process parameters 
   */
  if( ( argc < 4 ) || ( sscanf( argv[1], "%d", &port ) < 1 ) ) {
    printf( "usage: sms <port> <scheduler> <num threads>\n" );
    return 0;
  }

  if ( strcmp(argv[2],"SJF")==0 ) {
    scheduler = 1; /* SJF is scheduler 1 */
  }
  else if ( strcmp(argv[2],"RR")==0  ) {
    scheduler = 2; /* RR is scheduler 2 */
    printf("Using the round robin scheduler\n");
  }
  else if ( strcmp(argv[2],"MLFB")==0  ) {
    scheduler = 3; /* MLFB is scheduler 3*/
  }
  else {
    printf("Error: invalid scheduler\n");
    return 0;  /* exit */
  }
  
  if ( sscanf( argv[3], "%d", &threads ) <1 ||  sscanf( argv[3], "%d", &threads )>MAX_THREADS ) {
    printf("Error: number of threads must be between 1 and 100\n");
    return 0;
  }
  else {
    thread_count = threads; /* update the number of threads */
    printf("Initializing %d threads\n", thread_count);
  }

  pthread_t tid[thread_count];
  for ( int i=0; i < thread_count; i++ ) {
    printf("initializing thread %d\n", i);
    pthread_create(&tid[i], NULL, NULL, NULL); 
  }

  network_init( port );                             /* init network module */

  for( ;; ) {                                       /* main loop */
    network_wait();                                 /* wait for clients */

    for( fd = network_open(); fd >= 0; fd = network_open() ) { /* get clients */
      /* Add all of our fd's to a queue */
      /* To use the round robin scheduler */
      if ( scheduler==1 ) {
        short_job_first( fd );
      }
      else if ( scheduler==2 ) {
        round_robin( fd );
      }
      else if ( scheduler==3 ) {
        mlfb( fd );
      }
      else {
        printf("Error: unspecified scheduler\n");
        return 0;
      }
      //serve_client( fd );                           /* process each client */
    }
  }
}
