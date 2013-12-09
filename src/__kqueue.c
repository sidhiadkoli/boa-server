#include "boa.h" 	//it’ll go in boa.h. so doesn’t matter  - ok :)  - :)
#include <time.h>
// kqueue: #include <sys/event.h>
// i've changed defines.h
// also, have changed boa.h

#define MAX_EVENTS 10	//should push it to defines.h. a configurable parameter?  - we arent //sure - let it be


#define KEVENT_ISSET(cfd, ctype,kevents,count) {\
	int i=0;\
	return_val = 0;\
	for (i=0; i<=count; ++i) \
		if(kevents[i].ident == cfd && (kevents[i].flags && ctype)) {\
			return_val = 1; \
			break; \
		} \
}

static void fdset_update(void);
struct kevent event;
struct kevent kevents[MAX_EVENTS];
int return_val;
int nkev;
int kq;

void kqueue_loop(int server_s)
{
	if ((kq = kqueue()) < 0) {
		DIE("kqueue");
	}
	
	EV_SET(&event, server_s, EVFILT_READ, EV_ADD, 0, 0, NULL);
	if (kevent(kq, &event, 1, NULL, 0, NULL) == -1) {
		DIE("? kevent pehla");
	}
	
	while (1) {	//TODO: indent later :P - ok :P
		if (sighup_flag)
			sighup_run();
		if (sigchld_flag)
			sigchld_run();
		if (sigalrm_flag)
			sigalrm_run();
		
		if (sigterm_flag) {
			if (sigterm_flag == 1)
				sigterm_stage1_run(server_s);
			if (sigterm_flag == 2 && !request_ready && !request_block) {
				sigterm_stage2_run();
			}
		}
		
        if (request_block)
		/* move selected req's from request_block to request_ready */
            fdset_update();
		
        /* any blocked req's move from request_ready to request_block */
        process_requests(server_s);
		
		int time_out = (request_ready ? 0 : (ka_timeout ? ka_timeout : REQUEST_TIMEOUT));
		struct timespec t_sec;
		t_sec.tv_sec = time_out;
		//(request_ready || request_block ? &t_sec : NULL)
		
		
		nkev = kevent(kq, NULL, 0, kevents, MAX_EVENTS, NULL);	// some issue with timeout	
		
		WARN("aaaa");
		
		if (nkev < 1)
		{
			if (errno == EINTR)
				continue;   /* while(1) */
			else if (errno != EBADF) {
				DIE("kevent again");
            }
		}
			
		time(&current_time);
			
		KEVENT_ISSET(server_s, EVFILT_READ, kevents, nkev); 	// 
			
		if (return_val) 	 
			pending_requests = 1;
			
	} //while close
		
} //kqueue_loop

	
static void fdset_update(void)
{
	request *current, *next;
	
	for(current = request_block;current;current = next) {
		time_t time_since = current_time - current->time_last;
		next = current->next;
		
		/* hmm, what if we are in "the middle" of a request and not
		 * just waiting for a new one... perhaps check to see if anything
		 * has been read via header position, etc... */
		if (current->kacount < ka_max && /* we *are* in a keepalive */
			(time_since >= ka_timeout) && /* ka timeout */
			!current->logline)  /* haven't read anything yet */
			current->status = DEAD; /* connection keepalive timed out */
		else if (time_since > REQUEST_TIMEOUT) {
			log_error_doc(current);
			fputs("connection timed out\n", stderr);
			current->status = DEAD;
		}
		
		if (current->buffer_end && current->status < DEAD) {
			KEVENT_ISSET(current->fd, EVFILT_WRITE, kevents, nkev);
			if (return_val)
				ready_request(current);
			else {
				KEV_CTL(current->fd, EVFILT_WRITE, EV_ADD);
			}
		} else {
			switch (current->status) {
				case WRITE:
				case PIPE_WRITE:
					KEVENT_ISSET(current->fd, EVFILT_WRITE, kevents, nkev);
					if (return_val)
						ready_request(current);
					else {
						KEV_CTL(current->fd, EVFILT_WRITE, EV_ADD);
					}
					break;
				case BODY_WRITE:
					KEVENT_ISSET(current->post_data_fd, EVFILT_WRITE, kevents, nkev);
					if (return_val)
						ready_request(current);
					else {
						KEV_CTL(current->post_data_fd, EVFILT_WRITE, EV_ADD);
					}
					break;
				case PIPE_READ:
					KEVENT_ISSET(current->data_fd, EVFILT_READ, kevents, nkev);
					if (return_val)
						ready_request(current);
					else {
						KEV_CTL(current->data_fd, EVFILT_READ, EV_ADD);
					}
					break;
				case DONE:
					KEVENT_ISSET(current->fd, EVFILT_WRITE, kevents, nkev);
					if (return_val)
						ready_request(current);
					else {
						KEV_CTL(current->fd, EVFILT_WRITE, EV_ADD);
					}
					break;
				case DEAD:
					ready_request(current);
					break;
				default:
					KEVENT_ISSET(current->fd, EVFILT_READ, kevents, nkev);
					if (return_val)
						ready_request(current);
					else {
						KEV_CTL(current->fd, EVFILT_READ, EV_ADD);
					}
					break;
			}
		}
		current = next;
	}
}
