#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <mqueue.h>
#include <string.h>

pid_t pid_almacen, pid_fabrica, pid_ventas, pid_almacenar;

sem_t sem_ensamblar, sem_pintar, sem_empaquetar, sem_uds;
sem_t* sem_almacenar;

mqd_t cola;

int unidades_producto = 0;

int tiempo_aleatorio(int min, int max) {
    return rand() % (max - min + 1) + min;
}

void* ensamblar(void* args) {
	printf("[Ensamblaje] Comienzo de mi ejecución...\n");
	while(1) {
		printf("[Ensamblaje] Ensamblando producto...\n");
		sleep(tiempo_aleatorio(3, 8));
		printf("[Ensamblaje] Producto ensamblado.\n");
		sem_post(&sem_pintar);
	}
}

void* pintar(void* arg) {
	printf("[Pintado] Comienzo de mi ejecución...\n");
	while (1) {
		sem_wait(&sem_pintar);
		printf("[Pintado]: Producto recibido. Pintando...\n");
	    sleep(tiempo_aleatorio(2, 4));
	    printf("[Pintado]: Producto pintado.\n");
	    sem_post(&sem_empaquetar);
	}
}


void* empaquetar(void* arg) {
	printf("[Empaquetado] Comienzo de mi ejecución...\n");
	while (1) {
	    sem_wait(&sem_empaquetar);
        printf("[Empaquetado] Producto recibido: empaquetando...\n");
        sleep(tiempo_aleatorio(2, 5));
		printf("[Empaquetado] Producto empaquetado\n");
		sem_post(sem_almacenar);
	}
}

void* almacenar_productos(void* arg) {
	while (1) {
		sem_wait(sem_almacenar);
		sem_wait(&sem_uds);
		unidades_producto++;
		printf("[Almacén] Almacenado nuevo producto\n");
		sem_post(&sem_uds);
	}
}

void* almacen_ventas(void* arg) {
	while (1) {
		char buff[50];
		strcpy(buff, "");

		mq_receive(cola, buff, 50, NULL);
		printf("[Almacén] Recibida orden desde ventas\n");

		int uds, num_orden;
		sscanf(buff, "%d,%d", &num_orden, &uds); //solución de copilot para deserializar
		sem_wait(&sem_uds);
		while (unidades_producto < uds) {
			// esperando a almacenar más unidades
			sem_post(&sem_uds);
			usleep(100000);
			sem_wait(&sem_uds);
		}
		unidades_producto -= uds;
		snprintf(buff, 50, "%d", num_orden); //solución de copilot para serializar
		printf("[Almacén] Atendida orden nº %s. Unidades restantes: %d\n", buff , unidades_producto);
		sem_post(&sem_uds);
	}
}

int main(int argc, char* argv[]) {

	sem_unlink("/sem_almacenar");
	sem_almacenar = sem_open("/sem_almacenar", O_CREAT, 0644, 0);
	sem_init(&sem_pintar, 0, 0);
	sem_init(&sem_empaquetar, 0, 0);
	sem_init(&sem_uds, 0, 1);

	pthread_attr_t attri;
	pthread_attr_init(&attri);

	struct mq_attr attr;
	attr.mq_flags = 0;
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = 50;
	attr.mq_curmsgs = 0;
	mq_unlink("/ventas");
	cola = mq_open("/ventas", O_CREAT | O_RDWR, 0644, &attr);

	pid_almacen = fork();

	if (pid_almacen != 0) {
		pid_fabrica = fork();
		if (pid_fabrica != 0) {
			pid_ventas = fork();
			if(pid_ventas != 0) {
				/* Proceso padre */
				wait(NULL);
				wait(NULL);
				wait(NULL);
			} else {
				/* Proceso Ventas */
				int num_orden = 0;
				while(1) {
					sleep(tiempo_aleatorio(10, 15));
					printf("[Ventas] Recibida compra desde cliente. Enviando orden nº %d al almacén...\n", num_orden);

					char buff[50];
					strcpy(buff, "");
					int uds = rand() % 2 + 1;
					snprintf(buff, 50, "%d,%d", num_orden, uds);
					if (mq_send(cola, buff, 50, 0) == -1) {
						printf("Error de tamaño de mensaje en el proceso Ventas");
					}
					num_orden++;
				}
			}
		} else {
			printf("[Fábrica] Comienzo mi ejecución...\n");


			pthread_t tensamblar;
			pthread_create(&tensamblar, &attri, ensamblar, NULL);
			pthread_t tpintar;
			pthread_create(&tpintar, &attri, pintar, NULL);
			pthread_t tempaquetar;
			pthread_create(&tempaquetar, &attri, empaquetar, NULL);

			pthread_join(tensamblar, NULL);
			pthread_join(tpintar, NULL);
			pthread_join(tempaquetar, NULL);
		}
	} else {
		printf("[Almacén] Comienzo mi ejecución...\n");

		pthread_t talmacenar;
		pthread_create(&talmacenar, &attri, almacenar_productos, NULL);
		pthread_t talmacen_ventas;
		pthread_create(&talmacen_ventas, &attri, almacen_ventas, NULL);

		pthread_join(talmacenar, NULL);
		pthread_join(talmacen_ventas, NULL);
	}

	sem_unlink("/sem_almacenar");
	sem_close(sem_almacenar);
	sem_destroy(&sem_pintar);
	sem_destroy(&sem_empaquetar);

	mq_unlink("/ventas");
	mq_close(cola);

	exit(0);
}
