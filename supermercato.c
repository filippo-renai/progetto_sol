#include "./libreria/mylib.h" //includo la mia libreria

//struct della configurazione
static config *c; 

//socket che comunicherà con il direttore
static int sfd; 

//il pid del direttore mi servirà per inviargli i segnali
static int dirPid=-10;

//variabili per i segnali
static volatile sig_atomic_t sighup=0;
static volatile sig_atomic_t sigquit=0;
static volatile sig_atomic_t uscitaZeroProdotti=0;

//lock usate per far si che le richieste di uscita dei clienti che hanno zero clienti siano singole
static pthread_mutex_t uscitaZeroProdottiLock = PTHREAD_MUTEX_INITIALIZER;

//array di threads per i clienti ed i cassieri
static pthread_t *clientiTh;
static pthread_t *casse;

//variabile atomica che se = 1 vuol dire che il resp. cassier. deve comunicare con i direttore
static atomic_int informa = 0;

//variabile atomica usata per le entrate contingentate
static atomic_int clientiUsciti=0;

//variabile atomica riguardante i clienti attuali utile nel caso di chiusura sighup
static atomic_int clientiAtt=0;

//variabile atomica utilizzata per far chiudere le casse e il loro responsabile nel caso sighup
static atomic_int varCass = 0; 

//variabile atomica totale dei clienti serviti dal supermercato
static atomic_int clientiTot=0;

//variabile atomica numero di prodotti comprati e relativa lock
static atomic_int prodottiTot=0;	

//variabile atomica numero dei clienti usciti con zero prodotti
static atomic_int clientiZero = 0;

//matrix dei clienti che entreranno in coda
static clienteCoda **cl;

//Array di casse con code di clienti
static cassa *casseCode; 

/* file log dove verrano registrate tutte le statistiche durante l'apertura
   e la sua relativa lock,visto che più thread posso scrivere dentro il file */
static FILE *fileLog;
static pthread_mutex_t fileLogLock = PTHREAD_MUTEX_INITIALIZER;


/* Array che raccoglie lo status di ogni singola cassa, che sarà inviato al direttore ogni volta che una cassa
   deve notificare:casseStatus[i] = -1 -> la cassa è chiusa, casseStatus[i] = num -> la cassa ha num clienti in coda */
static int *casseStatus;
static pthread_mutex_t casseStatusLock = PTHREAD_MUTEX_INITIALIZER;




//gestore dei segnali
static void handler (int sig) {

	//chiusura immediata del supermercato
	if (sig==SIGQUIT) sigquit = 1; 
	//non posso entrare nuovi clienti
	if (sig==SIGHUP)  sighup = 1; 
	//il direttore da il consenso ai clienti con zero prodotti di uscire
	if (sig==SIGUSR1) uscitaZeroProdotti = 1;

}

