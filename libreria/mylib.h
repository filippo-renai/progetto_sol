//File .h contentente tutte le strutture dati,macro,prototipi di funzioni utili al progetto
#if !defined(MYLIB_H_)
#define MYLIB_H_

//include utili al progetto
#define _POSIX_C_SOURCE 200809L 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdatomic.h>

//define 
#define CODCASSA -15 //utili per far capire al direttore il tipo di richiesta
#define CODCHIUSURA -10 //utili per far capire al direttore il tipo di richiesta ed altre funzioni
#define CLIENTIMAX 1000 //massimo dei clienti che posso entrare nel supermercato
#define FILELOG "./logFile.log" //file log dove segniamo le statistiche
#define BUFSIZE 128
//socket utile per la comunicazione
#if !defined(SOCKNAME)
    #define SOCKNAME "./sock"
#endif
//usato per fare i controlli delle chiamate di funzioni
#define MENO1(r,c,e) if((r=c)==-1) { perror(e); exit(errno); } 


/* STRUCT PROGETTO */

typedef struct config {
	int K; 	//numero di casse nel supermercato
	int C; 	//numero massimo di clienti dentro il supermercato
	int E; 	//numero di clienti che devono uscire prima di farne entrare altri di uguale quantità
	int T; 	//tempo massimo per i clienti per fare acquisti - T>10 msec
	int P; 	//massimo numero di prodotti che un cliente puo' comprare
   int S;   //tempo in cui un cliente puo' decidere di cambiare cassa
   int F;   //il cliente cambia cassa se ci sono almeno F persone prima in cassa
	int Z; 	//tempo che ci mette un cassiere per passare un prodotto
	int S1;  //numero di casse con al più un cliente in coda
	int S2;  //numero di clienti in coda in almeno una cassa
	int casseIniz; //quante casse sono aperte all'apertura del supermercato
	int notifDir;  //ogni quanto tempo i cassieri devono notificare il direttore sul numero di clienti in coda
} config;

typedef struct clienteCoda {
	int id; //identificativo
	int numprod; //prodotti acquistati dal cliente
	atomic_int servito; //servito = 0 -> cliente non servito dalla cassa, servito = 1 -> cliente servito dalla cassa (var. atomica)
} clienteCoda;

typedef struct node {
	clienteCoda* info; //elemento del nodo
	struct node *next; //Puntatore al nodo successivo nella coda
} node;

typedef struct coda {
	int size; //dimensione della coda
	struct node *first; //puntatore a primo elemento della coda
	pthread_mutex_t lock; //semaforo di mutex interno alla coda
	pthread_cond_t cond;	//variabile di condizione interna alla coda
} coda;

typedef struct cassa {
	atomic_int open;
	coda *codaClienti;	//Non serve nessun lock sulla coda perche' l'implementazione di questa coda prevede gia l'uso di lock
	pthread_mutex_t openLock;
	pthread_cond_t openCond; //utilizzata per far 'dormire' le casse chiuse
} cassa;

/* PROTOTIPI DI FUNZIONI */

/* CONFIGURAZIONE */

/* dato il file di configurazione 'riempio' i campi della struct config e ritorno essa
   valore di ritorno diverso da NULL nessun errore,NULL altrimenti */
config *inconfig (char*confile);

/* controllo che la lettura del file sia andata apposto
   1 se il controllo è andato a buon fine,-1 altrimenti */
int checkconfig(config *c);

/* CASSA */

/* inizializza e ritorna la coda
   valore di ritorno diverso da NULL nessun errore,NULL altrimenti */
coda* inCoda ();

/* inserimento fifo di un elemento elem
   ritorna 0 se è ok,ritorna -1 se c'è stato un errore */
int push (coda *q, clienteCoda *elem);

/* rimuove la testa
   ritorna il primo elemento della coda,altrimenti NULL se coda e' vuota */
clienteCoda* pop(coda*q);

/* il cliente esce dalla cassa prima di essere servito restituisce 1
	il cliente è il primo della fila o non è presente restituisce -1 */
int uscitaCassa(coda *q, int id);

//controllo quanti clienti sono prima del cliente,se non è presente restituisce -1
int primaCliente(coda *q,int id);

//Ritorna la lunghezza della coda oppure -1 se c'è stato un errore
int dimensione (coda *q);

//liberazione della memoria della coda e quindi sua eliminazione
void freeCoda (coda*q);

//ripulisce la coda dai tutti i suoi nodi
void cleanCoda(coda *q);

/* SOCKET */

/* read safe da una socket
   restituisce 0 se c'è stato un problema di connessione,-1 errore,corretto altrimenti */
int readn(long fd, int *buf, size_t size);

/* write safe per una socket
   restituisce 0 se c'è stato un problema di connessione,-1 errore,corretto altrimenti */
int writen(long fd, int *buf, size_t size);

/* TEMPO */

//restuisce in millisecondi la differenza di tempo
float timedifference_msec(struct timespec start,struct timespec end);

#endif
