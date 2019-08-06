// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/** Implementation of the transport server interface.
 *
 *  This interface should be used to set up a server receiving files sent by
 *  processes using the client interface.
 *
 *  An internal metric is available to get statistics about server.
 *  The samping is defined with env. var. TRANSPORT_SERVER_STAT_SAMPLING (seconds) (default=1sec).
 *  And the output file with env. var. TRANSPORT_SERVER_STAT_FILE (if not specified, no stats are recorded).
 *  If set to 'log', output is done in usual log file
 *
 *  Output format:
 *  	time	number of connexions	bytes received	average (kb/s)
 *
 *
 *  Updates:
 * 	   11/2003: added server support for UDP data
 *	04/03/2003: code rewritten - single threaded
 *	19/12/2002: KEEP_ALIVE option on sockets, client name logging when disconnected
 *
 * @file	transport_server.c
 * @see		transport_server.h
 * @author	sylvain.chapeland@cern.ch
*/
#ifdef SUN_OS
#include "sunos.h"
#endif

#include "transport_server.h"
#include "utility.h"
#include "simplelog.h"
#include "transport_files.h"

#include <stdio.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <fcntl.h>
       
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include <pthread.h>


/*
#define TR_SERVER_DEBUG 1
*/


#define TR_SERVER_LOG_HEADER			"Server : "	/** Header to log messages */
#define TR_SERVER_MAX_INCOMING_CONNECTIONS	50		/** listening socket backlog parameter : see `man 2 listen` */
#define TR_SERVER_BUFFER_SIZE 			5000		/** Reception buffer size for a client. */
#define TR_SERVER_QUEUE_LENGTH			1000		/** Number of files stored in/out FIFOs */
#define TR_SERVER_ACK_MAX_FILES			20		/** Maximum number of files received before acknowledging
								    This should never be more than the output FIFO of the client.
								    This will limit the number of files transmitted by second otherwise.
								    An acknowledge is done every second, disregarding number of files received.									    
								*/

/* UDP settings */
#define TR_SERVER_UDP_SOCKETBUFFER	2000000			/* Size of socket buffer for UDP */
#define TR_SERVER_UDP_ACK         	0			/* if set to 0 no acknowledge from server*/
#define TR_SERVER_UDP_MSG_VERSION 	"A0"			/* agent/server protocol version */



/** Definition of server states for each connexion */
#define TR_SERVER_STATE_INIT 			0
#define TR_SERVER_STATE_WAITING_FILE_BEGIN 	1
#define TR_SERVER_STATE_RECEIVING_FILE   	2
#define TR_SERVER_STATE_WAITING_FILE_END	3





/** Structure describing a connection to a remote sample provider */
struct _TR_data_connection {
	int			socket;			/**< The connected socket */
	struct sockaddr_in 	address;		/**< Client address */
	
	int	state;					/**< Reception state */
	
	char	buffer[TR_SERVER_BUFFER_SIZE];		/**< Receive buffer */
	int	buffer_start;				/**< Buffer index in case part of it has already been processed */	

	TR_file *current_file;				/**< The current file being received */
	int 	bytes_received;				/**< Number of bytes currently available for the file being received */
	
	struct _TR_server	*handle;		/**< handle to the server */
	

	int			cx_id;			/**< Connection identifier */
	TR_file_id		ack_file;		/**< file id to acknowledged */
	int			non_acknowledged;	/**< number of files not acknowledged yet*/

	pthread_mutex_t		mutex;			/**< mutex for cx_id, ack_file, and socket variables   */
};





/** Structure containing all server data.
    A handle to a server is a reference to such a structure.
*/
struct _TR_server {
	int                       	listen_sock;		/**< The socket on which the server accepts connections. */	
	int				server_type;		/**< The server type: TR_SERVER_UDP or TR_SERVER_TCP */

	pthread_t			thread;			/**< Handle to the state machine thread */

	struct _TR_data_connection 	*cx_table;		/**< Array of connections */
	int                      	cx_table_size;		/**< Number of connections allowed */
	pthread_mutex_t           	cx_table_mutex;		/**< Lock on the array, when updating the list */

	int				shutdown;		/**< set to 1 to stop state machine */
	pthread_mutex_t           	shutdown_mutex;		/**< lock on shutdown variable */

	int				cx_counter;		/**< A counter to identify connexions */
	
	struct ptFIFO			*output_queue;		/**< The queue of files received - contains TR_file structs* to be freed */
	
