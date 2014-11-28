#include <sys/types.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <iostream>
#include <vector>
#include <thread>
#include "threadpool.h"
#include "message.h"
#include <semaphore.h>
#include <mutex>
#include <condition_variable>

#define BUFSIZE 4096
#define DEFAULT_NUM_THREADS 30
#define IN_SOCKET_TYPE 2
#define OUT_SOCKET_TYPE 1
#define HEADER_LEN 40
#define NUM_PROTOCOL_PIPES 18
#define NUM_PROTOCOL_PIPES_FDS 36
#define NUM_MESSAGES 2
using namespace std;

mutex application_dns_mutex;
condition_variable application_dns_cv;
mutex application_rdp_mutex;
condition_variable application_rdp_cv;
mutex application_ftp_mutex;
condition_variable application_ftp_cv;
mutex application_telnet_mutex;
condition_variable application_telnet_cv;
mutex dns_rdp_send_mutex;
condition_variable dns_rdp_send_cv;
mutex udp_tcp_send_mutex;
condition_variable udp_tcp_send_cv;
mutex ip_send_mutex;
condition_variable ip_send_cv;
mutex ethernet_send_mutex;
condition_variable ethernet_send_cv;
mutex ftp_telnet_send_mutex;
condition_variable ftp_telnet_send_cv;

/* Pipearray designations:
	0, 1 (ethernet recieve pipe read,write)
	2, 3 (ethernet send pipe read, write)
	4, 5 (ip receive pipe read, write)
	6, 7 (ip send pipe read, write)
	8, 9 (udp receive pipe read, write)
	10, 11 (tcp receive pipe read, write)
	12, 13 (udp/tcp send pipe read, write)
	14, 15 (ftp receive pipe read, write)
	16, 17 (telnet receive pipe read, write)
	18, 19 (telnet/ftp send pipe read, write)
	20, 21 (rdp receive pipe read, write)
	22, 23 (dns receive pipe read, write)
	24, 25 (rdp/dns send pipe read, write)
	26, 27 (application_message_ftp send pipe read, write)
	28, 29 (application_message_telnet send pipe read, write)
	30, 31 (application_message_rdp send pipe read, write)
	32, 33 (application_message_dns send pipe read, write)
	34, 35 (socket send pipe read, write)
*/

struct header {
	int hlp;
	vector<char> other_info;
	int message_len;
};

char *append_header_to_message(int higher_protocol_id, int other_info, char *buffer, int bytes_read) {
	char *send_buffer = new char[BUFSIZE];
	header *message_header = new header;
	message_header->hlp = higher_protocol_id;
	message_header->other_info.resize(other_info);
	message_header->message_len = bytes_read;
	Message *m = new Message(buffer, bytes_read);
	m->msgAddHdr((char*)message_header, sizeof(header));
	m->msgFlat(send_buffer);
	return send_buffer;
}

void application_to_dns(int *pipearray) {
	char *message = new char[BUFSIZE];
	memcpy(message, "1", 1);
	for(int i = 0;i < NUM_MESSAGES;i++) {
		application_dns_mutex.lock();
		write(pipearray[33], message, 1);
		application_dns_mutex.unlock();
		application_dns_cv.notify_one();
		usleep(5000);// 5 milliseconds
	}
}

void application_to_rdp(int *pipearray) {
	char *message = new char[BUFSIZE];
	memcpy(message, "2", 1);
	for(int i = 0;i < NUM_MESSAGES;i++) {
		application_rdp_mutex.lock();
		write(pipearray[31], message, 1);
		application_rdp_mutex.unlock();
		application_rdp_cv.notify_one();
		usleep(5000);// 5 milliseconds
	}

}

void application_to_telnet(int *pipearray) {
	char *message = new char[BUFSIZE];
	memcpy(message, "3", 1);
	for(int i = 0;i < NUM_MESSAGES;i++) {
		application_telnet_mutex.lock();
		write(pipearray[29], message, 1);
		application_telnet_mutex.unlock();
		application_telnet_cv.notify_one();
		usleep(5000);// 5 milliseconds
	}

}

void application_to_ftp(int *pipearray) {
	char *message = new char[BUFSIZE];
	memcpy(message, "4", 1);
	for(int i = 0;i < NUM_MESSAGES;i++) {
		application_ftp_mutex.lock();
		write(pipearray[27], message, 1);
		application_ftp_mutex.unlock();
		application_ftp_cv.notify_one();
		usleep(5000);// 5 milliseconds
	}
}

