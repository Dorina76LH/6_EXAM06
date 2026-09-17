// ----- includes -----
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/time.h>

// ----- variables -----
int server_fd = -1;
int max_fd = 0;
int max_id = 0;
fd_set afds, wfds, rfds;
int ids[1024];
char *clts_recv_buf[1024] = {0};
char serv_send_buf[1000001] = {0};
char serv_recv_buf[1000001] = {0};

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

// ----- cleanup -----
void cleanup()
{
	// parcourir les fds
	for (int fd = 0 ; fd <= max_fd ; fd++)
	{
		// si fd actif
		if (FD_ISSET(fd, &afds))
		{
			close(fd);
			free(clts_recv_buf[fd]);
			clts_recv_buf[fd] = NULL;
		}
	}
}

// ----- exit_error -----
void exit_error(char *err_msg)
{
	// liberer les ressources
	cleanup();

	// ecrire le msg err
	write(2, err_msg, strlen(err_msg));

	// quitter
	exit(1);

}

// ----- broadcast_str -----
void broadcast_str(int sender_fd, char *str)
{
	// parcourir les fds
	for (int fd = 0 ; fd <= max_fd ; fd++)
	{
		// pas d'envoi vers le server ou soi-meme
		if (fd == server_fd || fd == sender_fd)
			continue;
		
		// si pas de fd actif ou fd pret pour l'ecriture -> suivant
		else if (!FD_ISSET(fd, &afds) || !FD_ISSET(fd, &wfds))
			continue;

		// envoyer le msg
		// ssize_t send(int socket, const void *buffer, size_t length, int flags);
		send(fd, str, strlen(str), 0);
	}
}

// ----- register_clt -----
void register_clt(int fd)
{
	// maj max_fd
	if(fd > max_fd)
		max_fd = fd;
	
	// enregistrer le client (ajout afds et ids)
	FD_SET(fd, &afds);
	ids[fd] = max_id++;

	// contruire le msg
	sprintf(serv_send_buf, "server: client %d just arrived\n", ids[fd]);

	// diffuser le msg
	broadcast_str(fd, serv_send_buf);
}

// ----- remove_clt -----
void remove_clt(int fd)
{
	// desinscire le clt
	FD_CLR(fd, &afds);
	
	// liberer les ressources
	close(fd);
	free(clts_recv_buf[fd]);
	clts_recv_buf[fd] = NULL;

	// contruire le msg
	sprintf(serv_send_buf, "server: client %d just left\n", ids[fd]);

	// diffuser le msg
	broadcast_str(fd, serv_send_buf);
}

// ----- handle_clt_msg -----
void handle_clt_msg(int fd)
{
	// variables
	char *msg;
	int ret = extract_message(&clts_recv_buf[fd], &msg);

	// boucle d'extration (tant au'il y a '\n')
	while (ret == 1)
	{
		// construire le msg
		sprintf(serv_send_buf, "client %d: %s", ids[fd], msg);

		// diffuser le msg
		broadcast_str(fd, serv_send_buf);
		
		// liberer msg
		free(msg);

		// tentative d'extraction
		ret = extract_message(&clts_recv_buf[fd], &msg);
	}

	// check err malloc
	if (ret == -1)
		exit_error("Fatal error\n");
}

int main(int argc, char **argv)
{
	int connfd;
	socklen_t len;
	struct sockaddr_in servaddr, cli;

	// check argc
	if (argc != 2)
	{
		exit_error("Wrong number of arguments\n");
	}

	// socket create and verification 
	server_fd = socket(AF_INET, SOCK_STREAM, 0); 
	if (server_fd == -1)
	{
		exit_error("Fatal error\n");
	} 
	bzero(&servaddr, sizeof(servaddr)); 

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(atoi(argv[1])); 
  
	// Binding newly created socket to given IP and verification 
	if ((bind(server_fd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
	{
		exit_error("Fatal error\n");
	} 
	
	if (listen(server_fd, 10) != 0)
	{
		exit_error("Fatal error\n");
	}

	// init variables
	FD_SET(server_fd, &afds);
	max_fd = server_fd;

	// boucle d'ecoute server
	while (1)
	{
		// creer une nouvelle copie de rfds et wfds
		wfds = afds;
		rfds = afds;

		// attendre qu'un fd soit pret en lecture ou en ecriture
		//int select(int nfds, fd_set *restrict readfds, fd_set *restrict writefds, fd_set *restrict errorfds, struct timeval *restrict timeout);
		if (select(max_fd + 1, &rfds, &wfds, NULL, NULL) < 0)
		{
			exit_error("Fatal error\n");
		}

		// parcourir les fds
		for (int fd = 0 ; fd <= max_fd ; fd++)
		{
			// fd inactif -> rien a lire -> continuer
			if (!FD_ISSET(fd, &rfds))
				continue;

			// connexion
			if (fd == server_fd)
			{
				len = sizeof(cli);
				connfd = accept(server_fd, (struct sockaddr *)&cli, &len);
				if (connfd < 0)
					continue;
				register_clt(connfd);
				continue;
			}

			// donnees recues
			//  ssize_t recv(int socket, void *buffer, size_t length, int flags);
			size_t recv_bytes = recv(fd, serv_recv_buf, 1000000, 0);
			
			// connexion perdue
			if (recv_bytes <= 0)
			{
				remove_clt(fd);
				continue;
			}

			// ajouter '\0' dans le buffer
			serv_recv_buf[recv_bytes] = '\0';

			// transferer le msg dans le buffer du client
			clts_recv_buf[fd] = str_join(clts_recv_buf[fd], serv_recv_buf);
			if (!clts_recv_buf[fd])
			{
				exit_error("Fatal error\n");
			}
			
			// diffuser le msg complet
			handle_clt_msg(fd);
		}
	}
}