	/* statistics */
	char *				stat_file;		/**< file to output statistics. NULL if function disabled */
	int				stat_sample_freq;	/**< rate at which server stats are issued */
	time_t				stat_last_time;		/**< time of last sampling */
	int 				stat_bytes_received;	/**< number of bytes received */
	int				stat_files_received;	/**< number of files received */
	int				stat_connected_clients;	/**< number of connected clients */
};






/* close a given connexion */
void TR_server_connection_close(struct _TR_data_connection* cx){

	/* mutex to ensure nobody is acknowledging on this socket in another thread */

	pthread_mutex_lock(&cx->mutex);

	close(cx->socket);
	cx->socket=-1;
	cx->cx_id=-1;

	#ifdef TR_SERVER_DEBUG
	slog(SLOG_INFO,TR_SERVER_LOG_HEADER "last ack file %d %d",cx->ack_file.minId,cx->ack_file.majId);
	#endif

	pthread_mutex_unlock(&cx->mutex);


	/* delete file being received if any */
	TR_file_destroy(cx->current_file);
	cx->current_file=NULL;
	
	slog(SLOG_INFO,TR_SERVER_LOG_HEADER "%s disconnected",inet_ntoa(cx->address.sin_addr));
	
	return;
}




/* read data from connection
   returns -1 on error (FIFO full timeout)
*/
int TR_server_connection_read(struct _TR_data_connection* cx){

	char buffer_tmp[TR_SERVER_BUFFER_SIZE];
	char * end_buffer=NULL;
	char * end_line=NULL;
	char * parse_ptr=NULL; 
	int min_id,maj_id,size;		
	
	int result,i;


	#ifdef TR_SERVER_DEBUG
		slog(SLOG_INFO,"Reception in progress");
	#endif
	
	if (cx->state==TR_SERVER_STATE_RECEIVING_FILE) {
	
		/* read socket directly to file structure to avoid later copy */		
		result=read(cx->socket, &((char *)cx->current_file->first->value)[cx->bytes_received], cx->current_file->first->size - cx->bytes_received);

		/* on success, commit read and return */
		if (result>0) {
			cx->bytes_received+=result;

			/* file reception completed? */
			if (cx->bytes_received==cx->current_file->first->size) {
				cx->state=TR_SERVER_STATE_WAITING_FILE_END;
			}

			return 0;
		}
		
	} else {
		
		/* read socket to a temporary buffer (appended if containing already data) */
		result=read(cx->socket,&cx->buffer[cx->buffer_start],TR_SERVER_BUFFER_SIZE-cx->buffer_start-1);

		/* NULL terminated buffer easier to parse */
		if (result>0) {
			end_buffer=&cx->buffer[cx->buffer_start+result];
			end_buffer[0]=0;
			
			parse_ptr=cx->buffer;

			cx->buffer_start+=result;
			
			#ifdef TR_SERVER_DEBUG
			slog(SLOG_INFO,"Buffer: %s",cx->buffer);
			#endif
		}
	}

	
	
	/* error reading socket ? */
	if (result<0) {
		slog(SLOG_ERROR,TR_SERVER_LOG_HEADER "read - error %d",errno);
		result=0;
	}

	/* close connection on EOF / error */
	if (result==0) {
		TR_server_connection_close(cx);
		return 0;
	}
	


	
	/* Now parse buffer content - can be several lines */
	/* Check connection still valid (can be closed during parsing) */
	for (;cx->socket!=-1;) {

		/* does the buffer contain a full line? */
		end_line=strchr(parse_ptr,'\n');
		if (end_line==NULL) {
			/* no new line found in remaining buffer to parse */
			
			if (parse_ptr!=cx->buffer) {
				/* remaining bytes are not at the beginning of the buffer*/
				
				if (parse_ptr<end_buffer) {
					/* copy unused bytes to the beginning of the buffer */
					memmove(cx->buffer,parse_ptr,end_buffer-parse_ptr);
					cx->buffer_start=end_buffer-parse_ptr;
				} else {
					/* we parsed everything */
					cx->buffer_start=0;
				}

			} else {
				/* line incomplete yet : just wait - but check we have some space left */				
			
				if (cx->buffer_start>=TR_SERVER_BUFFER_SIZE-1) {
					/* buffer too small... this should not happen. Close connexion */
					slog(SLOG_ERROR,TR_SERVER_LOG_HEADER "Buffer too small - dropping %s",cx->buffer);
					TR_server_connection_close(cx);
				}
			}
					
			/* no need to continue parsing */
			break;
			
		} else {

			/* NULL terminated line */
			end_line[0]=0;
			end_line++;

			switch (cx->state) {
			
				case TR_SERVER_STATE_WAITING_FILE_BEGIN:
				
					min_id=-1;
					maj_id=-1;
					size=0;
					if ( (4!=sscanf(parse_ptr,"File %s %d %d %d", buffer_tmp, &min_id, &maj_id, &size)) || (size<=0) ) {
						slog(SLOG_ERROR,TR_SERVER_LOG_HEADER "Can't parse %s",parse_ptr);
						TR_server_connection_close(cx);
					} else {

						#ifdef TR_SERVER_DEBUG
						slog(SLOG_INFO,"Client %03d : receiving file	%d %d (%d bytes)",cx->cx_id,min_id,maj_id,size);
						#endif

						cx->current_file=TR_file_new();
						cx->current_file->id.majId=maj_id;
						cx->current_file->id.minId=min_id;
						cx->current_file->id.source=checked_strdup(buffer_tmp);
						cx->current_file->id.sender=cx->cx_id;
						cx->current_file->id.sender_magic=(void *)cx;
						cx->current_file->first=checked_malloc(sizeof(struct _TR_blob));
						cx->current_file->last=cx->current_file->first;
						cx->current_file->size=size;

						cx->current_file->first->size=size;
						cx->current_file->first->next=NULL;
						cx->current_file->first->value=checked_malloc(size+1);
						((char *)cx->current_file->first->value)[size]=0;	/* NULL terminated value will help parsing */

						cx->state=TR_SERVER_STATE_RECEIVING_FILE;

						/* copy the remaining of the buffer (if any) to the right place */
						size=end_buffer-end_line;
						if (size>0) {
							/* limit to the file size */
							if (size>=cx->current_file->size) {
								size=cx->current_file->size;
								/* in this case all data is already available */
								cx->state=TR_SERVER_STATE_WAITING_FILE_END;
							}
							
							/* the beginning of the file has already been received */
							memcpy(cx->current_file->first->value,end_line,size);
							cx->bytes_received=size;

							/* advance the end of line marker */
							end_line+=size;							

						} else {
							/* there is no file data in the buffer yet */
							cx->bytes_received=0;
						}
					}

					break;



				case TR_SERVER_STATE_WAITING_FILE_END:

					if (strncmp("END",parse_ptr,3)) {
						slog(SLOG_ERROR,TR_SERVER_LOG_HEADER "Expected end of file, not %s",parse_ptr);
						TR_server_connection_close(cx);
					} else {
						/* do what we want from the file */
						#ifdef TR_SERVER_DEBUG
						slog(SLOG_INFO,"File received : %s",cx->current_file->first->value);
						#endif
											
						/* try to push to output buffer */
						for (i=0;;i++) {
							#ifdef TR_SERVER_DEBUG
							slog(SLOG_INFO,"Try to write FIFO");
							#endif

							/* update statistics */
							cx->handle->stat_bytes_received+=cx->current_file->size;
							cx->handle->stat_files_received++;
							
							result=ptFIFO_write(cx->handle->output_queue,cx->current_file,1);
							
							if (!result) {
								if (i>0) {
									slog(SLOG_INFO,TR_SERVER_LOG_HEADER "output FIFO available again");
								}
								break;
							}										
							
							if (i==0) {
								slog(SLOG_WARNING,TR_SERVER_LOG_HEADER "output FIFO full");
							}
							
							if (i==10) {
								/* long timeout : there must be shutdown pending.
								Anyway close connexion and notice master state machine */
								slog(SLOG_WARNING,"Timeout on output FIFO");
								TR_server_connection_close(cx);
								return -1;
							}
						}

						#ifdef TR_SERVER_DEBUG
						slog(SLOG_INFO,"File pushed in FIFO");
						#endif
											
						cx->current_file=NULL;

						/* we can wait another now */
						cx->state=TR_SERVER_STATE_WAITING_FILE_BEGIN;
					}

					break;
					
					
					
				case TR_SERVER_STATE_INIT:

					/* here we should parse 'INI node ...' command */

					snprintf(buffer_tmp,TR_SERVER_BUFFER_SIZE,"READY\n");
					result=send(cx->socket,buffer_tmp,6, MSG_DONTWAIT);

					if (result!=6) {
						/* don't waste time on this socket */
						slog(SLOG_ERROR,TR_SERVER_LOG_HEADER "send failed");
						TR_server_connection_close(cx);

					} else {
						/* now file reception mode */
						cx->state=TR_SERVER_STATE_WAITING_FILE_BEGIN;
					}
					
					break;
				
				
					
				default:
					break;
			}
			
			/* advance the pointer to the string to parse */
			parse_ptr=end_line;
			
		}
	}

	return 0;
}







