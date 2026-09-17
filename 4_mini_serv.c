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

// ----- variables -----
int server_fd = -1;
int max_fd = 0;
int max_id = 0;
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
    for (int fd = 0 ; fd <= max_fd ; fd++)
    {
        // liberer les ressources (fd et malloc)
        close(fd);
        free(clts_recv_buf[fd]);
        clts_recv_buf[fd] = NULL;
    }
}

// ----- exit_error -----
void exit_error(char *err_msg)
{
    // liberer les ressources
    cleanup();

    // imprimer le msg d'erreur
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
        // pas d'envoi vers le serveur ou soi-meme
        if (sender_fd == server_fd || sender_fd == ids[fd])
            continue;

        // pas d'envoi vers un fd inactif ou un fd pas pret pour l'ecriture
        if (!FD_ISSET(fd, &afds) || !FD_ISSET(fd, &wfds))
            continue;

        // envoyer le msg
        // ssize_t send(int sockfd, const void *buf, size_t len, int flags);
        send(fd, str, strlen(str), 0);
    }
}

// ----- register_clt -----
void register_clt(int fd)
{
    // maj  fd_max
    if (fd > max_fd)
        max_fd = fd;
    
    // register (ajout dans afds & ids)
    FD_SET(fd, &afds);
    ids[fd]=max_id++;

    // construire le msg
    sprintf(serv_send_buf, "server: client %d just arrived\n", ids[fd]);

    // diffuser le msg
    broadcast_str(fd, serv_send_buf);
}

// ----- register_clt -----
void remove_clt(int fd)
{
    // desinscrire le clt
    FD_CLR(fd, &afds);
    
    // liberer les ressources (afds, fd, buf)
    close(fd);
    free(clts_recv_buf[fd]);
    clts_recv_buf[fd] = NULL;
    
    // construire le msg
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

    // boucle d'extraction -> '\n' trouve
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

    // err malloc
    if (ret == -1)
        exit_error("Fatal error\n");
}


int main(int argc, char **argv)
{
	// variables
    int connfd;
    socklen_t len;
	struct sockaddr_in servaddr, cli;
    
    // check arg
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
        exit_error("Fatal error\n"); 

	if (listen(server_fd, 10) != 0)
        exit_error("Fatal error\n"); 

    // register server (ajout afds et max_fd)
    FD_SET(server_fd, &afds);
    max_fd = server_fd;

    // boucle d'ecoute
    while (1)
    {
        // creer une nouvelle copie wfds, rfds
        wfds = afds;
        rfds = afds;

        // attendre qu'un fd soit pret en lecture ou en ecriture
        // int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
        if(select(max_fd + 1, &rfds, &wfds, NULL, NULL) < 0)
            exit_error("Fatal error\n");

        // parcourir les fds pour traiter l'evenement
        for (int fd = 0 ; fd <= max_fd ; fd ++)
        {
            // fd inactif -> rien a lire -> fd suivant
            if (!FD_ISSET(fd, &rfds))
                continue;
            
            // nouvelle connection (accept)
            if (fd == server_fd)
            {
                len = sizeof(cli);
                connfd = accept(server_fd, (struct sockaddr *)&cli, &len);
                if (connfd < 0)
                    exit_error("Fatal error\n");
                
                register_clt(connfd);
                continue; // passer au fd suivant
            }
    
            // donnees recues de la part d'un client actif -> recv() dans serv_recv_buf
            // ssize_t recv(int sockfd, void *buf, size_t len, int flags)
            ssize_t recv_bytes = recv(fd, serv_recv_buf, 1000000, 0);

            // client a deconnecter
            if (recv_bytes <= 0)
            {
                remove_clt(fd);
                continue; // passer au fd suivant
            }

            // rajouter le '\0' dans le buf
            serv_recv_buf[recv_bytes] = '\0';

            // cumuler les donnees dans le buf clt
            clts_recv_buf[fd] = str_join(clts_recv_buf[fd], serv_recv_buf);

            // check erreur malloc
            if (!clts_recv_buf[fd])
                exit_error("Fatal error\n");
            
            // traiter la ligne complete
            handle_clt_msg(fd);
        }
    }
}