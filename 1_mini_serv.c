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

// int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
// select() allows a program to monitor multiple file descriptors, waiting
// until one or more of the file descriptors become "ready" for some class
// of I/O operation
// void FD_CLR(int fd, fd_set *set);   -> retire fd de l'ensemble -> met son bit a 0
// int  FD_ISSET(int fd, fd_set *set); -> teste si fd est present -> bit a 1 ou 0
// void FD_SET(int fd, fd_set *set);   -> ajoute fd a l'ensemble -> met son bit a 1
// void FD_ZERO(fd_set *set);          -> vide l'ensemble -> met tous les bits a 0
// FD_SETSIZE -> par defaut vaut 1024 -> ids[1024] & clts_recv_buf[1024]

// fd_set
//   - type dedie (bitset), un bit par fd possible
//   - a manipuler uniquement via les macros ci-dessus, jamais directement
//   - afds = source de verite, modifiee seulement par nous (FD_SET/FD_CLR)
//   - rfds/wfds = copies temporaires de afds, modifiees par select() lui-meme
//     (il retire les fds pas prets), donc a recopier depuis afds a chaque tour

// clts_recv_buf[1024]
//   - le TABLEAU de 1024 pointeurs est global -> stocke dans le BSS
//   - CE VERS QUOI CHAQUE POINTEUR POINTE est alloue dynamiquement (str_join,
//     malloc/calloc) -> stocke sur le tas (heap)
//   - a liberer (free) individuellement pour chaque fd, dans remove_client()
//     (deconnexion normale) ou cleanup() (erreur fatale) -> sinon memory leak

// serv_recv_buf et serv_send_buf
//   - taille 1 000 000 caracteres + 1 pour le '\0'
//   - variables globales, taille fixe connue a la compilation
//   - pas de malloc/free -> pas de leak possible

// REGLE : tableau -> pointeur (array-to-pointer decay)
//   - le NOM d'un tableau, utilise seul dans une expression, est
//     automatiquement converti en un pointeur vers son 1er element
//   - char buf[10]; -> "buf" utilise seul vaut &buf[0], donc un char*
//   - c'est pour ca qu'on peut passer directement send_buf/recv_buf
//     (des tableaux) a des fonctions qui attendent un char*
//     (ex: sprintf, strlen, strcpy, recv, send...)
//   - aucune conversion explicite/cast necessaire, c'est automatique

// sprintf(str, format, ...)
//   - ECRIT DIRECTEMENT en memoire a l'adresse pointee par 'str'
//     (comme strcat/strcpy), ne "retourne" pas un nouveau buffer
//   - ne verifie JAMAIS la taille du buffer de destination
//     -> ici sans risque car send_buf fait 1 000 000 char (tres large)
//     -> mais a savoir : sprintf peut faire un buffer overflow si le
//        texte formate depasse la taille reelle du buffer

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
//char *clts_send_buf[1024] = {0};  // par client : donnees a envoyer, en attente que send() les accepte
char serv_recv_buf[1000001] = {0};		// buff tmp qui ser a stocker ce que recv() vient de lire	
char serv_send_buf[1000001] = {0};		// buf tmp qui sert a construire le msg a broadcaster, avant send()

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

//? ---------------------------------------------------------------------------
//? (NEW) cleanup
//? ---------------------------------------------------------------------------
// Role : en cas d'erreur fatal (juste avant exit(1))
//  - ferme tous les fds actifs (serveur et clients)
//  - libere tous les buffers pour eviter les memory leak
void cleanup()
{
	//& 1. parcourir les fd (de 0 jusqu'a fd_max)
	for (int fd = 0 ; fd <= max_fd ; fd++)
	{
		//& si le fd est actuellement actif
		if (FD_ISSET(fd, &afds))
		{
			//& fermer le fd
			close(fd);

			//& liberer le buffer
			free(clts_recv_buf[fd]);
		}
	}
}

//? ---------------------------------------------------------------------------
//? (NEW) error_exit
//? ---------------------------------------------------------------------------
// Role : en cas d'erreur fatal
//  - appelle cleanup pour liberer les ressources
//  - imprime le message d'erreur sur le stderr
//  - exit avec code 1
void error_exit(char *str) {
	
	//& 1. liberer les ressources (fds et buffers)
	cleanup();

	//& 2. ecrire le message d'erreur sur stderr
	write(2, str, strlen(str));

	//& 3.quitter avec code 1
	exit(1);
}