/* acknowledge files for a given connection */
/* this function is thread-safe */

void TR_server_acknowledge_files(struct _TR_data_connection *cx, int min_number){
	char buffer_ack[TR_SERVER_BUFFER_SIZE];
	
	pthread_mutex_lock(&cx->mutex);

	if (cx->socket!=-1) {

		if (cx->non_acknowledged > min_number) {
			
			#ifdef TR_SERVER_DEBUG
			slog(SLOG_INFO,"Send back ack %d %d (%d)",
				cx->ack_file.minId,
				cx->ack_file.majId,
				cx->ack_file.sender);
			#endif
			
			snprintf(buffer_ack,TR_SERVER_BUFFER_SIZE,"ACK %d %d\n",
					cx->ack_file.minId,
					cx->ack_file.majId);

			if (send(cx->socket,buffer_ack,strlen(buffer_ack), MSG_DONTWAIT)!=-1) {
				cx->non_acknowledged=0;
			}	
		}
	}
				
	pthread_mutex_unlock(&cx->mutex);
}






/* output file statistics */
void TR_server_print_stats(TR_server_handle h, time_t new_time){

	FILE 	*fp;	/**< A file pointer for the stat file */
	long the_mem_alloc;	
	
	if (h->stat_file!=NULL) {
		if (new_time-h->stat_last_time>=h->stat_sample_freq) {
			the_mem_alloc=checked_memstat();

			if (strcmp("log",h->stat_file)) {
				fp=fopen(h->stat_file,"a");
				if (fp!=NULL) {
					fprintf(fp,"%d\t%d\t%d\t%d\t%.2f\t%ld\t%d\n",
						(int)new_time,
						h->stat_connected_clients,
						h->stat_files_received,
						h->stat_bytes_received,
						h->stat_bytes_received*1.0/h->stat_sample_freq,
						the_mem_alloc,
						(int)(100*ptFIFO_space_used(h->output_queue))
					);
					fclose(fp);
				}
			} else {
				/*slog(SLOG_INFO,"Fmon server statistics: %d\t%d\t%d\t%.2f\t%ld\t%d",
						h->stat_connected_clients,
						h->stat_files_received,
						h->stat_bytes_received,
						h->stat_bytes_received*1.0/h->stat_sample_freq,
						the_mem_alloc,
						(int)(100*ptFIFO_space_used(h->output_queue))
				);
                                */
			}
			h->stat_bytes_received=0;
			h->stat_files_received=0;								
			h->stat_last_time=new_time;
		}
	}
}






