#include "./libreria/mylib.h" //includo la mia libreria


static config *c; //struct della configurazione

static int supPid=-10; //il pid del supermercato mi servirà per inviargli i segnali

//funzione dedicata alla cattura e all'invio dei segnali
static void handler (int sig) {

	//segnali inviati dall'esterno
	if (sig==SIGQUIT) kill(supPid, SIGQUIT); 
	if (sig==SIGHUP)  kill(supPid, SIGHUP); 
	
	//segnale inviato dal supermercato per comunicare che un cliente con zero prodotti vuoli uscire
	if (sig==SIGUSR1) kill(supPid,SIGUSR1);

}

//funzione di pulizia
void cleanup() {
   unlink(SOCKNAME);
}

int main(int argc, char *argv[]) {
	//pulizia per la socket
	cleanup();
	atexit(cleanup);

   //errore negli argomenti passati per linea di comando
   if(argc != 3){
      perror("Errore: argomenti passati per linea di comando");
      return EXIT_FAILURE;
   }

	//apro e ricavo le informazioni sui valori della configurazione
	if ((c=inconfig(argv[1])) == NULL ) {
		perror("Errore: nella lettura del file di configurazioni");
		return EXIT_FAILURE;
	}

//SETTARE TUTTO PER I SEGNALI
	//Gestisco i segnali SIGHUP e SIGQUIT e SIGUSR1, reindirizzati sull'handler
	int err;
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler=handler;
	sigset_t handmask;
	MENO1(err, sigemptyset(&handmask), "Errore: emptying handler mask");
	MENO1(err, sigaddset(&handmask, SIGHUP), "Errore: adding SIGHUP to handler mask");
	MENO1(err, sigaddset(&handmask, SIGQUIT), "Errore: adding SIGQUIT to handler mask");
	MENO1(err, sigaddset(&handmask, SIGUSR1), "Errore: adding SIGUSR1 to handler mask");
	sa.sa_mask=handmask;
	sa.sa_flags=SA_RESTART; //flag utile nel caso degli invii dei segnali
	//Reindirizzo i segnali sull'handler
	MENO1(err, sigaction(SIGHUP, &sa, NULL), "Errore: redirecting SIGHUP to handler");
	MENO1(err, sigaction(SIGQUIT, &sa, NULL), "Errore: redirecting SIGQUIT to handler");
	MENO1(err, sigaction(SIGUSR1, &sa, NULL), "Errore: redirecting SIGUSR1 to handler");
	
//SETTARE TUTTO PER LA CONNESSIONE CON IL SUPERMERCATO
	//Creo il socket
	int sfd;
	MENO1(sfd, socket(AF_UNIX, SOCK_STREAM, 0), "Errore: creazione della socket (direttore)");
	//Setto l'indirizzo
	struct sockaddr_un serv_addr;
	memset(&serv_addr, '0', sizeof(serv_addr));
	serv_addr.sun_family=AF_UNIX;
	strncpy(serv_addr.sun_path, SOCKNAME, strlen(SOCKNAME)+1);
	//Bindo l'indirizzo
	int unused;
	MENO1(unused, bind(sfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), "Errore: binding indice (direttore)");
	//Setta socket in ascolto
	MENO1(unused, listen(sfd, 1), "Errore: listening socket (direttore)");

//STARTARE IL SUPERMERCATO NEL CASO TEST2
   if(argc == 3 && strcmp(argv[2],"2") == 0){
      int pid = fork();
      if(pid == -1){ //Errore
         perror("Errore: nella fork");
         return EXIT_FAILURE;
      }
      if(pid == 0){ //figlio qui posso lanciare il processo supermercato
         execl("./supermercato", "supermercato", argv[1], (char *) NULL);
         perror("Errore: nel lanciare il processo supermercato");
         return EXIT_FAILURE;
      }
   }

//COMUNICAZIONE CON IL SUPERMERCATO
	long afd;
	int n,richiesta,risposta= (int) getpid();
	MENO1(afd, accept(sfd, (struct sockaddr*)NULL, NULL), "Errore: accettazione di una connessione socket (direttore)");

	//il supermercato invia il pid e il direttore invia il suo pid
	MENO1(n, readn(afd, &richiesta, sizeof(int)), "Errore: lettura della richiesta del supermercato");
	supPid = richiesta;
	MENO1(n, writen(afd, &risposta, sizeof(int)), "Errore: invio della risposta al supermercato");

	//ciclo si interrompe quando il supermercato è chiuso
	while(1){
		MENO1(n, readn(afd, &richiesta, sizeof(int)), "Errore: lettura della richiesta del supermercato");
		//supermercato è chiuso posso chiudere il direttore
		if(richiesta == CODCHIUSURA) break;
	
		//il supermercato deve inviare al direttore la situazione delle casse
		if(richiesta == CODCASSA){
			risposta = 0; 
			//le flag utilizzate per non far chiudere o aprire più di una cassa
			int *casse,flagChiu=0,flagApr=0,s1=0;

			//creazione array delle casse
			if ((casse=(int*) malloc(sizeof(int)*c->K)) == NULL) {
				perror("Errore: creazione array delle casse direttore");
				return EXIT_FAILURE;
			}

			MENO1(n, readn(afd, casse, (sizeof(int)*c->K)), "Errore: lettura della richiesta del supermercato");
			
			//controllo se devo chiudere o aprire casse in base a S1,S2
			for(int i=0;i<c->K;i++){
				//potrei chiudere una cassa se s1 è uguale o maggiore a quello imposto dalla configurazione
				if(casse[i] <= 1 && casse[i] != -1 && !flagChiu){		
					s1++;
					if(s1 >= c->S1){ //devi chiudere una cassa
						risposta--;
						flagChiu=1;
					}
				}
				if(casse[i] >= c->S2 && !flagApr){ //devo aprire una cassa
					risposta++;
					flagApr = 1;
				}

				//praticamente non posso fare più altro esco dal ciclo
				if(flagChiu && flagApr) break;
			}

			//risposta 1 devo aprire una cassa, 0 niente, -1 devo chiudere una cassa
			MENO1(n, writen(afd, &risposta, sizeof(int)), "Errore: invio della risposta al supermercato");
			free(casse);
		}
	
	}
	printf("DIRETTORE CHIUSO\n");

	//liberazione di tutto
	close(afd);
	close(sfd);
	free(c);
   return 0;
}