void dns_send_pipe(int *pipearray) {
	int bytes_read;
	char *read_buffer = new char[BUFSIZE];
	char *temp_buffer;
	char *send_buffer = new char[BUFSIZE];
	while(1) {
		bzero(read_buffer, BUFSIZE);
		unique_lock<mutex> lk(application_dns_mutex);
		application_dns_cv.wait(lk);
		bytes_read = read(pipearray[32], read_buffer, BUFSIZE);
		lk.unlock();
		if(bytes_read == -1) {
			fprintf(stderr, "error reading from application_dns send pipe: %s\n", strerror(errno));
			exit(1);
		}
		cout << read_buffer << endl;
		temp_buffer = append_header_to_message(0, 8, read_buffer, bytes_read);
		memcpy(send_buffer, "8", 1);
		memcpy(send_buffer + 1, temp_buffer, (HEADER_LEN + bytes_read));
		dns_rdp_send_mutex.lock();
		if(write(pipearray[25], send_buffer, (bytes_read + HEADER_LEN + 1)) == -1) {
			fprintf(stderr, "error writing to rdp/dns send pipe: %s\n", strerror(errno));
		}
		dns_rdp_send_mutex.unlock();
		dns_rdp_send_cv.notify_one();
	}
}

void dns_receive_pipe(int *pipearray) {
	char *buffer = new char[BUFSIZE];
	char *write_buffer = new char[BUFSIZE];
	while(1) {
		bzero(buffer, BUFSIZE);
		if(read(pipearray[22], buffer, BUFSIZE) == -1) {
			fprintf(stderr, "error reading from dns receive pipe: %s\n", strerror(errno));
		}
		Message *m = new Message(buffer, BUFSIZE);
		header *temp_header = (header *)m->msgStripHdr(HEADER_LEN);
		//cout << "dns receive hlp: " << temp_header->hlp << endl;
		//cout << "dns receive oi: " << temp_header->other_info.size() << endl;
		//cout << "dns receive message_len: " << temp_header->message_len << endl;
		m->msgFlat(write_buffer);
		//cout << "dns receive message: " << write_buffer << endl;
		cout << write_buffer << endl;
	}
}

void rdp_send_pipe(int *pipearray) {
	int bytes_read;
	char *read_buffer = new char[BUFSIZE];
	char *temp_buffer;
	char *send_buffer = new char[BUFSIZE];
	while(1) {
		bzero(read_buffer, BUFSIZE);
		unique_lock<mutex> lk(application_rdp_mutex);
		application_rdp_cv.wait(lk);
		bytes_read = read(pipearray[30], read_buffer, BUFSIZE);
		lk.unlock();
		if(bytes_read == -1) {
			fprintf(stderr, "error reading from application_rdp send pipe: %s\n", strerror(errno));
			exit(1);
		}
		//cout << "rdp send message: " << read_buffer << endl;
		cout << read_buffer << endl;
		temp_buffer = append_header_to_message(0, 12, read_buffer, bytes_read);
		memcpy(send_buffer, "7", 1);
		memcpy(send_buffer + 1, temp_buffer, (HEADER_LEN + bytes_read));
		dns_rdp_send_mutex.lock();
		if(write(pipearray[25], send_buffer, (bytes_read + HEADER_LEN + 1)) == -1) {
			fprintf(stderr, "error writing to rdp/dns send pipe: %s\n", strerror(errno));
		}
		dns_rdp_send_mutex.unlock();
		dns_rdp_send_cv.notify_one();
	}
}

void rdp_receive_pipe(int *pipearray) {
	char *buffer = new char[BUFSIZE];
	char *write_buffer = new char[BUFSIZE];
	while(1) {
		bzero(buffer, BUFSIZE);
		if(read(pipearray[20], buffer, BUFSIZE) == -1) {
			fprintf(stderr, "error reading from rdp receive pipe: %s\n", strerror(errno));
		}
		Message *m = new Message(buffer, BUFSIZE);
		header *temp_header = (header *)m->msgStripHdr(HEADER_LEN);
		//cout << "rdp receive hlp: " << temp_header->hlp << endl;
		//cout << "rdp receive oi: " << temp_header->other_info.size() << endl;
		//cout << "rdp receive message_len: " << temp_header->message_len << endl;
		m->msgFlat(write_buffer);
		//cout << "rdp receive message: " << write_buffer << endl;
		cout << write_buffer << endl;
	}
}