//thread che simula le operazioni del cliente
void *cliente (void *arg) {
	int id=*(int*)arg,numcasse=0,randomCassa=1,idCassa=-1; 
	float ttot=0,tcoda = 0;
	struct timespec startAlg={0,0},endAlg={0,0},start={0,0},end={0,0},startCas={0,0}; 
	double tempoCambio = (double) c->S;

	//inizio timer per il tempo totale
	clock_gettime(CLOCK_REALTIME, &start);

	//Simulazione spesa
	unsigned int seme =  (unsigned int) id; //così il random dipende dall'id del cliente quindi è diversificato
	unsigned int prodAcq = rand_r(&seme) % (c->P+1); //prodotti acquistati [0,P] (P = massimo dei prodotti)
	unsigned int tempAcq = (rand_r(&seme) % (c->T-10+1)) + 10; //tempo di acquisto [10,T]
	struct timespec tempo = { 0, tempAcq*1000000 };
	nanosleep(&tempo, NULL); 

	//caratteristiche del cliente che entrerà nella coda
	(cl[id])->id=id;
	(cl[id])->numprod=prodAcq;
	(cl[id])->servito=0; 
	
	clientiAtt++;
	
	//se non ha acquistato nessun prodotto non entra nemmeno in una coda
	if (prodAcq == 0 && !sigquit) {
		
		//chiede il permesso di uscire al direttore,quando verrà informato puo' uscire
		pthread_mutex_lock(&uscitaZeroProdottiLock);
		kill(dirPid,SIGUSR1);
		while(!uscitaZeroProdotti);
		uscitaZeroProdotti=0;
		pthread_mutex_unlock(&uscitaZeroProdottiLock);
		

		//Calcolo tempo e scrivo i dati nel fileLog
		clock_gettime(CLOCK_REALTIME, &end);
		ttot= (timedifference_msec(start, end))/1000;

		clientiZero++;
		clientiTot++;
		clientiUsciti++;
		clientiAtt--;
		//caso sighup il supermercato ha servito tutti i clienti, si puo' chiudere
		if(clientiAtt == 0 && sighup) varCass = CODCHIUSURA;

		//scrivo nel file log il cliente con le sue caratteristiche
		pthread_mutex_lock(&fileLogLock);
		fprintf(fileLog,"Cliente | id:%d | n. prodotti acquistati:%d | tempo totale nel super.:%0.3f s | tempo tot. speso in coda:%0.3f s | n. di code visitate:%d |\n", (id+1), prodAcq,ttot,tcoda, numcasse);
		fflush(fileLog);
		pthread_mutex_unlock(&fileLogLock);
		
	}
	
	else if(prodAcq>0 && !sigquit){ 
		
		//scelta della cassa tra quelle aperte, se con l'algoritmo la più conveniente altrimenti random
		while (1) {
		nuovacassa:
			if(randomCassa){ //mandiera randomica 
				idCassa = rand_r(&seme) % c->K;
				if (!casseCode[idCassa].open) continue;		
				break;
			}
			else{ //scelta con l'algoritmo		
				if (!casseCode[idCassa].open) { //cassa chiusa scelgo nuovamente in maniera randomica
					randomCassa=1;
					continue;
				}
				break;	
			}
		}

		//supermercato non chiuso
		if (!sigquit) { 

			//faccio entrare il cliente nella coda delle cassa idCassa
			if (push(casseCode[idCassa].codaClienti, cl[id]) == -1) {
				fprintf(stderr, "Errore: entrata del cliente:#%d nella coda\n", id);
				return (void*)0;
			}

			//Inizia a contare il tempo che il cliente sta nella cassa
			clock_gettime(CLOCK_REALTIME, &startCas);
			//inizio il timer per vedere se devo cambiare cassa
			clock_gettime(CLOCK_REALTIME, &startAlg);

			numcasse++;

			while (1) {

				//vedo se il cliente è stato servito o è chiuso il supermercato,se si esco 
				if(sigquit || (cl[id])->servito) break;
														
				//cassa non più aperta devo cambiare cassa
				if (!casseCode[idCassa].open){
					randomCassa=1;
					goto nuovacassa; 
				}

				//controllo se è passato il tempo per verificare l'algorimo di cambio cassa
				clock_gettime(CLOCK_REALTIME, &endAlg); 
				if(timedifference_msec(startAlg, endAlg) >= tempoCambio){
					/* ALGORITMO */ 

					//devo cambiare cassa ,se possibile essendoci molte persone in coda
					if(primaCliente(casseCode[idCassa].codaClienti,id) >= c->F){
	
						//cerco,esclusa la cassa attuale, la cassa con meno clienti
						int min = 100,nuova=idCassa;
						for(int i=0;i<c->K;i++){
							//la cassa che controllo è diversa dalla cassa attuale
							if(i != idCassa){
								//controllo che la cassa sia aperta
								if(casseCode[i].open){
									int dimC = dimensione(casseCode[i].codaClienti);
									//questa cassa ha meno coda delle altre
									if(min > dimC){
										min = dimC;
										nuova = i;
										//sicuramente almeno una delle casse più convenienti dove andare
										if(min == 0 )break;
									}
								}
							}
						}

						// ora controllo che la cassa con meno clienti,ne ha meno delle persone prima del cliente e
						// se il cliente non è uscito dalla cassa
						int prima = primaCliente(casseCode[idCassa].codaClienti,id);
						if((min < prima) && prima != -1){
								
							//controllo nel fra tempo se il supermercato chiude, il cliente è stato servito o se la coda ha chiuso
							if((cl[id])->servito || sigquit) break;
							if (!casseCode[idCassa].open){
								randomCassa=1;
								goto nuovacassa; 
							}

							//cliente esce dalla coda, pronto ad entrare nella cassa più vantagiosa attraverso un goto
							if(uscitaCassa(casseCode[idCassa].codaClienti,id)){
								randomCassa = -1;
								idCassa = nuova;
								goto nuovacassa;
							}
						}
					}
					
					//sono rimasto sempre nella stessa cassa quindi devo inziare il timer dell'algoritmo
					clock_gettime(CLOCK_REALTIME, &startAlg);
				} 	
			}			
		} 
		
		
		//cliente sicuramente servito
		if((cl[id])->servito){  
			
			clock_gettime(CLOCK_REALTIME, &end);
			ttot=(timedifference_msec(start, end))/1000;
			tcoda=(timedifference_msec(startCas, end))/1000;
			
			clientiTot++;
			clientiUsciti++;	
			clientiAtt--;
			//caso sighup il supermercato ha servito tutti i clienti, si puo' chiudere
			if(clientiAtt == 0 && sighup) varCass = CODCHIUSURA;
			
			//scrivo nel file log il cliente con le sue caratteristiche
			pthread_mutex_lock(&fileLogLock);
			fprintf(fileLog,"Cliente | id:%d | n. prodotti acquistati:%d | tempo totale nel super.:%0.3f s | tempo tot. speso in coda:%0.3f s | n. di code visitate:%d |\n", (id+1), prodAcq,ttot,tcoda, numcasse);
			fflush(fileLog);
			pthread_mutex_unlock(&fileLogLock);
		} 
		
	}
	
	return (void*)0;
}

