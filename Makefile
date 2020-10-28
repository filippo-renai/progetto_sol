#MAKEFILE 

CC = gcc
CFLAGS  = -Wall -std=c99 -lpthread 
LIBDIR = ./libreria
CONF1 = ./configurazioni/config1.txt
CONF2 = ./configurazioni/config2.txt
VALGRIND = valgrind --leak-check=full


.PHONY: all compile test1 test2 clean


all: compile

#Compilo la libreria, la linko ai file supermercato.c direttore.c e genero gli eseguibili
compile:
	@echo "Creo la libreria statica e generaro gli eseguibili del supermercato e direttore.\n"
	$(CC) $(CFLAGS) $(LIBDIR)/mylib.c -c -o $(LIBDIR)/mylib.o
	ar rvs libmy.a $(LIBDIR)/mylib.o
	$(CC) supermercato.c $(CFLAGS) -o supermercato -L. -lmy
	$(CC) direttore.c $(CFLAGS) -o direttore -L. -lmy

#Esegue il test1
test1:
	@echo "Esecuzione supermercato per 15 secondi.\n"
	(./direttore $(CONF1) 1 & echo $$! > t1.PID) &
	$(VALGRIND) ./supermercato $(CONF1) &
	sleep 15
	kill -s QUIT $$(cat t1.PID)

#Esegue il test2
test2:
	@echo "Esecuzione supermercato, nuovi clienti devono entrare entro 25 secondi.\n"
	(./direttore $(CONF2) 2 & echo $$! > t2.PID) &
	sleep 25
	@echo "\nNessun nuovo cliente puo' entrare.\n"
	kill -s HUP $$(cat t2.PID)
	chmod +x ./analisi.sh
	./analisi.sh

#Rimuovo i file generati
clean:
	@echo "Ripulisco i file generati.\n"
	rm -f  *.o direttore supermercato logFile.log libmy.a $(LIBDIR)/mylib.o t2.PID  t1.PID sock
