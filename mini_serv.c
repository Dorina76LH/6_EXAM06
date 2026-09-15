/*
Assignment name  : mini_serv
Expected files   : mini_serv.c
Allowed functions: write, close, select, socket, accept, listen, send, recv,
				   bind, strstr, malloc, realloc, free, calloc, bzero, atoi,
				   sprintf, strlen, exit, strcpy, strcat, memset
--------------------------------------------------------------------------------

Write a program that will listen for client to connect on a certain port on 127.0.0.1
and will let clients to speak with each other

This program will take as first argument the port to bind to
- If no argument is given, it should write in stderr "Wrong number of arguments" followed by a \n
  and exit with status 1
- If a System Calls returns an error before the program start accepting connection,
  it should write in stderr "Fatal error" followed by a \n and exit with status 1
- If you cant allocate memory it should write in stderr "Fatal error" followed by a \n
  and exit with status 1

Your program must be non-blocking but client can be lazy and if they don't read your
message you must NOT disconnect them...

Your program must not contains #define preproc
Your program must only listen to 127.0.0.1
The fd that you will receive will already be set to make 'recv' or 'send' to block
if select hasn't be called before calling them, but will not block otherwise. 

When a client connect to the server:
- the client will be given an id. the first client will receive the id 0 and each
  new client will received the last client id + 1
- %d will be replace by this number
- a message is sent to all the client that was connected to the server: "server: client %d just arrived\n"

clients must be able to send messages to your program.
- message will only be printable characters, no need to check
- a single message can contains multiple \n
- when the server receive a message, it must resend it to all the other client with "client %d: "
  before every line!

When a client disconnect from the server:
- a message is sent to all the client that was connected to the server: "server: client %d just left\n"

Memory or fd leaks are forbidden

To help you, you will find the file main.c with the beginning of a server and maybe some useful functions.
(Beware this file use forbidden functions or write things that must not be there in your final program)

Warning our tester is expecting that you send the messages as fast as you can. Don't do un-necessary buffer.

Evaluation can be a bit longer than usual...

Hint: you can use nc to test your program
Hint: you should use nc to test your program
Hint: To test you can use fcntl(fd, F_SETFL, O_NONBLOCK) but use select and NEVER check EAGAIN (man 2 send)
*/

/*
	1. includes a ajouter : stdio, stdlib, sys/select
	2. declarer les variables globales
	3. remplacer printf par sprintf
	4. remplacer les msg erreur
*/


// ----------------------------------------------------------------------------
// includes
// ----------------------------------------------------------------------------
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>			// sprintf
#include <stdlib.h>			// malloc, calloc, realloc, free
#include <sys/select.h>		// select

// ----------------------------------------------------------------------------
// variables
// ----------------------------------------------------------------------------
int max_fd;							// fd max actuel a passer a select()
int server_fd;						// fd du socket serveur cree par socket ()
int next_id = 0;					// compteur global pour attribuer les id client
int ids[1024];						// id logique du client sur ce fd
fd_set afds;						// "all fds" : ensemble persistant de tous les fds actifs (serveur + clients connectes)
fd_set wfds;    					// copie tmp de afds, passee a select() pour tester qui peut recevoir un send() sans bloquer (ECRITURE)
fd_set rfds;    					// copie tmp de afds, passee a select() pour tester qui a des donnees a lire (LECTURE)
char *clts_recv_buf[1024] = {0};	// par client : donnees recues, en attente d'un '\n' complet
char *clts_send_buf[1024] = {0};   	// par client : donnees a envoyer, en attente que send() les accepte
char recv_buf[1000001] = {0};		// buff tmp qui ser a stocker ce que recv() vient de lire	
char send_buf[1000001] = {0};		// buf tmp qui sert a construire le msg a broadcaster, avant send()
// recv_buf et send_buf
//   - taille 1 000 000 caracteres + 1 pour le '\0'
//   - variables globales, taille fixe connue a la compilation
//   - pas de malloc/free -> pas de leak possible