//thread che gestisce l'entrata contingentata dei clienti,in base ai clienti dentro il supermercato
void *responsabileClienti (void *arg) {
	//clienti totali in questo momento e l'array degli indici
	int *id,i; 
	int clientiTotOra = c->C;

	//Creo un array di C thread clienti,che rappresenta tutti i clienti dentro il supermercato al momento
	if ((clientiTh=(pthread_t*)malloc(CLIENTIMAX*sizeof(pthread_t))) == NULL) {
		perror("Errore: creazione array dei thread clienti");
		return (void*)0;
	}

	//creao un array bid. del clienti che entreranno nella coda ed alloca ogni elemento
	if((cl=(clienteCoda**) malloc(CLIENTIMAX*sizeof(clienteCoda*))) == NULL){
		perror("Errore: creazione array bid. clienteCoda");
		return (void*)0;
	}

	//Creo un array di k indici da passare come argomento ai thread
	if ((id=(int*) malloc(CLIENTIMAX*sizeof(int))) == NULL) {
		perror("Errore: creazione array di indici");
		return (void*)0;
	}

	//ciclo per velocizzare il processo d'entrata dei nuovi clienti
	for(int j=0;j<CLIENTIMAX;j++){
		cl[j] =(clienteCoda*) malloc(sizeof(clienteCoda));
		id[j] = j;
	}

	//creo i vari thread come argomento li passiamo i, che equivale all'identificativo del cliente
	for (i=0; i<c->C; i++) {
		if (pthread_create(&clientiTh[i], NULL, cliente, &id[i]) != 0) {
			perror("Errore: creazione thread cliente");
			return (void*)0;
		}
	}

	//faccio entrare i clienti finchè non ho un segnale di chiusura
	while (!sighup && !sigquit) {

		//se la condizione è verificata devo far entrare altri c->E clienti
		if(clientiUsciti >= c->E){
			clientiUsciti = 0;
			clientiTotOra += c->E;
			
			//supero i clienti massimi,errore
			if(clientiTotOra > CLIENTIMAX){
				perror("Errore: clienti che vogliono accedere più dei clienti massimi che possono accedere, alzare il valore della define nel .h");
				return (void*)0;
			}

			for (int k=0; k<c->E; k++,i++) {
				if (pthread_create(&clientiTh[i], NULL, cliente, &id[i]) != 0) {
					perror("Errore: creazione thread cliente");
					return (void*)0;
				}
			}		
		}
	}

	//aspetto tutti i clienti
	for (int i=0; i<clientiTotOra; i++) {
		if (pthread_join(clientiTh[i], NULL) != 0) {
			perror("Error waiting for thread customer");
			return (void*)0;
		}
	}

	//libero l'array degli indici,le altre cose le libero nel main
	free(id);
	return (void*)0;
}