/* state machine running in a separate thread */
void *TR_server_state_machine(void *arg){
	struct _TR_server *h;		/**< Server handle */

	fd_set select_read;		/**< List of sockets to select */
	int highest_sock;	
	struct timeval tv;
	int result;
	
	int new_cl_sock;			/**< socket address for new client */
	struct sockaddr_in new_cl_addr;		/**< address of new client */
	socklen_t cl_addr_len;			/**< address length */

	int i;
	
	int abort_loop;				/**< flag in case we want to abort current processes to check shutdown status */

	time_t	the_time,new_time;				/**< A counter to measure time from loop to loop */
	
	h=(struct _TR_server *)arg;
	the_time=0;
	

	for(;;) {
		/*
		printf(".\n");
		fflush(stdout);
		*/

		abort_loop=0;
		

		/* create a 'select' list */
		FD_ZERO(&select_read);
		
		/* first socket is the listening socket for new connections */
		FD_SET(h->listen_sock,&select_read);
		highest_sock=h->listen_sock;

		/* add the connected clients */
		for(i=0;i<h->cx_table_size;i++) {
			if (h->cx_table[i].socket!=-1) {
				FD_SET(h->cx_table[i].socket,&select_read);
				if (h->cx_table[i].socket > highest_sock) {
					highest_sock=h->cx_table[i].socket;
				}
			}
		}

				
		/* timeout after a while to allow for server shutdown if needed */
	        tv.tv_sec = 1;
        	tv.tv_usec = 0;

		/* wait events (read/errors) */
		result=select(highest_sock+1,&select_read, NULL, NULL, &tv);

		if (result<0) {

			/* an error occurred */
			slog(SLOG_ERROR,TR_SERVER_LOG_HEADER "select - error %d",errno);


		} else if (result>0) {

			/* some sockets are ready */
	

			/* read from clients */	
			for(i=0;i<h->cx_table_size;i++) {
				if (h->cx_table[i].socket!=-1) {
					if (FD_ISSET(h->cx_table[i].socket,&select_read)) {
						
						if (TR_server_connection_read(&h->cx_table[i])!=0) {
							/* abort if FIFO full to check if shutdown pending */
							abort_loop=1;
							break;
						}
						
					}
				}
			}

			
			
			/* update client list : accept new connections */
			if ( (!abort_loop) && (FD_ISSET(h->listen_sock,&select_read)) ) {

				cl_addr_len=sizeof(new_cl_addr);
				new_cl_sock = accept(h->listen_sock, (struct sockaddr *) &new_cl_addr, &cl_addr_len);

				if (new_cl_sock < 0) {
					slog(SLOG_ERROR,TR_SERVER_LOG_HEADER "accept - %d",errno);
				} else {
					slog(SLOG_INFO,TR_SERVER_LOG_HEADER "%s connected on port %d",inet_ntoa(new_cl_addr.sin_addr),new_cl_addr.sin_port);

					for(i=0;i<h->cx_table_size;i++) {
						if (h->cx_table[i].socket==-1) {
							
							pthread_mutex_lock(&h->cx_table[i].mutex);

							h->cx_table[i].socket=new_cl_sock;
							h->cx_table[i].address=new_cl_addr;

							h->cx_table[i].state=TR_SERVER_STATE_INIT;
							h->cx_table[i].buffer_start=0;							

							h->cx_table[i].current_file=NULL;

							h->cx_table[i].cx_id=h->cx_counter++;
							h->cx_table[i].ack_file.minId=0;
							h->cx_table[i].ack_file.majId=0;
							h->cx_table[i].non_acknowledged=0;
							
							pthread_mutex_unlock(&h->cx_table[i].mutex);

							break;
						}
					}

					/* check if a free slot was found */
					if (i==h->cx_table_size) {
						close(new_cl_sock);
						slog(SLOG_WARNING,TR_SERVER_LOG_HEADER "no more clients allowed - disconnecting");
					}
				}
			}
			
						
		}


		/* To do once a second at most */
		time(&new_time);
		if (the_time!=new_time){
			the_time=new_time;


			/* acknowledge file received if needed - no minimum number of files*/
			h->stat_connected_clients=0;
			for(i=0;i<h->cx_table_size;i++) {
				if (h->cx_table[i].socket!=-1) {
					h->stat_connected_clients++;
					TR_server_acknowledge_files(&h->cx_table[i],0);
				}
			}


			/* issue server statistics when required */
			TR_server_print_stats(h, new_time);
		}				

		/* shutdown requested? */
		pthread_mutex_lock(&h->shutdown_mutex);
		if (h->shutdown) {
			pthread_mutex_unlock(&h->shutdown_mutex);
			break;
		}
		pthread_mutex_unlock(&h->shutdown_mutex);		
	}

	return NULL;
}