void telnet_send_pipe(int *pipearray) {
	int bytes_read;
	char *read_buffer = new char[BUFSIZE];
	char *temp_buffer;
	char *send_buffer = new char[BUFSIZE];
	while(1) {
		bzero(read_buffer, BUFSIZE);
		unique_lock<mutex> lk(application_telnet_mutex);
		application_telnet_cv.wait(lk);
		bytes_read = read(pipearray[28], read_buffer, BUFSIZE);
		lk.unlock();
		if(bytes_read == -1) {
			fprintf(stderr, "error reading from application telnet send pipe: %s\n", strerror(errno));
			exit(1);
		}
		//cout << "telnet send message: " << read_buffer << endl;
		cout << read_buffer << endl;
		temp_buffer = append_header_to_message(0, 8, read_buffer, bytes_read);
		memcpy(send_buffer, "6", 1);
		memcpy(send_buffer + 1, temp_buffer, (HEADER_LEN + bytes_read));
		ftp_telnet_send_mutex.lock();
		if(write(pipearray[19], send_buffer, (bytes_read + HEADER_LEN + 1)) == -1) {
			fprintf(stderr, "error writing to ftp/telnet send pipe: %s\n", strerror(errno));
		}
		ftp_telnet_send_mutex.unlock();
		ftp_telnet_send_cv.notify_one();
	}
}

void telnet_receive_pipe(int *pipearray) {
	char *buffer = new char[BUFSIZE];
	char *write_buffer = new char[BUFSIZE];
	while(1) {
		bzero(buffer, BUFSIZE);
		if(read(pipearray[16], buffer, BUFSIZE) == -1) {
			fprintf(stderr, "error reading from telnet receive pipe: %s\n", strerror(errno));
		}
		Message *m = new Message(buffer, BUFSIZE);
		header *temp_header = (header *)m->msgStripHdr(HEADER_LEN);
		//cout << "telnet receive hlp: " << temp_header->hlp << endl;
		//cout << "telnet receive oi: " << temp_header->other_info.size() << endl;
		//cout << "telnet receive message_len: " << temp_header->message_len << endl;
		m->msgFlat(write_buffer);
		//cout << "telnet receive message: " << write_buffer << endl;
		cout << write_buffer << endl;
	}
}

void ftp_send_pipe(int *pipearray) {
	int bytes_read;
	char *read_buffer = new char[BUFSIZE];
	char *temp_buffer;
	char *send_buffer = new char[BUFSIZE];
	while(1) {
		bzero(read_buffer, BUFSIZE);
		unique_lock<mutex> lk(application_ftp_mutex);
		application_ftp_cv.wait(lk);
		bytes_read = read(pipearray[26], read_buffer, BUFSIZE);
		lk.unlock();
		if(bytes_read == -1) {
			fprintf(stderr, "error reading from application ftp send pipe: %s\n", strerror(errno));
			exit(1);
		}
		//cout << "ftp send message: " << read_buffer << endl;
		cout << read_buffer << endl;
		temp_buffer = append_header_to_message(0, 8, read_buffer, bytes_read);
		memcpy(send_buffer, "5", 1);
		memcpy(send_buffer + 1, temp_buffer, (HEADER_LEN + bytes_read));
		ftp_telnet_send_mutex.lock();
		if(write(pipearray[19], send_buffer, (bytes_read + HEADER_LEN + 1)) == -1) {
			fprintf(stderr, "error writing to ftp/telnet send pipe: %s\n", strerror(errno));
		}
		ftp_telnet_send_mutex.unlock();
		ftp_telnet_send_cv.notify_one();
	}
}

void ftp_receive_pipe(int *pipearray) {
	char *buffer = new char[BUFSIZE];
	char *write_buffer = new char[BUFSIZE];
	while(1) {
		bzero(buffer, BUFSIZE);
		if(read(pipearray[14], buffer, BUFSIZE) == -1) {
			fprintf(stderr, "error reading from ftp receive pipe: %s\n", strerror(errno));
		}
		Message *m = new Message(buffer, BUFSIZE);
		header *temp_header = (header *)m->msgStripHdr(HEADER_LEN);
		//cout << "ftp receive hlp: " << temp_header->hlp << endl;
		//cout << "ftp receive oi: " << temp_header->other_info.size() << endl;
		//cout << "ftp receive message_len: " << temp_header->message_len << endl;
		m->msgFlat(write_buffer);
		//cout << "ftp receive message: " << write_buffer << endl;
		cout << write_buffer << endl;
	}
}