//? ---------------------------------------------------------------------------
//? (NEW) broadcast_str
//? ---------------------------------------------------------------------------
// Role : envoyer le texte 'str' (deja construit) a tous les fds actifs,
// sauf le socket serveur lui-meme et l'expediteur (sender_fd)
//  - "sender_fd" peut etre un fd client normal (message envoye par un client)
//    ou un fd qui vient d'etre enregistre/retire (arrivee/depart)
//  - FD_ISSET(fd, &wfds) verifie que ce fd est bien pret a recevoir un send()
//    sans bloquer (version simple : pas de gestion du cas "pas pret" pour
//    l'instant, cf. lazy client a ajouter plus tard si le temps le permet)
void broadcast_str(int sender_fd, char *str)
{
	//& 1. boucle pour parcourir les fds actifs
	for (int fd = 0 ; fd <= max_fd ; fd++)
	{
		//& 2. si fd actif (est ce n'est ni le server ni le sender)
		if ( fd != server_fd && fd != sender_fd && FD_ISSET(fd, &wfds))
		{
			//& 3. broadcast le string
			send(fd, str, strlen(str), 0);
		}
	}
}

//? ---------------------------------------------------------------------------
//? (NEW) resgister_client
//? ---------------------------------------------------------------------------
// Role : enregistrer un nouveau client sur le serveur et en informer les
// autres clients deja connectes
//  - met a jour max_fd si necessaire (pour select())
//  - ajoute le fd a l'ensemble des fds actifs (afds)
//  - attribue un id logique unique et croissant (jamais reutilise)
//  - construit et diffuse le message d'arrivee a tous les autres clients
void register_client(int fd)
{
	//& 1. update fd_max
	if (fd > max_fd)
		max_fd = fd;

	//& 2. register client (ajouter dans afds)
	FD_SET(fd, &afds);
	ids[fd] = next_id++;

	//& 3. construire le msg dans send_buf
	sprintf(serv_send_buf, "server: client %d just arrived\n", ids[fd]);

	//& 4. broadcast le msg a tous les autres clients actifs
	broadcast_str(fd, serv_send_buf);
}

//? ---------------------------------------------------------------------------
//? (NEW) remove_client
//? ---------------------------------------------------------------------------
// Role : deconnecter proprement un client et en informer les autres
//  - retire le fd de l'ensemble actif (afds)
//  - ferme le socket (evite un fd leak)
//  - libere son buffer de reception (evite un memory leak)
//  - remet le pointeur a NULL (evite un double-free si ce fd est reutilise
//    plus tard par le systeme pour un nouveau client)
//  - construit et diffuse le message de depart aux autres clients
void remove_client(int fd)
{
	//& 1. desinscrire le client (retirer dans afds)
	FD_CLR(fd, &afds);

	//& 2. nettoyer les ressources
	close(fd);
	free(clts_recv_buf[fd]);
	clts_recv_buf[fd] = NULL;

	//& 3. construire le msg dans send_buf
	sprintf(serv_send_buf, "server: client %d just left\n", ids[fd]);

	//& 4. broadcast le msg a tous les autres clients actifs
	broadcast_str(fd, serv_send_buf);
}