/* state machine running in a separate thread */
void *TR_UDP_server_state_machine(void *arg){
	struct _TR_server *h;		/**< Server handle */

	fd_set select_read;		/**< List of sockets to select */
	int highest_sock;	
	struct timeval tv;
	int result;
	
	//int abort_loop;				/**< flag in case we want to abort current processes to check shutdown status */
	time_t	the_time,new_time;				/**< A counter to measure time from loop to loop */
	int bytes_rcv;
	char buf[TR_SERVER_BUFFER_SIZE];	
	int fifo_full;

	struct sockaddr_in si_other;
	socklen_t slen=sizeof(si_other);
	
	TR_file *new_file;
	
	int min_id,maj_id;

	h=(struct _TR_server *)arg;
	the_time=0;
	
	min_id=1;
	maj_id=1;
	fifo_full=0;
	
	for(;;) {
		//abort_loop=0;	

		/* create a 'select' list */
		FD_ZERO(&select_read);
		
		/* first socket is the listening socket for new connections */
		FD_SET(h->listen_sock,&select_read);
		highest_sock=h->listen_sock;
			
		/* timeout after a while to allow for server shutdown if needed */
	        tv.tv_sec = 1;
        	tv.tv_usec = 0;

		/* wait events (read/errors) */
		result=select(highest_sock+1,&select_read, NULL, NULL, &tv);

		/* get time */
		time(&new_time);
		
		/* check select result */
		if (result<0) {

			/* an error occurred */
			slog(SLOG_ERROR,TR_SERVER_LOG_HEADER "select - error %d",errno);


		} else if (result>0) {

			/* socket ready */		
			bytes_rcv=recvfrom(h->listen_sock, buf, TR_SERVER_BUFFER_SIZE-1, 0, (struct sockaddr *)&si_other, &slen);

			for(;;){	

				if (bytes_rcv==-1) {
					slog(SLOG_ERROR,"recvfrom - error %d",errno);
					break;
				}

				/* compare with buffer size */
				if (bytes_rcv >= TR_SERVER_BUFFER_SIZE * 0.9) {
					slog(SLOG_WARNING,"UDP server - buffer saturation warning: %d bytes read",bytes_rcv);
				}
				
				/* Update counters */
				h->stat_files_received++;
				h->stat_bytes_received+=bytes_rcv;
				
				/* add 0 at the end of the packet for string compatibility */
				buf[bytes_rcv]=0;

				/* create 'file' structure */
				new_file=TR_file_new();
				new_file->id.majId=maj_id;
				new_file->id.minId=min_id++;
				new_file->id.source=checked_strdup(inet_ntoa(si_other.sin_addr));
				new_file->id.sender=0;
				new_file->id.sender_magic=NULL;

				new_file->first=checked_malloc(sizeof(struct _TR_blob));
				new_file->last=new_file->first;
				new_file->size=bytes_rcv;

				new_file->first->size=bytes_rcv;
				new_file->first->next=NULL;
				new_file->first->value=checked_strdup(buf);

				/* insert in output queue */
				if (ptFIFO_write(h->output_queue,new_file,0)==-1) {
					if (fifo_full==0) {
						slog(SLOG_WARNING,"UDP server: output FIFO full");
						fifo_full=1;
					}					
					TR_file_destroy(new_file);
				} else {
					if (fifo_full) {
						slog(SLOG_INFO,"UDP server: output FIFO now available");
						fifo_full=0;
					}
				}

				break;
			}
		}

		/* To do once a second at most */
		if (the_time!=new_time){
			the_time=new_time;

			/* update file maj id */
			maj_id++;
			min_id=1;

			/* issue server statistics when required */
			TR_server_print_stats(h, new_time);
		}				

		/* shutdown requested? */
		pthread_mutex_lock(&h->shutdown_mutex);
		if (h->shutdown) {
			pthread_mutex_unlock(&h->shutdown_mutex);
			break;
		}
		pthread_mutex_unlock(&h->shutdown_mutex);
	}

	return NULL;
}