//thred che simula le operazione di una cassa
void *cassiere (void *arg) {
	int id = *(int*)arg,nProd=0, nClienti=0, apertaPrima=0, nChiusure=0;
	float tmedserv=0, ttotserv=0, tChius=0; 
	struct timespec startNot={0,0},endNot={0,0},start={0,0},end={0,0},startCh={0,0},endCh={0,0},startServ={0,0},endServ={0,0}; 
	double tempoNotifica = (double) c->notifDir; //notifica del direttore
	unsigned int seed=(unsigned int) id,tServ = (rand_r(&seed) % (80-20+1)) + 20; //tempo servizio nel range delle specifiche [20-80]

	//Inizio a contare il tempo per sapere il tempo totale del supermercato
	clock_gettime(CLOCK_REALTIME, &start);
	//inizio a contare il tempo per la notifica al responsabile direttore che comunica con il direttore
	clock_gettime(CLOCK_REALTIME, &startNot);
	
	//esce dal ciclo nei due casi dei segnali
	while (varCass != CODCHIUSURA && !sigquit) {

		//cassa non è aperta
		if (!casseCode[id].open && !sigquit && varCass != CODCHIUSURA) {
			//Calcolo il tempo della chiusura,ci servirà poi per il calcolo del tempo totale di apertura
			clock_gettime(CLOCK_REALTIME, &startCh);

			//se la cassa è stata aparta in precedenza devo pulire la coda per la nuova riapertura
			if (apertaPrima) {
				nChiusure++;
				cleanCoda(casseCode[id].codaClienti);
			}

			//aspetto di essere svegliato dal responsabile delle casse
			pthread_mutex_lock(&casseCode[id].openLock);
		 	while(!casseCode[id].open)
				pthread_cond_wait(&casseCode[id].openCond, &casseCode[id].openLock);
			pthread_mutex_unlock(&casseCode[id].openLock);
			
			//Calcolo tempo2, trovo differenza e sommo al tempo di chiusura
			clock_gettime(CLOCK_REALTIME, &endCh);
			tChius+=timedifference_msec(startCh, endCh);
			//inizio a contare il tempo per la notifica, appena la cassa sarà riaperta
			clock_gettime(CLOCK_REALTIME, &startNot);
		}
		apertaPrima=1;

		//vedere se notificare la situazione della cassa al resp. delle casse che comunica con il direttore
		clock_gettime(CLOCK_REALTIME, &endNot);
		if((timedifference_msec(startNot, endNot) >= tempoNotifica) && !sigquit && varCass != CODCHIUSURA && casseCode[id].open){
			informa = 1;
			clock_gettime(CLOCK_REALTIME, &startNot);
		}

		//se la cassa è aperta il super. non deve chiudere e ci sono clienti in coda servo il primo della lista
		if (dimensione(casseCode[id].codaClienti)>0 && casseCode[id].open && !sigquit && varCass != CODCHIUSURA) {

			clienteCoda *cl=pop(casseCode[id].codaClienti); //prelievo il primo della coda
			if(cl != NULL){
				if(!cl->servito && !sigquit && varCass != CODCHIUSURA){ //condizione non necessaria solo per maggior sicurezza
					nProd+=cl->numprod;
					nClienti++;

					//la cassa serve il cliente attuale
					clock_gettime(CLOCK_REALTIME, &startServ);
					struct timespec servtime = { 0, (tServ+(c->Z*cl->numprod))*1000000 };
					nanosleep(&servtime, NULL);
					clock_gettime(CLOCK_REALTIME, &endServ);

					//calcolo i relativi tempi
					ttotserv+=timedifference_msec(startServ, endServ);
					tmedserv=(ttotserv)/nClienti;

					//informo che il cliente è stato servito 
					if(cl != NULL) cl->servito=1;
				}
			}

			//scrivo la situazione della cassa nell'array casseStatus
			pthread_mutex_lock(&casseStatusLock);
			casseStatus[id] = dimensione(casseCode[id].codaClienti);
			pthread_mutex_unlock(&casseStatusLock);	
		}
	}

	prodottiTot += nProd;

	//Calcolo tempo finale
	clock_gettime(CLOCK_REALTIME, &end);
	float ttotopen=timedifference_msec(start, end)-tChius;
	if (ttotopen<0) ttotopen=0;

	//scrivo le statistiche della cassa nel file
	pthread_mutex_lock(&fileLogLock);
	fprintf(fileLog, "CASSA | id:%d | n. prodotti elaborati:%d | n. di clienti:%d | tempo tot. di apertura:%.3f s | tempo medio di servizio:%.3f s | n. di chiusure:%d |\n", (id+1), nProd, nClienti, (ttotopen/1000), (tmedserv/1000), nChiusure);
	fflush(fileLog);
	pthread_mutex_unlock(&fileLogLock);

	return (void*)0;
}

