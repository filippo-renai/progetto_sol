#!/bin/bash
PID=$(cat t2.PID)
LOG="logFile.log"

#aspetto il processo direttore, che termina per ultimo 
while [ -e /proc/$PID ]; do
	sleep 0.3
done

#stampo il file nello std output se presente
if [ -f $LOG ]; then
	while read line; do
		echo $line
	done < $LOG
else
	echo "Errore: file non trovato"
fi
