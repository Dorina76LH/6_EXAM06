#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>

int max_fd;
int serv_fd;
int next_id = 0;
int ids[1024];
char *msgs[1024] = {0};
char rec_buf[1000001] = {0};
char send_buf[1000001] = {0};
fd_set afds, wfds, rfds;

int extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int	i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == 0)
		return (0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

void cleanup() {
	for (int fd = 0; fd <= max_fd; fd++) {
		if (FD_ISSET(fd, &afds)) {
			close(fd);
			free(msgs[fd]);
		}
	}
}

void error_exit(char *str) {
	cleanup();
	write(2, str, strlen(str));
	exit(1);
}

void broadcast_str(int sender, char *str) {
	for (int fd = 0; fd <= max_fd; fd++) {
		if (fd != serv_fd && fd != sender && FD_ISSET(fd, &wfds)) {
			send(fd, str, strlen(str), 0);
		}
	}
}

void register_client(int fd) {
	if (fd > max_fd)
		max_fd = fd;
	FD_SET(fd, &afds);
	ids[fd] = next_id++;
	sprintf(send_buf, "server: client %d just arrived\n", ids[fd]);
	broadcast_str(fd, send_buf);
}

void remove_client(int fd) {
	FD_CLR(fd, &afds);
	close(fd);
	free(msgs[fd]);
	msgs[fd] = NULL;
	sprintf(send_buf, "server: client %d just left\n", ids[fd]);
	broadcast_str(fd, send_buf);
}

void broadcast(int fd) {
	char *message;
	int ret = extract_message(&msgs[fd], &message);

	while (ret == 1) {
		sprintf(send_buf, "client %d: %s", ids[fd], message);
		broadcast_str(fd, send_buf);
		free(message);
		ret = extract_message(&msgs[fd], &message);
	}
	if (ret == -1)
		error_exit("Fatal error\n");
}

int main(int argc, char *argv[]) {
	int connfd;
	socklen_t len;
	struct sockaddr_in servaddr, cli; 

	FD_ZERO(&afds);
	if (argc != 2)
		error_exit("Wrong number of arguments\n");
	// socket create and verification 
	serv_fd = socket(AF_INET, SOCK_STREAM, 0); 
	if (serv_fd == -1)
		error_exit("Fatal error\n");
	bzero(&servaddr, sizeof(servaddr)); 

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(atoi(argv[1])); 
  
	// Binding newly created socket to given IP and verification 
	if ((bind(serv_fd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
		error_exit("Fatal error\n");
	if (listen(serv_fd, 1024) != 0)
		error_exit("Fatal error\n");
	FD_SET(serv_fd,&afds);
	max_fd = serv_fd;
	while (1) {
		wfds = afds;
		rfds = afds;
		if (select(max_fd + 1, &rfds, &wfds, NULL, NULL) < 0)
			error_exit("Fatal error\n");
		for (int fd = 0; fd <= max_fd; fd++) {
			if (!FD_ISSET(fd, &rfds))
				continue;
			if (fd == serv_fd) {
				len = sizeof(cli);
				connfd = accept(serv_fd, (struct sockaddr *)&cli, &len);
				if (connfd < 0)
					continue;
				register_client(connfd);
				continue;
			}
			ssize_t rec_bytes = recv(fd, rec_buf, 1000000, 0);
			if (rec_bytes <= 0){
				remove_client(fd);
				continue;
			}
			rec_buf[rec_bytes] = 0;
			msgs[fd] = str_join(msgs[fd], rec_buf);
			if (!msgs[fd])
				error_exit("Fatal error\n");
			broadcast(fd);
		}
	}
}