//thread che gestisce l'apertura e la chiusura delle varie casse,attraverso la comunicazione con il direttore
void *responsabileCasse (void *arg) {
	//casse aperte all'inizio del supermercato e un array di indici
	int casseAperte=c->casseIniz,*id;

	//Creo un array thread di K thread cassa
	if ((casse=malloc(c->K*sizeof(pthread_t))) == NULL) {
		perror("Errore: creazione threads di casse");
		return (void*)0;
	}

	//Creo un array di k indici da passare come argomento ai thread
	if ((id=malloc(c->K*sizeof(int))) == NULL) {
		perror("Errore: creazione array di indici");
		return (void*)0;
	}

	//Creo un array di k casse (ogni cassa ha la sua coda)
	if ((casseCode=malloc(c->K*sizeof(cassa))) == NULL) {
		perror("Errore: crezione array di casse");
		return (void*)0;
	}

	//Crea un array che rappresenta la status delle k casse da inviare al direttore
	if ((casseStatus=malloc(c->K*sizeof(int))) == NULL) {
		perror("Errore: crezione array di casse status");
		return (void*)0;
	}

	//inizializzo tutto sulle casse
	for (int i=0; i<c->K; i++) {
		id[i]=i; //array utili per gli indici delle casse
		casseCode[i].codaClienti=inCoda();
		if (i<casseAperte) { //cassa aperta all'apertura del supermercato
			casseCode[i].open=1;
			casseStatus[i] = 0; //cassa aperta con zero clienti
		}
		else{ //casse chiuse inizialmente
			casseCode[i].open=0;
			casseStatus[i] = -1; //cassa chiusa
		}
		if (pthread_mutex_init(&casseCode[i].openLock, NULL) != 0) {
			fprintf(stderr, "Errore: inizializzazione openLock della coda della cassa:%d\n", i);
			return (void*)0;
		}
		if (pthread_cond_init(&casseCode[i].openCond, NULL) != 0) {
			fprintf(stderr, "Errore: inizializzazione openCond della coda della cassa:%d\n", i);
			return (void*)0;
		}
	}

	//starto tutti i thread cassiere
	for(int i=0;i<c->K;i++){
		if (pthread_create(&casse[i], NULL, cassiere, &id[i]) != 0) {
			fprintf(stderr, "Errore: creazione thread cassa #%d\n", i);
			return (void*)0;
		}
	} 

	//cassa aperte ora posso iniziare a far entrare i clienti
	pthread_t respCli;
	if (pthread_create(&respCli, NULL, responsabileClienti, NULL) != 0) {
		perror("Errore: creazione thread responsabile dei clienti");
		return (void*)0;
	}

	//finchè non chiudo il supermercato verifico se devo chiudere e aprire casse,attraverso la comunicazione con il direttore
	while (varCass != CODCHIUSURA && !sigquit) {
		
		//aspetto finchè una cassa deve inviare una informare il direttore
		if(informa){
			informa = 0;

		//COMUNICAZIONE TRA IL SUPERMERCATO ED IL DIRETTORE
			int n,richiesta = CODCASSA,nov;
			//invio al direttore la situazione delle casse,e aspetto risposta
			MENO1(n, writen(sfd, &richiesta, sizeof(int)),"Errore: scrittura al direttore");
			pthread_mutex_lock(&casseStatusLock);
			MENO1(n, writen(sfd, casseStatus, (sizeof(int)*c->K)),"Errore: scrittura al direttore");
			pthread_mutex_unlock(&casseStatusLock);
			MENO1(n, readn(sfd, &nov, sizeof(int)), "Errore: lettura della risposta al direttore");

		//VEDERE SE DEVO CHIUDERE O APRIRE UNA CASSA
			if (nov==-1 && casseAperte>1) { //devo chiudere una cassa	
				int cassa=-1;
				// chiudo una cassa che ha meno clienti,ovviamente se trovo subito una cassa con zero clienti
				// la ricerca è finita,sicuramente ci sono due casse che hanno al più un cliente
				for(int i=0;i<c->K;i++){
					if (casseCode[i].open) { //cassa aperta
						if(dimensione(casseCode[i].codaClienti) == 0){ //cassa da chiudere zero clienti
							casseCode[i].open=0;
							pthread_mutex_lock(&casseStatusLock);
							casseStatus[i] = -1; //anche l'array che invio al direttore viene aggiornato
							pthread_mutex_unlock(&casseStatusLock);
							cassa=-1;
							casseAperte--;
							pthread_mutex_unlock(&casseCode[i].openLock);
							break;
						}
						if(dimensione(casseCode[i].codaClienti) == 1) cassa=i;
					}	
				}
			
				//non ci sono casse con zero clienti devo chiudere uno con 1 cliente
				if (cassa!=-1){
					casseAperte--;
					casseCode[cassa].open=0;		
					pthread_mutex_lock(&casseStatusLock);
					casseStatus[cassa] = -1; //anche l'array che invio al direttore viene aggiornato
					pthread_mutex_unlock(&casseStatusLock);
				}
			
			}

			if (nov==1 && casseAperte < c->K){ //devo aprire una cassa	
				//apro la prima cassa chiusa
				for(int i=0;i<c->K;i++){
					if(!casseCode[i].open){ //apro la cassa
						casseAperte++;
						pthread_mutex_lock(&casseStatusLock);
						casseStatus[i] = 0; //anche l'array che invio al direttore viene aggiornato
						pthread_mutex_unlock(&casseStatusLock);

						pthread_mutex_lock(&casseCode[i].openLock);
						casseCode[i].open=1;
						pthread_cond_signal(&casseCode[i].openCond);
						pthread_mutex_unlock(&casseCode[i].openLock);
						break;
					}
				}		
			}
		}
	}
	
	//devo chiudere il supermercato,quindi sveglio le casse chiuse per dirle di uscire
	for (int i=0; i<c->K;i++){
		pthread_mutex_lock(&casseCode[i].openLock);
		casseCode[i].open = 1;
		pthread_cond_signal(&casseCode[i].openCond);
		pthread_mutex_unlock(&casseCode[i].openLock);
	}

	//aspetto tutte le casse
	for (int i=0; i<c->K;i++) {
		if (pthread_join(casse[i], NULL) != 0) {
			fprintf(stderr, "Error waiting for thread checkout #%d\n", i);
			return (void*)0;
		}
	} 

	//aspetto il responsabile dei clienti
	if (pthread_join(respCli, NULL) != 0) {
		perror("Errore: attesa del thread responsabile delle clienti");
		return (void*)0;
	}

	//libero l'array degli indici,le altre cose le libero nel main
	free(id);
	return (void*)0;
}