//* ----------------------------------------------------------------------------
//* (OK) extract_message
//* ----------------------------------------------------------------------------
// Role : chercher un '\n' dans *buf. Si trouve, decouper le buffer en deux :
//   - *msg : un message complet (tout ce qui precede le '\n' + '\n')
//   - *buf : tout ce qui reste dans le buffer apres le premier '\n' trouve
// Retour :
// 	 1 -> un msg complet a ete extrait
//   0 -> pas de '\n' trouve (message incomplet, il faut recv() encore)
//  -1 -> erreur d'allocation
int extract_message(char **buf, char **msg)
{
	//& 1. declaration des variables
	char	*newbuf;
	int		i;
	*msg = 0;	// par defaut, pas de msg extrait

	//& 2. check *buf vide
	if (*buf == 0)
		return (0);
	
	//& 3. boucle de lecture du buf
	i = 0;
	while ((*buf)[i])
	{
		//& 1. si fin de la ligne trouvee
		if ((*buf)[i] == '\n')
		{
			//& 1. allouer un newbuff pour extraire le message
			// calloc -> newbuf se termine avec '\0'
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				return (-1); // echec d'allocation
			
			//& 2. copier dans newbuf la reste de buf (apres la premiere '\n')
			strcpy(newbuf, *buf + i + 1);

			//& 3. extraire la ligne dans msg
			// pointer vers buf et ajoute un 0 apres la premiere '\n'
			// pour tronquer la chaine)
			*msg = *buf;
			(*msg)[i + 1] = 0; // 0 == '\0'
			
			//& 4. mettre a jour buf
			*buf = newbuf;
			return (1);
		}
		//& 2. si pas de fin de ligne trouvee, passer au char suivant
		i++;
	}

	//& 4. pas de ligne complete, en attente d'autre send()
	return (0);
}

//* ----------------------------------------------------------------------------
//* (OK) strjoin
//* ----------------------------------------------------------------------------
// ajoute 'add' dans 'buf'
//   - buf : buffer contenant les caracteres deja recus
//   - add : conentant les caracteres recues via send()
//   - newbuf : cumul de buf et add
//  Return
//     0 -> erreur d'allocation
//     newbuf -> buffer contenant buf et add
// char *strcat(char *dest, const char *src);
// he  strcat() function appends the src string to the dest string, over‐
// writing the terminating null byte ('\0') at the end of dest,  and  then
// adds  a  terminating  null  byte.
char *str_join(char *buf, char *add)
{
	//& 1. declaration des variables
	char	*newbuf;
	int		len;

	//& 2. calculer len
	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	
	//& 3. allouer newnuf (buf + add)
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	
	//& 4. erreur d'allocation
	if (newbuf == 0)
		return (0);
	
	//& 5. copier buf dans newbuf
	newbuf[0] = 0; // vider newbuf
	if (buf != 0)
		strcat(newbuf, buf);
	
	//& 6. liberer buf
	free(buf);
	
	//& 7. ajouter 'add' dans 'newbuf'
	strcat(newbuf, add);
	
	//& 8. retourner newbuf (a la palce de buf)
	return (newbuf);
}


int main() {
	int sockfd, connfd, len;
	struct sockaddr_in servaddr, cli; 

	// socket create and verification 
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1) { 
		printf("socket creation failed...\n"); 
		exit(0); 
	} 
	else
		printf("Socket successfully created..\n"); 
	bzero(&servaddr, sizeof(servaddr)); 

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(8081); 
  
	// Binding newly created socket to given IP and verification 
	if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) { 
		printf("socket bind failed...\n"); 
		exit(0); 
	} 
	else
		printf("Socket successfully binded..\n");
	if (listen(sockfd, 10) != 0) {
		printf("cannot listen\n"); 
		exit(0); 
	}
	len = sizeof(cli);
	connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
	if (connfd < 0) { 
        printf("server acccept failed...\n"); 
        exit(0); 
    } 
    else
        printf("server acccept the client...\n");
}