/** Start a server with a given configuration.
  * @param config : server configuration.
  * @return	: a handle to the server connection.
*/
TR_server_handle TR_server_start(TR_server_configuration* config){

	int listen_sock=-1;
	int opts;
	struct sockaddr_in srv_addr;
	TR_server_handle h;
	int i;
	char *sample_freq;

	/* UDP vars */
	unsigned long ulSocketBufferSize;
	socklen_t opt_size;
	

	if ((config->server_type!=TR_SERVER_UDP)&&(config->server_type!=TR_SERVER_TCP)) {
		slog(SLOG_ERROR,"Bad server type");
		return NULL;
	}



	if (config->server_type==TR_SERVER_UDP) {
		/* UDP server */
		
		slog(SLOG_INFO,TR_SERVER_LOG_HEADER "starting on UDP port %d",config->server_port);

		/* create socket */
		if ((listen_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
			slog(SLOG_ERROR,TR_SERVER_LOG_HEADER "socket - %d",errno);
			return NULL;
		}
		
		/* set socket buffer size */
		opt_size=sizeof(ulSocketBufferSize);
		ulSocketBufferSize=TR_SERVER_UDP_SOCKETBUFFER;
		setsockopt(listen_sock, SOL_SOCKET, SO_RCVBUF, (void*)&ulSocketBufferSize,opt_size);
		if (getsockopt(listen_sock, SOL_SOCKET, SO_RCVBUF, (void*)&ulSocketBufferSize, &opt_size)==0) {
			slog(SLOG_INFO,TR_SERVER_LOG_HEADER "UDP receive buffer size = %ld\n",ulSocketBufferSize/2);
		}

		/* make sure re-bind possible without TIME_WAIT problems */
		opts=1;
		setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts));	

		/* define server address / port */
		bzero((char *) &srv_addr, sizeof(srv_addr));
		srv_addr.sin_family = AF_INET;
		srv_addr.sin_addr.s_addr = INADDR_ANY;
		srv_addr.sin_port=htons(config->server_port);

		/* bind socket */
		if (bind(listen_sock, (struct sockaddr *) &srv_addr, sizeof(srv_addr)) < 0) {
			slog(SLOG_ERROR,TR_SERVER_LOG_HEADER "bind - %d",errno);
			close(listen_sock);
			return NULL;
		}
	}
	
	else if (config->server_type==TR_SERVER_TCP) {
		/* TCP server */

		slog(SLOG_INFO,TR_SERVER_LOG_HEADER "starting on TCP port %d",config->server_port);

		/* initialize receiving socket */

		/* create a socket */
		if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			slog(SLOG_ERROR,TR_SERVER_LOG_HEADER "socket - %d",errno);
			return NULL;
		}

		/* make sure re-bind possible without TIME_WAIT problems */
		opts=1;
		setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts));	

		/* use keep alive messages */
		opts=1;
		setsockopt(listen_sock, SOL_SOCKET, SO_KEEPALIVE, &opts, sizeof(opts));	

		/* define server address / port */
		bzero((char *) &srv_addr, sizeof(srv_addr));
		srv_addr.sin_family = AF_INET;
		srv_addr.sin_addr.s_addr = INADDR_ANY;
		srv_addr.sin_port=htons(config->server_port);

		/* bind socket */
		if (bind(listen_sock, (struct sockaddr *) &srv_addr, sizeof(srv_addr)) < 0) {
			slog(SLOG_ERROR,TR_SERVER_LOG_HEADER "bind - %d",errno);
			close(listen_sock);
			return NULL;
		}

		/* queue length for incoming connections */
		if (listen(listen_sock, TR_SERVER_MAX_INCOMING_CONNECTIONS) < 0) {
			slog(SLOG_ERROR,TR_SERVER_LOG_HEADER "listen - %d",errno);
			close(listen_sock);
			return NULL;
		}
	}



	/* now create the handle */
	h=checked_malloc(sizeof(struct _TR_server));

	/* listening socket */
	h->listen_sock=listen_sock;

	/* server type */
	h->server_type=config->server_type;

	/* table init */
	h->cx_table_size=config->max_clients;
	pthread_mutex_init(&h->cx_table_mutex,NULL);	
	h->cx_table=checked_malloc(sizeof(struct _TR_data_connection)*config->max_clients);
	
	for(i=0;i<h->cx_table_size;i++) {
		h->cx_table[i].socket=-1;
		h->cx_table[i].current_file=NULL;
		h->cx_table[i].handle=h;
		h->cx_table[i].cx_id=-1;
		pthread_mutex_init(&h->cx_table[i].mutex,NULL);
	}
				
	/* shutdown variable */
	h->shutdown=0;
	pthread_mutex_init(&h->shutdown_mutex,NULL);	
	
	/* connexion counter */
	h->cx_counter=0;
		
	/* file queue */
	if (config->queue_length==0) {
		h->output_queue=ptFIFO_new(TR_SERVER_QUEUE_LENGTH);
	} else {
		h->output_queue=ptFIFO_new(config->queue_length);
	}
	

	/* statistics setup */
	h->stat_file=checked_strdup(getenv("TRANSPORT_SERVER_STAT_FILE"));
	h->stat_last_time=0;
	h->stat_sample_freq=1;	
	h->stat_files_received=0;
	h->stat_bytes_received=0;
	h->stat_connected_clients=0;
	sample_freq=getenv("TRANSPORT_SERVER_STAT_SAMPLING");
	if (sample_freq) {
		sscanf(sample_freq,"%d",&h->stat_sample_freq);
	}
	
	/* launch the thread for server */	
	if (config->server_type==TR_SERVER_UDP) {
		/* UDP server */
		pthread_create(&h->thread,NULL,TR_UDP_server_state_machine,(void *) h);
	}
	
	else if (config->server_type==TR_SERVER_TCP) {
		/* TCP server */
		pthread_create(&h->thread,NULL,TR_server_state_machine,(void *) h);
	}

	return h;
}





