import ray

# Inizializza Ray (in locale)
ray.init(ignore_reinit_error=True)

# Funzione remota per che somma due numeri
@ray.remote
def somma(a, b):
    return a + b

# Funzione di riduzione parallela
def parallel_reduce(array):
    # Mette in memoria condivisa
    futures = [ray.put(x) for x in array]

    while len(futures) > 1:
        # Somma a coppie
        nuovi_futures = []
        for i in range(0, len(futures) - 1, 2):
            nuovi_futures.append(somma.remote(futures[i], futures[i + 1]))

        # Se dispari, porta avanti l'ultimo elemento
        if len(futures) % 2 == 1:
            nuovi_futures.append(futures[-1])

        futures = nuovi_futures

    return ray.get(futures[0])

# Esegui test
if __name__ == "__main__":
    import random

    array = [random.randint(1, 10) for _ in range(16)]  # 16 elementi
    print("Array:", array)

    somma_totale = parallel_reduce(array)
    print("Somma totale (parallela):", somma_totale)
    print("Somma totale (controllo):", sum(array))