void udp_send_pipe(int *pipearray) {
	int bytes_read, higher_protocol_id;
	char *read_buffer = new char[BUFSIZE];
	char *read_buffer_minus_id = new char[BUFSIZE];
	char *temp_buffer;
	char *send_buffer = new char[BUFSIZE];
	while(1) {
		bzero(read_buffer, BUFSIZE);
		bzero(read_buffer_minus_id, BUFSIZE);
		unique_lock<mutex> lk(dns_rdp_send_mutex);
		dns_rdp_send_cv.wait(lk);
		bytes_read = read(pipearray[24], read_buffer, BUFSIZE);
		lk.unlock();
		if(bytes_read == -1) {
			fprintf(stderr, "error reading from rdp/dns send pipe: %s\n", strerror(errno));
			exit(1);
		}
		higher_protocol_id =  (int)atol(&read_buffer[0]);
		//cout << "udp send hlp: " << higher_protocol_id << endl;
		//cout << "udp bytes read: " << bytes_read << endl;
		memcpy(read_buffer_minus_id, read_buffer + 1, BUFSIZE-1);
		temp_buffer = append_header_to_message(higher_protocol_id, 4, read_buffer_minus_id, bytes_read-1);
		memcpy(send_buffer, "4", 1);
		memcpy(send_buffer + 1, temp_buffer, (HEADER_LEN + bytes_read - 1));
		udp_tcp_send_mutex.lock();
		if(write(pipearray[13], send_buffer, (bytes_read + HEADER_LEN)) == -1) {
			fprintf(stderr, "error writing to udp/tcp send pipe: %s\n", strerror(errno));
		}
		udp_tcp_send_mutex.unlock();
		udp_tcp_send_cv.notify_one();
	}
}

void udp_receive_pipe(int *pipearray) {
	char *buffer = new char[BUFSIZE];
	char *write_buffer = new char[BUFSIZE];
	while(1) {
		bzero(buffer, BUFSIZE);
		if(read(pipearray[8], buffer, BUFSIZE) == -1) {
			fprintf(stderr, "error reading from udp receive pipe: %s\n", strerror(errno));
		}
		Message *m = new Message(buffer, BUFSIZE);
		header *temp_header = (header *)m->msgStripHdr(HEADER_LEN);
		//cout << "udp receive hlp: " << temp_header->hlp << endl;
		//cout << "udp receive oi: " << temp_header->other_info.size() << endl;
		//cout << "udp receive message_len: " << temp_header->message_len << endl;
		m->msgFlat(write_buffer);
		if(temp_header->hlp == 7) {
			if(write(pipearray[21], write_buffer, BUFSIZE) == -1) {
				fprintf(stderr, "error writing to rdp receive pipe: %s\n", strerror(errno));
			}
		}
		else if(temp_header->hlp == 8) {
			if(write(pipearray[23], write_buffer, BUFSIZE) == -1) {
				fprintf(stderr, "error writing to dns receive pipe: %s\n", strerror(errno));
			}
		}
	}
}

void tcp_send_pipe(int *pipearray) {
	int bytes_read, higher_protocol_id;
	char *read_buffer = new char[BUFSIZE];
	char *read_buffer_minus_id = new char[BUFSIZE];
	char *temp_buffer;
	char *send_buffer = new char[BUFSIZE];
	while(1) {
		bzero(read_buffer, BUFSIZE);
		bzero(read_buffer_minus_id, BUFSIZE);
		unique_lock<mutex> lk(ftp_telnet_send_mutex);
		ftp_telnet_send_cv.wait(lk);
		bytes_read = read(pipearray[18], read_buffer, BUFSIZE);
		lk.unlock();
		if(bytes_read == -1) {
			fprintf(stderr, "error reading from telnet/ftp send pipe: %s\n", strerror(errno));
			exit(1);
		}
		higher_protocol_id =  (int)atol(&read_buffer[0]);
		//cout << "tcp send hlp: " << higher_protocol_id << endl;
		memcpy(read_buffer_minus_id, read_buffer + 1, BUFSIZE-1);
		temp_buffer = append_header_to_message(higher_protocol_id, 4, read_buffer_minus_id, bytes_read-1);
		memcpy(send_buffer, "3", 1);
		memcpy(send_buffer + 1, temp_buffer, (HEADER_LEN + bytes_read-1));
		udp_tcp_send_mutex.lock();
		if(write(pipearray[13], send_buffer, (bytes_read + HEADER_LEN )) == -1) {
			fprintf(stderr, "error writing to udp/tcp send pipe: %s\n", strerror(errno));
		}
		udp_tcp_send_mutex.unlock();
		udp_tcp_send_cv.notify_one();
	}
}