/** Stop the server
  * @param h : server handle.  
*/
int TR_server_stop(TR_server_handle h){
	int 		i;
	TR_file 	*current_file;
	
	
	if (h==NULL) return -1;
	
	/* request shutdown */
	pthread_mutex_lock(&h->shutdown_mutex);
	h->shutdown=1;
	pthread_mutex_unlock(&h->shutdown_mutex);
	
	/* wait */
	slog(SLOG_INFO,TR_SERVER_LOG_HEADER "waiting state machine to stop");
	pthread_join(h->thread,NULL);

	/* close listening connection */
	close(h->listen_sock);
	
	/* close clients */
	for(i=0;i<h->cx_table_size;i++) {
		if (h->cx_table[i].socket!=-1) {
			TR_server_connection_close(&h->cx_table[i]);
			pthread_mutex_destroy(&h->cx_table[i].mutex);
		}
	}


	/* purge output queue */
	for(;;) {
		current_file=ptFIFO_read(h->output_queue,0); /* no timeout */
		if (current_file==NULL) break;
		TR_file_dec_usage(current_file);
	}
	ptFIFO_destroy(h->output_queue);


	/* free memory */
	pthread_mutex_destroy(&h->shutdown_mutex);
	checked_free(h->cx_table);
	checked_free(h->stat_file);
	checked_free(h);


	slog(SLOG_INFO,TR_SERVER_LOG_HEADER "stopped");	

	return 0;
}








