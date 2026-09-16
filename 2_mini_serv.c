// ----- includes -----
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/time.h>

// ----- variables globales (8) -----
int server_fd;
int max_fd;
int max_id;
int ids[1024];
fd_set afds, wfds, rfds;
char *clts_recv_buf[1024] = {0};
char serv_recv_buf[1000001] = {0};
char serv_send_buf[1000001] = {0};

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
	for (int fd = 0; fd <= max_fd; fd++)
	{
		// si fds actif, close fd et liberer le buffer
		if (FD_ISSET(fd, &afds))
		{
			close(fd);
			free(clts_recv_buf[fd]);
		} 
	}
}

// ----- exit_error -----
void exit_error(char *msg_error)
{
	// 1. liberer les ressources
	cleanup();

	// 2. afficher err_msg
	write(2, msg_error, strlen(msg_error));

	// 3. quitter
	exit(1);
}

// ----- broadcats_str -----
void broadcast_str(int sender_fd, char *str)
{
	// parcourir les fds
	for (int fd = 0 ; fd <= max_fd ; fd++)
	{
		// pas d'envoi vers le serveur ou soi-meme
		if (fd == server_fd || fd == sender_fd)
			continue;
		// pas d'envoi ves les fds inactifs / pas prets en ecriture
		else if (!FD_ISSET(fd, &afds) || !FD_ISSET(fd, &wfds))
			continue;
		else
			send(fd, str, strlen(str), 0);
	}
}

void register_client(int fd)
{
	// update max_fd
	if (fd > max_fd)
		max_fd = fd;

	// register (ajouter dans afds et ids)
	FD_SET(fd, &afds);
	ids[fd] = max_id++;

	// construire le msg dans le buf
	sprintf(serv_send_buf, "server: client %d just arrived\n", ids[fd]);

	// broadcast le msg du serveur a tous les autres
	broadcast_str(fd, serv_send_buf);
}

void remove_client(int fd)
{
	// retirer de afds
	FD_CLR(fd, &afds);

	// nettoyer les ressources (fd et buf)
	close(fd);
	free(clts_recv_buf[fd]);
	clts_recv_buf[fd] = NULL;

	// construire le msg dans le buf
	sprintf(serv_send_buf, "server: client %d just left\n", ids[fd]);

	// broadcast le msg du serveur a tous les autres
	broadcast_str(fd, serv_send_buf);

}

// ----- handle_clt_msg -----
/*
	valeurs de retour extract msg
		-1 : erreur malloc
		 1 : '\n' trouve -> diffuser
		 0 : pas de '\n' -> en attente d'autre send()
*/
void handle_clt_msg(int fd)
{
	// variables
	char *msg;
	int ret = extract_message(&clts_recv_buf[fd], &msg);

	// boucle d'extraction
	while (ret == 1)
	{
		// construire le msg
		sprintf(serv_send_buf, "client %d: %s", ids[fd], msg);

		// diffuser le msg
		broadcast_str(fd, serv_send_buf);

		// liberer msg (reutilise a chq iteration)
		free(msg);

		// tentative d'extraction
		ret = extract_message(&clts_recv_buf[fd], &msg);
	}
	
	// si erreur malloc
	if (ret == -1)
		exit_error("Fatal error\n");

	// si pas de ligne complet
}

int main(int argc, char **argv)
{
	//& 1. declaration des variables
	int connfd;
	socklen_t len;
	struct sockaddr_in servaddr;
	struct sockaddr_in cli;

	//& 2. check argc
	if (argc != 2)
		exit_error("Wrong number of arguments\n");

	// socket create and verification 
	FD_ZERO(&afds);
	server_fd = socket(AF_INET, SOCK_STREAM, 0); 
	if (server_fd == -1)
		exit_error("Fatal error\n");
	bzero(&servaddr, sizeof(servaddr)); 

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	int port = atoi(argv[1]);
	servaddr.sin_port = htons(port); 
  
	// Binding newly created socket to given IP and verification 
	if ((bind(server_fd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
		exit_error("Fatal error\n");

	if (listen(server_fd, 10) != 0)
		exit_error("Fatal error\n");
	//? new
	FD_SET(server_fd, &afds); // ajouter server_fd dans afds
	max_fd = server_fd;	// update max_fd

	//? new boucle ecoute
	while (1)
	{
		//& 1. recopier afds dans wfds et rfds avant chaque select()
		//&    select() modifie ces sets a chaque tour -> faire une copie propre
		wfds = afds;
		rfds = afds;

		//& 2. attendre qu'au moins un fds soit pret en lecture ou en ecriture
		if (select(max_fd + 1, &rfds, &wfds, NULL, NULL) < 0)
			exit_error("Fatal error\n");

		//& 3. parcourir tous les fds pour identifier les fds actifs
		for (int fd = 0; fd <= max_fd; fd++)
		{
			//& 3.1 fd inactif -> rien a lire -> continue
			if (!FD_ISSET(fd, &rfds))
				continue;

			//& 3.2 fd = serveur_fd -> nouvelle connexion -> register
			if (fd == server_fd)
			{
				len = sizeof(cli);
				//connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
				connfd = accept(server_fd, (struct sockaddr *)&cli, &len);
				if (connfd < 0) 
					continue; // echec transitoire

				register_client(connfd);
				continue;
			}

			//& 3.3 donnees recues d'un client existant
			ssize_t rec_bytes = recv(fd, serv_recv_buf, 1000000, 0);

			//& si client deconnecte
			if (rec_bytes <= 0)
			{
				remove_client(fd);
				continue;
			}

			//& 3.4 ajouter '\0' et cumuler pour client
			serv_recv_buf[rec_bytes]= '\0';
			clts_recv_buf[fd] = str_join(clts_recv_buf[fd], serv_recv_buf);
			
			//& check erreur malloc
			if (!clts_recv_buf[fd])
				exit_error("Fatal error\n");

			//& 3.5 traiter les lignes completes
			handle_clt_msg(fd);
		}
	}
}