void tcp_receive_pipe(int *pipearray) {
	char *buffer = new char[BUFSIZE];
	char *write_buffer = new char[BUFSIZE];
	while(1) {
		bzero(buffer, BUFSIZE);
		if(read(pipearray[10], buffer, BUFSIZE) == -1) {
			fprintf(stderr, "error reading from tcp receive pipe: %s\n", strerror(errno));
		}
		Message *m = new Message(buffer, BUFSIZE);
		header *temp_header = (header *)m->msgStripHdr(HEADER_LEN);
		//cout << "tcp receive hlp: " << temp_header->hlp << endl;
		//cout << "tcp receive oi: " << temp_header->other_info.size() << endl;
		//cout << "tcp receive message_len: " << temp_header->message_len << endl;
		m->msgFlat(write_buffer);
		if(temp_header->hlp == 5) {
			if(write(pipearray[15], write_buffer, BUFSIZE) == -1) {
				fprintf(stderr, "error writing to ftp receive pipe: %s\n", strerror(errno));
			}
		}
		else if(temp_header->hlp == 6) {
			if(write(pipearray[17], write_buffer, BUFSIZE) == -1) {
				fprintf(stderr, "error writing to telnet receive pipe: %s\n", strerror(errno));
			}
		}
	}
}

void ip_receive_pipe(int *pipearray) {
	char *buffer = new char[BUFSIZE];
	char *write_buffer = new char[BUFSIZE];
	while(1) {
		bzero(buffer, BUFSIZE);
		if(read(pipearray[4], buffer, BUFSIZE) == -1) {
			fprintf(stderr, "error reading from ip receive pipe: %s\n", strerror(errno));
		}
		Message *m = new Message(buffer, BUFSIZE);
		header *temp_header = (header *)m->msgStripHdr(HEADER_LEN);
		//cout << "ip receive hlp: " << temp_header->hlp << endl;
		//cout << "ip receive oi: " << temp_header->other_info.size() << endl;
		//cout << "ip receive message_len: " << temp_header->message_len << endl;
		m->msgFlat(write_buffer);
		if(temp_header->hlp == 3) {
			if(write(pipearray[11], write_buffer, BUFSIZE) == -1) {
				fprintf(stderr, "error writing to tcp receive pipe: %s\n", strerror(errno));
			}
		}
		else if(temp_header->hlp == 4) {
			if(write(pipearray[9], write_buffer, BUFSIZE) == -1) {
				fprintf(stderr, "error writing to udp receive pipe: %s\n", strerror(errno));
			}
		}
	}
}

void ip_send_pipe(int *pipearray) {
	int bytes_read, higher_protocol_id;
	char *read_buffer = new char[BUFSIZE];
	char *read_buffer_minus_id = new char[BUFSIZE];
	char *send_buffer;
	while(1) {
		bzero(read_buffer, BUFSIZE);
		bzero(read_buffer_minus_id, BUFSIZE);
		unique_lock<mutex> lk(udp_tcp_send_mutex);
		udp_tcp_send_cv.wait(lk);
		bytes_read = read(pipearray[12], read_buffer, BUFSIZE);
		lk.unlock();
		if(bytes_read == -1) {
			fprintf(stderr, "error reading from udp/tcp send pipe: %s\n", strerror(errno));
			exit(1);
		}
		higher_protocol_id =  (int)atol(&read_buffer[0]);
		//cout << "ip send hlp: " << higher_protocol_id << endl;
		//cout << "ip send bytes read: " << bytes_read - 1 << endl;
		memcpy(read_buffer_minus_id, read_buffer + 1, BUFSIZE-1);
		send_buffer = append_header_to_message(higher_protocol_id, 12, read_buffer_minus_id, bytes_read-1);
		ip_send_mutex.lock();
		if(write(pipearray[7], send_buffer, (bytes_read + HEADER_LEN - 1)) == -1) {
			fprintf(stderr, "error writing to ip send pipe: %s\n", strerror(errno));
		}
		ip_send_mutex.unlock();
		ip_send_cv.notify_one();
	}
}