int main(int argc, char *argv[]) {

   //apro e ricavo le informazioni sui valori della configurazione
   if ((c=inconfig(argv[1])) == NULL ) {
      perror("Errore: nella lettura del file di configurazioni");
      return EXIT_FAILURE;
   }

	//apro il logfile in scrittura, codi che i thread possano scriverci
	if ((fileLog=fopen(FILELOG, "w")) == NULL) {
		perror("Errore: creazione del file log in modalità scrittura");
		return EXIT_FAILURE;
	}

//SETTARE TUTTO PER I SEGNALI
	//Gestisco i segnali SIGHUP e SIGQUIT, reindirizzati sull'handler
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
	sa.sa_flags=SA_RESTART;
	//Reindirizzo i segnali sull'handler
	MENO1(err, sigaction(SIGHUP, &sa, NULL), "Errore: redirecting SIGHUP to handler");
	MENO1(err, sigaction(SIGQUIT, &sa, NULL), "Errore: redirecting SIGQUIT to handler");
	MENO1(err, sigaction(SIGUSR1, &sa, NULL), "Errore: redirecting SIGUSR1 to handler");

//SETTARE TUTTO PER LA CONNESSIONE CON IL SUPERMERCATO
	//Creo il socket
	MENO1(sfd, socket(AF_UNIX, SOCK_STREAM, 0), "Errore: creazione della socket (supermercato)");
	//Setto l'indirizzo
	struct sockaddr_un serv_addr;
	memset(&serv_addr, '0', sizeof(serv_addr));
	serv_addr.sun_family=AF_UNIX;
	strncpy(serv_addr.sun_path, SOCKNAME, strlen(SOCKNAME)+1);
	//Connetto il socket e l'indirizzo
	int unused;
	MENO1(unused, connect(sfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), "Errore: connessione socket (supermercato)");

//COMUNICAZIONE CON IL DIRETTORE
	//mando il pid del supermercato al direttore, che manda il suo, così potranno mandarsi i segnali
	int n,risposta,richiesta = (int) getpid();
	MENO1(n,writen(sfd, &richiesta, sizeof(int)),"Errore: scrittora al direttore");
	MENO1(n, readn(sfd, &risposta, sizeof(int)), "Errore: lettura della richiesta del supermercato");
	dirPid = risposta;

	//starto e aspetto i responsabili delle casse
	pthread_t respCa;
	if (pthread_create(&respCa, NULL, responsabileCasse, NULL) != 0) {
		perror("Errore: creazione thread responsabile delle casse");
		return EXIT_FAILURE;
	}
	if (pthread_join(respCa, NULL) != 0) {
		perror("Errore: attesa del thread responsabile delle casse");
		return EXIT_FAILURE;
	} 

	//stampo il numero di clienti totali entrati nel file
	int totC = (int) clientiTot,totP = (int) prodottiTot,totZ = (int) clientiZero;
	fprintf(fileLog, "\nQuantità clienti entrati totali = %d\nQuantità clienti che non hanno acquistato prodotti = %d\nQuantità prodotti comprati totali= %d\n", totC, totZ,totP);
	fflush(fileLog);

	//chiudo il supermercato
	richiesta = CODCHIUSURA;
	MENO1(n,writen(sfd, &richiesta, sizeof(int)),"Errore: scrittora al direttore");
	printf("SUPERMERCATO CHIUSO\n");

	//liberazione memoria

	//riguardante le casse
	for(int i=0;i<c->K;i++){
		pthread_mutex_destroy(&casseCode[i].openLock);
		pthread_cond_destroy(&casseCode[i].openCond);
		freeCoda(casseCode[i].codaClienti);
	}
	free(casse);
	free(casseCode);
	free(casseStatus); 

	//riguardante i clienti
	for(int i=0; i<CLIENTIMAX;i++)
		free(cl[i]);
	free(cl);
	free(clientiTh); 

	close(sfd);
	fclose(fileLog);
	free(c);

   return 0;
}
