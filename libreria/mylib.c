#include "mylib.h"

/* CONFIGURAZIONE */
int checkconfig(config *c) {
	if(c->K!=-1 && c->C!=-1 && c->E!=-1 && c->T!=-1 && c->P!=-1 && c->S!=-1 && c->F!=-1 && c->Z!=-1 &&
		c->S1!=-1 && c->S2!=-1 && c->casseIniz!=-1 && c->notifDir!=-1)
		return 1;
	return -1;
}

config *inconfig (char* confile) {
	FILE *fd=NULL;
	config *conf;
	char *buffer;

	//apertura del file
	if ((fd=fopen(confile, "r")) == NULL) {
		perror("Errore: apertura file di configurazione");
		fclose(fd);
		return NULL;
	}

	//errore nella malloc
	if ((conf=malloc(sizeof(config))) == NULL) {
		perror("Errore: malloc della struct");
		fclose(fd);
		free(conf);
		return NULL;
	}

	//errore nella malloc
	if ((buffer=malloc(BUFSIZE*sizeof(char))) == NULL) {
		perror("Errore: malloc del buffer");
		fclose(fd);
		free(conf);
		free(buffer);
		return NULL;
	}

	int i=0; //i è la riga,ogni riga contiene un diverso valore
	char* backup; //Per memorizzare il vecchio valore del buffer

	//inzializzo i valori con -1 mi servirà per il controllo
	conf->K=-1;
	conf->C=-1;
	conf->E=-1;
	conf->T=-1;
	conf->P=-1;
	conf->S=-1;
	conf->F=-1;
	conf->Z=-1;
	conf->S1=-1;
	conf->S2=-1;
	conf->casseIniz=-1;
	conf->notifDir=-1;

	while (fgets(buffer, BUFSIZE, fd) != NULL) {
		backup=buffer;
		while (*buffer != '=')
			buffer++;
		buffer++;

		switch (i) {
			case 0:
			conf->K=atoi(buffer);
			break;
			case 1:
			conf->C=atoi(buffer);
			break;
			case 2:
			conf->E=atoi(buffer);
			break;
			case 3:
			conf->T=atoi(buffer);
			break;
			case 4:
			conf->P=atoi(buffer);
			break;
			case 5:
			conf->S=atoi(buffer);
			break;
			case 6:
			conf->F=atoi(buffer);
			break;
			case 7:
			conf->Z=atoi(buffer);
			break;
			case 8:
			conf->S1=atoi(buffer);
			break;
			case 9:
			conf->S2=atoi(buffer);
			break;
			case 10:
			conf->casseIniz=atoi(buffer);
			break;
			case 11:
			conf->notifDir=atoi(buffer);
			break;
			default:
			break;
		}
		i++;
		buffer=backup;
	}

	//controllo se la lettura del file è andata a buon fine
	if (!checkconfig(conf)) {
		perror("Errore: lettura del file di configurazione");
		fclose(fd);
		free(conf);
		free(buffer);
		return NULL;
	}

	fclose(fd);
	free(buffer);
	return conf;
}

/* CODA FIFO */
coda *inCoda(){
	coda *q = malloc(sizeof(coda));

	//errore allocazione della memoria della coda
	if (q == NULL){
		perror("Errore: allocazione della coda");
		return NULL;
	}

	q->first = NULL;
	q->size = 0;

	//inizializzo la mutex della coda e controllo esito
	if (pthread_mutex_init(&q->lock, NULL) != 0){
		perror("Errore: inizializzazione della mutex della coda");
		//libero la memoria predecentemente allocata
		free(q);
		return NULL;
	}

	//inizializzo la variabile di condizione della coda e controllo esito
	if (pthread_cond_init(&q->cond, NULL) != 0){
		perror("Errore: inizializzazione della variabile di condizione della coda");
		//libero la memoria predecentemente allocata
		pthread_mutex_destroy(&q->lock);
		free(q);
		return NULL;
	}

	return q;
}

int push(coda *q, clienteCoda *elem){
	node *ins = malloc(sizeof(node));

	if (q == NULL || elem == NULL){
		if(ins != NULL) free(ins);
		perror("Errore: push di un elemento");
		return -1;
	}

	ins->info = elem;
	ins->next = NULL;

	pthread_mutex_lock(&q->lock);
	if (q->size == 0) q->first = ins;
	else{
		node *curr = q->first;
		while (curr->next != NULL)
			curr = curr->next;
		curr->next = ins;
	}
	(q->size)++;
	pthread_cond_signal(&q->cond);
	pthread_mutex_unlock(&q->lock);

	return 1;
}