void ethernet_send_pipe(int *pipearray) {
	int bytes_read;
	char *read_buffer = new char[BUFSIZE];
	char *send_buffer;
	while(1) {
		bzero(read_buffer, BUFSIZE);
		unique_lock<mutex> lk(ip_send_mutex);
		ip_send_cv.wait(lk);
		bytes_read = read(pipearray[6], read_buffer, BUFSIZE);
		lk.unlock();
		if(bytes_read == -1) {
			fprintf(stderr, "error reading from ip send pipe: %s\n", strerror(errno));
			exit(1);
		}
		//cout << "ethernet send bytes read: " << bytes_read << endl;
		cout << "5" << endl;
		send_buffer = append_header_to_message(2, 8, read_buffer, bytes_read);
		ethernet_send_mutex.lock();
		if(write(pipearray[3], send_buffer, (bytes_read + HEADER_LEN)) == -1) {
			fprintf(stderr, "error writing to ethernet send pipe: %s\n", strerror(errno));
		}
		ethernet_send_mutex.unlock();
		ethernet_send_cv.notify_one();
	}
}

void ethernet_receive_pipe(int *pipearray) {
	char *buffer = new char[BUFSIZE];
	char *write_buffer = new char[BUFSIZE];
	while(1) {
		bzero(buffer, BUFSIZE);
		bzero(write_buffer, BUFSIZE);
		if(read(pipearray[0], buffer, BUFSIZE) == -1) {
			fprintf(stderr, "error reading from ethernet receive pipe: %s\n", strerror(errno));
		}
		Message *m = new Message(buffer, BUFSIZE);
		header *temp_header = (header *)m->msgStripHdr(HEADER_LEN);
		//cout << "ethernet receive hlp: " << temp_header->hlp << endl;
		//cout << "ethernet receive oi: " << temp_header->other_info.size() << endl;
		//cout << "ethernet receive message_len: " << temp_header->message_len << endl;
		m->msgFlat(write_buffer);
		if(write(pipearray[5], write_buffer, temp_header->message_len) == -1) {
			fprintf(stderr, "error writing to ip receive pipe: %s\n", strerror(errno));
		}
	}
}


int *create_all_protocol_pipes() {
	int i;
	int j = 0;
	int *pipearray = new int[NUM_PROTOCOL_PIPES_FDS];
	for(i = 0; i < NUM_PROTOCOL_PIPES; i++) {
		int *pipefd = new int[2];
		if(pipe(pipefd) == -1) {
			fprintf(stderr, "error creating pipe: %s\n", strerror(errno));
		}
		pipearray[j] = pipefd[0];
		j++;
		pipearray[j] = pipefd[1];
		j++;
	}
	return pipearray;
}