//? ---------------------------------------------------------------------------
//? (NEW) handle_client_message
//? ---------------------------------------------------------------------------
// Role : traiter les donnees recues d'un client
//  - extrait chaque ligne complete disponible dans son buffer de reception
//    (une seule reception peut contenir plusieurs lignes, ou une ligne
//    incomplete qui attend encore un send() suivant)
//  - prefixe chaque ligne avec "client %d: " (une fois par ligne, pas par
//    message entier)
//  - delegue l'envoi reel a broadcast_str
// valeur de retour extract_message :
//   1 -> un msg complet a ete extrait
//   0 -> pas de '\n' trouve (message incomplet, il faut recv() encore)
//  -1 -> erreur d'allocation
void handle_client_msg(int fd)
{
	//& 1. declaration des variables
	char *message;
	int ret = extract_message(&clts_recv_buf[fd], &message);

	//& 2. boucle tant qu'il y a des messages complets a traiter
	while (ret == 1)
	{
		//& 2.1 construire le msg a diffuser
		sprintf(serv_send_buf, "client %d: %s", ids[fd], message);

		//& 2.2 diffuser le msg
		broadcast_str(fd, serv_send_buf);

		//& 2.3 liberer les ressources (message alloue par extract_message)
		free(message);

		//& 2.4 tenter de recuperer la ligne suivante
		ret = extract_message(&clts_recv_buf[fd], &message);
	}

	//& 3. erreur d'allocation
	if (ret == -1)
	{
		error_exit("Fatal error\n");
	}
}

//? ---------------------------------------------------------------------------
//? (NEW) main
//? ---------------------------------------------------------------------------
// Variables locales utilisees :
//  - connfd  : fd du client fraichement accepte, retourne par accept() a
//              chaque nouvelle connexion (different de server_fd, qui lui
//              ne sert qu'a ECOUTER les nouvelles connexions, un seul fixe
//              pendant toute la vie du programme)
//  - len     : socklen_t (pas un int !) attendu par accept(), qui exige un
//              pointeur vers ce type precis pour la taille de la structure
//              d'adresse passee en parametre. Rempli par nous avant l'appel
//              (len = sizeof(cli)), potentiellement mis a jour par accept()
//  - servaddr: adresse du SERVEUR (IP 127.0.0.1 + port donne en argument),
//              remplie par nous, utilisee par bind() pour dire au systeme
//              sur quelle IP/port ecouter
//  - cli     : adresse du CLIENT qui se connecte, remplie automatiquement
//              par accept() (IP/port source du client). Doit exister et
//              etre passee a accept() meme si son contenu n'est jamais
//              utilise ensuite dans ce projet
// int accept(int socket, struct sockaddr *restrict address, 
//				socklen_t *restrict address_len);
// int select(int nfds, fd_set *restrict readfds, fd_set *restrict writefds,
// 				fd_set *restrict errorfds, struct timeval *restrict timeout);
int main(int argc, char **argv)
{
	//& 1. declaration des variables
	// int sockfd; -> variable globale server_fd
	int connfd;
	//int len;
	socklen_t len;
	struct sockaddr_in servaddr;
	struct sockaddr_in cli;

	//& 2. check argc
	if (argc != 2)
		error_exit("Wrong number of arguments\n");

	// socket create and verification 
	FD_ZERO(&afds);
	server_fd = socket(AF_INET, SOCK_STREAM, 0); 
	if (server_fd == -1)
		error_exit("Fatal error\n");
	//else
		//printf("Socket successfully created..\n"); 
	bzero(&servaddr, sizeof(servaddr)); 

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	//servaddr.sin_port = htons(8081);
	int port = atoi(argv[1]);
	servaddr.sin_port = htons(port); 
  
	// Binding newly created socket to given IP and verification 
	if ((bind(server_fd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
		error_exit("Fatal error\n");
	//else
		//printf("Socket successfully binded..\n");
	if (listen(server_fd, 10) != 0)
		error_exit("Fatal error\n");
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
		if (select(max_fd + 1, &rfds, &wfds, NULL, NULL))
			error_exit("Fatal error\n");

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
				// { 
				// 	printf("server acccept failed...\n"); 
				// 	exit(0); 
				// } 
				// else
				// 	printf("server acccept the client...\n");

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
				error_exit("Fatal error\n");

			//& 3.5 traiter les lignes completes
			handle_client_msg(fd);
		}
	}
}

/*

gcc -Wall -Werror -Wextra mini_Serv.c -o mini_serv
./mini_serv -> "wrong number of arguments"
valgrind --leak-check=full ./mini_serv 8080

nc 127.0.0.1 8080
- msg de bienvenue
- envoi msg (chez les autres pas chez moi)
- envoi msg avec plusieurs \n -> une ligne par partie
- fermeture terminal (ctrC ou ctrlD) -> msg client partie
- msg envoye cahr par char sans \n -> broadcast final seulement

*/