/* this function is thread-safe */
int TR_server_ack_file(TR_server_handle h,TR_file_id *id) {
	struct _TR_data_connection* cx;

	/* Nothing to do with UDP protocol */
	if (h->server_type==TR_SERVER_UDP) {
		return 0;
	}
	
	/* TCP protocol : acknowledge */
	if (id!=NULL) {
		cx=(struct _TR_data_connection*)id->sender_magic;
		if (cx!=NULL) {
			pthread_mutex_lock(&cx->mutex);
			if (cx->cx_id == id->sender) {
				/*
				slog(SLOG_INFO,"ack %d %d (%d)",id->minId, id->majId, id->sender);
				*/
				if (TR_file_id_compare(*id,cx->ack_file)) {
					cx->ack_file=*id;
					cx->non_acknowledged++;
				} else {
					slog(SLOG_WARNING,"bad order for ack %d %d (%d)",id->minId, id->majId, id->sender);
				}
			}
			pthread_mutex_unlock(&cx->mutex);
			
			TR_server_acknowledge_files(cx,TR_SERVER_ACK_MAX_FILES);
		}
	}

	return 0;
}



/* Get a file from queue */
TR_file *TR_server_get_file(TR_server_handle h, int timeout) {
	TR_file* f;
	
	f=(TR_file*)ptFIFO_read(h->output_queue, timeout);
	
	return f;
}



// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include <thread>
#include <memory>
#include <atomic>
#include <string>
#include <Common/Timer.h>

#include <Common/DataBlock.h>
#include <Common/DataBlockContainer.h>
#include <Common/DataSet.h>
#include <Common/Fifo.h>

// class to send data blocks remotely over a TCP/IP socket
class SocketTx {
  public:

  // constructor
  // name: name given to this client, for logging purpose
  // serverHost: IP of remote server to connect to
  // serverPort: port number of remote server to connect to
  SocketTx(std::string name, std::string serverHost, int serverPort);
  
  // destructor
  ~SocketTx();

  // push a new piece of data to output socket
  // returns 0 on success, or -1 on error (e.g. when already busy sending something)
  int pushData(DataBlockContainerReference &b);

  private:
  std::unique_ptr<std::thread> th;	// thread pushing data to socket
  std::atomic<int> shutdownRequest;	// flag to be set to 1 to stop thread

  // todo: create an input queue, instead of single block
  //std::unique_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>> blocks;

  unsigned long long bytesTx=0;	// number of bytes sent
  AliceO2::Common::Timer t; // timer, to count active time
  std::string clientName; // name of client, as given to constructor

  std::string serverHost; // remote server IP
  int serverPort; // remote server port
  
  private:  
  std::atomic<int> isSending; // if set, thread busy sending. if not set, new block can be pushed
  DataBlockContainerReference currentBlock=nullptr; // current data chunk being sent
  size_t currentBlockIndex=0; // number of bytes of chunk already sent
  
  void run(); // code executed in separate thread
};
