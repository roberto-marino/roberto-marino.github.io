#!/usr/bin/env bash
# test_multifd.sh
#
# Apre N connessioni simultanee verso il server,
# aspetta che siano tutte pronte, poi manda i messaggi
# tutte insieme per forzare select() a ritornare
# piu' fd pronti nello stesso ciclo.
#
# Utilizzo:
#   chmod +x test_multifd.sh
#   ./test_multifd.sh          # default: 5 client
#   ./test_multifd.sh 10       # 10 client
#   ./test_multifd.sh 5 2      # 5 client, attesa 2 secondi

set -euo pipefail

HOST="127.0.0.1"
PORT=8080
N="${1:-5}"       # numero di client (default 5)
DELAY="${2:-1}"   # secondi di attesa prima di mandare (default 1)

echo "=== test_multifd.sh ==="
echo "host   : $HOST:$PORT"
echo "client : $N"
echo "delay  : ${DELAY}s (tutti mandano insieme dopo questo tempo)"
echo ""
echo "Avvio $N connessioni in background..."

for i in $(seq 1 "$N"); do
    (
        sleep "$DELAY"
        echo "messaggio dal client $i"
    ) | nc "$HOST" "$PORT" &
done

echo "Connessioni avviate. Attendo che terminino..."
wait
echo ""
echo "Fatto. Controlla l'output del server:"
echo "dovresti vedere i messaggi arrivare tutti nello stesso ciclo."