int create_udp_socket(int socket_type) {
	struct sockaddr_in clientaddr;
	int s, errno;
	memset(&clientaddr, 0, sizeof(clientaddr));
	clientaddr.sin_family = AF_INET;
	clientaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	clientaddr.sin_port = htons(0);
	if((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		fprintf(stderr, "cannot creat UDP socket: %s\n", strerror(errno));
	}
	if(::bind(s, (struct sockaddr *)&clientaddr, sizeof(clientaddr)) < 0) {
		fprintf(stderr, "can't  to port: %s\n", strerror(errno));
		exit(1);
	}
	else {
		socklen_t socklen = sizeof(clientaddr);
		if((getsockname(s, (struct sockaddr *)&clientaddr, &socklen)) < 0) {
			fprintf(stderr, "getsockname error: %s\n", strerror(errno));
		}
		if(socket_type == 1) {
			printf("New OUT udp socket port number is %d\n", ntohs(clientaddr.sin_port));
			return s;
		}
		else {
			printf("New IN udp socket port number is %d\n", ntohs(clientaddr.sin_port));
			return s;
		}
	}
}

void socket_send(int port_number, int s, int ethernet_send_pipe_read_end) {
	char buffer[BUFSIZE];
	while(1) {
		bzero(buffer, BUFSIZE);
		unique_lock<mutex> lk(ethernet_send_mutex);
		ethernet_send_cv.wait(lk);
		if(read(ethernet_send_pipe_read_end, buffer, BUFSIZE) == -1) {
			fprintf(stderr, "error reading from send_pipe pipe: %s\n", strerror(errno));
		}
		lk.unlock();
		int sendto_error;
		struct sockaddr_in servaddr;
		bzero(&servaddr, sizeof(servaddr));
		servaddr.sin_family = AF_INET;
		servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
		servaddr.sin_port = htons(port_number);
		sendto_error = sendto(s, buffer, BUFSIZE, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
		if(sendto_error == -1) {
			fprintf(stderr, "error sending udp message: %s\n", strerror(errno));
		}
	}
}

void socket_receive(int s, int ethernet_receive_pipe_write_end) {
	char *buf = new char[BUFSIZE];
	int recvfrom_error;
	while(1) {
		bzero(buf, BUFSIZE);
		recvfrom_error = recvfrom(s, buf, BUFSIZE, 0, NULL, NULL);
		if(recvfrom_error == -1) {
			fprintf(stderr, "error receiving udp message: %s\n", strerror(errno));
		}
		if(write(ethernet_receive_pipe_write_end, buf, BUFSIZE) == -1) {
			fprintf(stderr, "error writing to ethernet receive pipe: %s\n", strerror(errno));
		}
	}
}
/*
void initialize_sems() {
	sem_init(&dns_rdp_send_sem, 0, 1);
	sem_init(&udp_tcp_send_sem, 0, 1);
	sem_init(&telnet_ftp_send_sem, 0, 1);
}

void destroy_sems() {
	sem_destroy(&dns_rdp_send_sem);
	sem_destroy(&udp_tcp_send_sem);
	sem_destroy(&telnet_ftp_send_sem);
}
*/
/*
void dispatch_threads(int *pipearray) {
	thread ethernet_receive (ethernet_receive_pipe, pipearray);
	thread ethernet_send (ethernet_send_pipe, pipearray);
	thread ip_send (ip_send_pipe, pipearray);
	thread ip_receive (ip_receive_pipe, pipearray);
	thread tcp_send (tcp_send_pipe, pipearray);
	thread tcp_receive (tcp_receive_pipe, pipearray);
	thread udp_send (udp_send_pipe, pipearray);
	thread udp_receive (udp_receive_pipe, pipearray);
	thread ftp_send (ftp_send_pipe, pipearray);
	thread ftp_receive (ftp_receive_pipe, pipearray);
	thread telnet_send (telnet_send_pipe, pipearray);
	thread telnet_receive (telnet_receive_pipe, pipearray);
	thread rdp_send (rdp_send_pipe, pipearray);
	thread rdp_receive (rdp_receive_pipe, pipearray);
	thread dns_send (dns_send_pipe, pipearray);
	thread dns_receive (dns_receive_pipe, pipearray);
}
*/
/*
void join_threads() {
	ethernet_receive.join();
	ethernet_send.join();
	ip_send.join();
	ip_receive.join();
	tcp_send.join();
	tcp_receive.join();
	udp_send.join();
	udp_receive.join();
	ftp_send.join();
	ftp_receive.join();
	telnet_send.join();
	telnet_receive.join();
	rdp_send.join();
	rdp_receive.join();
	dns_send.join();
	dns_receive.join();
}
*/
int main(int argc, char *argv[]) {
	int *pipearray = create_all_protocol_pipes();
	int job;
	if(argc < 2) {
		fprintf(stderr, "usage: ./process_per_protocol <job>");
		exit(1);
	}
	else {
		job = (int)atol(argv[1]);
	}

	if(job == 1) {
		int serv_in_udp_port, out_udp_socket, in_udp_socket, continue_;
		out_udp_socket = create_udp_socket(OUT_SOCKET_TYPE);
		in_udp_socket = create_udp_socket(IN_SOCKET_TYPE);
		cout << "Enter the port number of the server in udp socket: ";
		cin >> serv_in_udp_port;
		thread socket_r (socket_receive, in_udp_socket, pipearray[1]);
		thread socket_s (socket_send, serv_in_udp_port, out_udp_socket, pipearray[2]);
		thread ethernet_receive (ethernet_receive_pipe, pipearray);
		thread ethernet_send (ethernet_send_pipe, pipearray);
		thread ip_send (ip_send_pipe, pipearray);
		thread ip_receive (ip_receive_pipe, pipearray);
		thread tcp_send (tcp_send_pipe, pipearray);
		thread tcp_receive (tcp_receive_pipe, pipearray);
		thread dns_send (dns_send_pipe, pipearray);
		thread udp_send (udp_send_pipe, pipearray);
		thread udp_receive (udp_receive_pipe, pipearray);
		thread ftp_send (ftp_send_pipe, pipearray);
		thread ftp_receive (ftp_receive_pipe, pipearray);
		thread telnet_send (telnet_send_pipe, pipearray);
		thread telnet_receive (telnet_receive_pipe, pipearray);
		thread rdp_send (rdp_send_pipe, pipearray);
		thread rdp_receive (rdp_receive_pipe, pipearray);
		thread dns_receive (dns_receive_pipe, pipearray);
		//dispatch_threads(pipearray);
		cout << "Write message? ";
		cin >> continue_;
		thread application_dns (application_to_dns, pipearray);
		thread application_telnet (application_to_telnet, pipearray);
		thread application_rdp (application_to_rdp, pipearray);
		thread application_ftp (application_to_ftp, pipearray);
		cout << "End program? ";
		cin >> continue_;
		socket_r.join();
		socket_s.join();
		ethernet_receive.join();
		ethernet_send.join();
		ip_send.join();
		ip_receive.join();
		tcp_send.join();
		tcp_receive.join();
		udp_send.join();
		udp_receive.join();
		ftp_send.join();
		ftp_receive.join();
		telnet_send.join();
		telnet_receive.join();
		rdp_send.join();
		rdp_receive.join();
		dns_send.join();
		dns_receive.join();
		application_telnet.join();
		application_dns.join();
		application_rdp.join();
		application_ftp.join();
		//join_threads();
	}
	else {
		int serv_in_udp_port, out_udp_socket, in_udp_socket, continue_;
		out_udp_socket = create_udp_socket(OUT_SOCKET_TYPE);
		in_udp_socket = create_udp_socket(IN_SOCKET_TYPE);
		cout << "Enter the port number of the server in udp socket: ";
		cin >> serv_in_udp_port;
		thread socket_r (socket_receive, in_udp_socket, pipearray[1]);
		thread socket_s (socket_send, serv_in_udp_port, out_udp_socket, pipearray[2]);
		thread ethernet_receive (ethernet_receive_pipe, pipearray);
		thread ethernet_send (ethernet_send_pipe, pipearray);
		thread ip_send (ip_send_pipe, pipearray);
		thread ip_receive (ip_receive_pipe, pipearray);
		thread tcp_send (tcp_send_pipe, pipearray);
		thread tcp_receive (tcp_receive_pipe, pipearray);
		thread dns_send (dns_send_pipe, pipearray);
		thread udp_send (udp_send_pipe, pipearray);
		thread udp_receive (udp_receive_pipe, pipearray);
		thread ftp_send (ftp_send_pipe, pipearray);
		thread ftp_receive (ftp_receive_pipe, pipearray);
		thread telnet_send (telnet_send_pipe, pipearray);
		thread telnet_receive (telnet_receive_pipe, pipearray);
		thread rdp_send (rdp_send_pipe, pipearray);
		thread rdp_receive (rdp_receive_pipe, pipearray);
		thread dns_receive (dns_receive_pipe, pipearray);
		//dispatch_threads(pipearray);
		//cout << "Write message? ";
		//cin >> continue_;
		//application_to_dns(pipearray);
		cout << "End program? ";
		cin >> continue_;
		socket_r.join();
		socket_s.join();
		ethernet_receive.join();
		ethernet_send.join();
		ip_send.join();
		ip_receive.join();
		tcp_send.join();
		tcp_receive.join();
		udp_send.join();
		udp_receive.join();
		ftp_send.join();
		ftp_receive.join();
		telnet_send.join();
		telnet_receive.join();
		rdp_send.join();
		rdp_receive.join();
		dns_send.join();
		dns_receive.join();
		//join_threads();
	}
	return 0;
}
