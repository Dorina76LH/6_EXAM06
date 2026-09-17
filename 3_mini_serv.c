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

// ----- variables -----
int serv_fd;
int max_fd;
int max_id = 0; // fd_server = 0
fd_set afds, wfds, rfds;
int ids[1024];
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
        // si fd actif
        if (FD_ISSET(fd, &afds))
        {
            close(fd);
            free(clts_recv_buf[fd]);
        }
    }
}

// ----- exit -----
void exit_error(char *str)
{
    // liberer les ressources
    cleanup();

    // ecrire le msg erreur
    write(2, str, strlen(str));

    // quitter
    exit(1);

}

// ----- broadcast_str -----
void broadcast_str(int sender_fd, char *str)
{
    // parcourir les fds
    for (int fd = 0 ; fd <= max_fd ; fd++)
    {
        // pas d'envoi vers le serveur et soi-meme
        if (fd == serv_fd || fd == sender_fd)
            continue;
        
        // pas d'envoi vers fd inactif ou pas disponible en ecriture
        else if (!FD_ISSET(fd, &afds) || !FD_ISSET(fd, &wfds))
            continue;

        // envoi du msg (send)
        else
            send(fd, str, strlen(str), 0);
    }
}

// ----- register_clt -----
void register_clt(int fd)
{
    // maj max_fd
    if (fd > max_fd)
        max_fd = fd;
    
    // register (ajout dans afds & ids)
    FD_SET(fd, &afds);
    ids[fd] = max_id++;

    // construire le msg
    sprintf(serv_send_buf, "server: client %d just arrived\n", ids[fd]);
    
    // diffuser le msg
    broadcast_str(fd, serv_send_buf);
}

// ----- remove_clt -----
void remove_clt(int fd)
{
    // desinscrire le clt
    FD_CLR(fd, &afds);

    // liberer les ressources
    close(fd);
    free(clts_recv_buf[fd]);
    clts_recv_buf[fd] = NULL;

    // construire le msg
    sprintf(serv_send_buf, "server: client %d just left\n", ids[fd]);
    
    // diffuser le msg
    broadcast_str(fd, serv_send_buf);

}

// ----- handle_clts_msg -----
/*
    extract_msg -> transfert malloc
        -1  erreur malloc
         1  extraction ligne complete ('\n')
         0  pas d'extraction (pas de '\n' -> attente de send)
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

        // liberer msg
        free(msg);

        // tentative nouvelle extraction
        ret = extract_message(&clts_recv_buf[fd], &msg);
    }

    // erreur malloc
    if (ret == -1)
        exit_error("Fatal error\n");
}

// ----- main -----
int main(int argc, char **argv)
{
	// variables
    int connfd;
    socklen_t len; //? new
	struct sockaddr_in servaddr, cli;
    
    // check args
    if (argc != 2) //? new
        exit_error("Wrong number of arguments\n");

	// socket create and verification
    FD_ZERO(&afds); //? new
	serv_fd = socket(AF_INET, SOCK_STREAM, 0); 
	if (serv_fd == -1)
        exit_error("Fatal error\n"); //?new
	bzero(&servaddr, sizeof(servaddr)); 

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(atoi(argv[1])); //? new
  
	// Binding newly created socket to given IP and verification 
	if ((bind(serv_fd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) 
        exit_error("Fatal error\n"); //? new
    
    if (listen(serv_fd, 10) != 0) 
        exit_error("Fatal error\n"); //? new
	
    //? update afds et max_fd -> serv_fd
    FD_SET(serv_fd, &afds);
    max_fd = serv_fd;

    //? (new) boucle ecoute serveur
    while (1)
    {
        //? (new) maj wfds et rfds
        wfds = afds;
        rfds = afds;
        
        //? (attente qu'un fd soit pret en lecture ou en ecriture)
        if (select(max_fd + 1, &rfds, &wfds, NULL, NULL) < 0)
            exit_error("Fatal error\n");

        //? (new) parcourir tous les fds
        for (int fd = 0 ; fd <= max_fd ; fd++)
        {
            // fd inactif -> rien a lire -> passer au fd suivant
            if (!FD_ISSET(fd, &rfds))
                continue;

            // fd == serv_fd -> nouvelle connexion (accept)
            if (fd == serv_fd)
            {
                len = sizeof(cli);
                connfd = accept(serv_fd, (struct sockaddr *)&cli, &len);
                if (connfd < 0)
                    continue ; //? new echec transitoire
                
                register_clt(connfd); //? new
                continue ; //? new passer au fd suivant
            }

            // fd actif -> donnees recues d'un client -> stocker dans serv_recv_buf 
            ssize_t recv_bytes = recv(fd, serv_recv_buf, 1000000, 0);

            // client deconnecte
            if (recv_bytes <= 0)
            {
                remove_clt(fd);
                continue;
            }

            // terminer la chaine
            serv_recv_buf[recv_bytes] = '\0';

            // ajouter le msg dans le buf client
            clts_recv_buf[fd] = str_join(clts_recv_buf[fd], serv_recv_buf); // retour malloc
            if (!clts_recv_buf[fd])
                exit_error("Fatal error\n");
            
            // traiter les lignes completes (avec '\n')
            handle_clt_msg(fd);
        }
    }
}