clienteCoda *pop(coda *q){

	if (q == NULL) return NULL;

	pthread_mutex_lock(&q->lock);

	//aspetto finchè non c'è un elemento
	while (q->size == 0)
		pthread_cond_wait(&q->cond, &q->lock);

	node *curr = (node *)q->first; 
	clienteCoda *elem = (q->first)->info;

	q->first = q->first->next;
	(q->size)--;
	free(curr);
	pthread_mutex_unlock(&q->lock);

	return elem;
}

int dimensione(coda *q){

	if (q == NULL) return -1;

	int dim = -1;
	pthread_mutex_lock(&q->lock);
	dim = q->size;
	pthread_mutex_unlock(&q->lock);

	return dim;
}

int uscitaCassa(coda *q, int id){
	if(q == NULL) return -1;
	int esito = -1;
	pthread_mutex_lock(&q->lock);

	//se è il primo della fila non lo faccio uscire
	if(q->first->info->id == id){
		pthread_mutex_unlock(&q->lock);
		return esito;
	}

	node *corr = q->first;
	node *prec = NULL;

	for (int i = 0; i < q->size; i++){
		if (id == corr->info->id){
			esito = 1;
			prec->next = corr->next;
			break;
		}
		prec = corr;
		corr = corr->next;
	}
	if(esito){ //se il cliente è stato trovato ed è uscito
		(q->size)--;
		free(corr);
	}
	pthread_mutex_unlock(&q->lock);
	return esito;
}

int primaCliente(coda *q, int id){
	if(q == NULL) return -1;
	int dim = 0,trovato=0;

	pthread_mutex_lock(&q->lock);
	node *corr = q->first;
	for (int i = 0; i < q->size; i++){
		if (id == corr->info->id){
			trovato = 1;
			break;
		}
		dim++;
		corr = corr->next;
	}
	pthread_mutex_unlock(&q->lock);

	//caso in cui non c'è più il cliente
	if(!trovato) return -1;
	return dim;
}

void freeCoda(coda *q){

	if (q == NULL) return;
	//la lock non è necessaria,soltanto per sicurezza
	pthread_mutex_lock(&q->lock);
	//libero di ogni elemento della coda fino a quando rimane solo un elemento
	while (q->first != NULL){
		node *n = (node *)q->first;
		q->first = q->first->next;
		//free(n->info); ci pensa il cliente
		free(n);
	}
	//libero l'ultimo elemento rimasto della coda,la mutex e la variabile di condizione
	if (q->first) free(q->first);

	pthread_mutex_unlock(&q->lock);
	if (&q->lock) pthread_mutex_destroy(&q->lock);
	if (&q->cond) pthread_cond_destroy(&q->cond);
	free(q); //alla fine quando tutto è stato eliminato e liberato,elimino la coda stessa
}

void cleanCoda(coda *q){

	if (q == NULL) return;

	pthread_mutex_lock(&q->lock);
	//libero di ogni elemento della coda fino a quando rimane solo un elemento
	while (q->first != NULL){
		node *n = (node *)q->first;
		q->first = q->first->next;
		//free(n->info); ci pensa il cliente
		free(n);
	}
	q->size = 0;

	pthread_mutex_unlock(&q->lock);
}


/* SOCKET */
int readn(long fd, int *buf, size_t size){
	size_t left = size;
	int r;
	int *bufptr = (int*)buf;

	while(left>0) {
		if ((r=read((int)fd ,bufptr,left)) == -1) {
			if (errno == EINTR) continue; //non è un errore
			return -1;
		}
		if (r == 0) return 0;   // gestione chiusura socket
		left -= r;
		bufptr += r;
	}
	return size;
}

int writen(long fd, int *buf, size_t size) {
	size_t left = size;
	int r;
	int *bufptr = (int*)buf;
	while(left>0) {
		if ((r=write((int)fd ,bufptr,left)) == -1) {
			if (errno == EINTR) continue; //non un errore
			return -1;
		}
		if (r == 0) return 0; //problema connessione
		left -= r;
		bufptr += r;
	}
	return 1;
}

/* TEMPO */
float timedifference_msec(struct timespec start,struct timespec end){
	return ((((double)end.tv_sec + 1.0e-9*end.tv_nsec)-((double)start.tv_sec + 1.e-9*start.tv_nsec)))*1000;
}