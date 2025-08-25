#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>
#include <math.h>
#include <ctype.h>   // necesario para toupper()

// ------------------ Configuración ------------------
const char *SERIAL_PORT      = "/dev/ttyUSB0";
int BAUDRATE                 = B9600;

double MIN_START             = -50.3;
double MAX_START             = 543.5;
double RESET_LIMIT           = 1350.8;
double RESET_VALUE           = 0.0;

double INCREMENT_MIN         = 0.2;
double INCREMENT_MAX         = 1.5;

int INTERVAL_US              = 1000000; // microsegundos entre envíos
char PAUSE_KEY               = 'p';
char RESET_KEY               = ' ';

// ------------------ Variables globales ------------------
int fd; 
int running = 1;
int paused = 0;
struct termios orig_termios;

// ------------------ Funciones ------------------
void cleanup(int signo) {
    running = 0;
    if (fd > 0) close(fd);
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    printf("\nPrograma finalizado.\n");
    exit(0);
}

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

int kbhit() {
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

int setup_serial(const char *device) {
    int fd = open(device, O_RDWR | O_NOCTTY);
    if (fd == -1) {
        perror("Error abriendo puerto");
        exit(1);
    }
    fcntl(fd, F_SETFL, 0);

    struct termios options;
    tcgetattr(fd, &options);

    cfsetispeed(&options, BAUDRATE);
    cfsetospeed(&options, BAUDRATE);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;

    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
    options.c_oflag &= ~OPOST;

    tcsetattr(fd, TCSANOW, &options);
    return fd;
}

double rand_double(double min, double max) {
    return min + (rand() / (double)RAND_MAX) * (max - min);
}

// ------------------ Main ------------------
int main() {
    signal(SIGINT, cleanup);
    srand(time(NULL));

    enable_raw_mode();
    fd = setup_serial(SERIAL_PORT);

    double valor = MIN_START + rand_double(0, MAX_START - MIN_START);

    printf("Programa iniciado. Pausa/Reanuda con '%c', Reset con espacio.\n", PAUSE_KEY);

    while (running) {
        // --- Leer teclado ---
        if (kbhit()) {
            char c = getchar();
            if (c == RESET_KEY) {
                valor = RESET_VALUE;
                printf(" -> RESET aplicado: %.1f kg\n", valor);
            } else if (c == PAUSE_KEY || c == toupper(PAUSE_KEY)) {
                paused = !paused;
                printf(" -> %s\n", paused ? "PAUSADO" : "REANUDADO");
            }
        }

        if (!paused) {
            valor += rand_double(INCREMENT_MIN, INCREMENT_MAX);
            if (fabs(valor) >= RESET_LIMIT) {
                valor = RESET_VALUE;
                printf(" -> RESET automático: %.1f kg\n", valor);
            }
        }

        // --- Construir cadena con formato fijo ---
        char numbuf[16];
        snprintf(numbuf, sizeof(numbuf), "%c%7.1f", (valor >= 0 ? '+' : '-'), fabs(valor));

        char buffer[64];
        snprintf(buffer, sizeof(buffer), "ST,NT,%skg\r\n", numbuf);

        // Enviar por serial
        if (write(fd, buffer, strlen(buffer)) < 0) {
            perror("Error escribiendo al puerto");
            break;
        }

        // Mostrar en consola
        printf("Enviado: %s", buffer);

        usleep(INTERVAL_US);
    }

    cleanup(0);
    return 0;
}
