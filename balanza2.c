#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>
#include <ctype.h>
#include <math.h>   // necesario para fabs()

#define SERIAL_PORT "/dev/ttyUSB0"
#define BAUDRATE B9600

// Configuración de rango y reset
#define MIN_START       -50.3
#define MAX_START       543.5
#define RESET_LIMIT     1350.8
#define RESET_VALUE      0.0   // Reset con barra espaciadora

// Variables globales
int fd; 
int running = 1;
struct termios orig_termios;

// Manejo de Ctrl+C
void cleanup(int signo) {
    running = 0;
    if (fd > 0) close(fd);
    // Restaurar modo terminal
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    printf("\nPuerto cerrado. Saliendo...\n");
    exit(0);
}

// Restaurar modo terminal al salir
void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

// Habilitar modo raw en stdin
void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode); // restaurar al salir
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO); // desactivar buffering y eco
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// Chequear si hay tecla en stdin
int kbhit() {
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

// Configuración del puerto serie
int setup_serial(const char *device) {
    int fd = open(device, O_RDWR | O_NOCTTY);
    if (fd == -1) {
        perror("No se puede abrir el puerto serie");
        exit(1);
    }

    fcntl(fd, F_SETFL, 0); // quitar non-blocking

    struct termios options;
    tcgetattr(fd, &options);

    cfsetispeed(&options, BAUDRATE);
    cfsetospeed(&options, BAUDRATE);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;

    // Modo raw, sin procesar CR/LF ni control de flujo
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
    options.c_oflag &= ~OPOST;

    tcsetattr(fd, TCSANOW, &options);
    return fd;
}

int main() {
    signal(SIGINT, cleanup);
    srand(time(NULL));

    enable_raw_mode();      // activar modo raw para stdin
    fd = setup_serial(SERIAL_PORT);

    double valor = MIN_START + (rand() / (double)RAND_MAX) * (MAX_START - MIN_START);

    printf("Enviando por serial cada 1 segundo. Ctrl+C para salir.\n");

    while (running) {
        // incremento y reset si supera el límite
        valor += 1.0;
        double absval = fabs(valor);
        if (absval >= RESET_LIMIT) valor = RESET_VALUE;

        // parte entera y decimal aleatoria
        int parte_entera = (int)absval;
        int parte_decimal = rand() % 10;
        char numstr[16];
        snprintf(numstr, sizeof(numstr), "%d.%d", parte_entera, parte_decimal);

        // calcular espacios para tener exactamente 7 caracteres entre signo y 'kg'
        int espacios = 7 - strlen(numstr);
        if (espacios < 0) espacios = 0;

        // construir numbuf manualmente
        char numbuf[32];
        int pos = 0;
        numbuf[pos++] = (valor >= 0 ? '+' : '-');
        for (int i = 0; i < espacios; i++) {
            if (pos < sizeof(numbuf) - 1) numbuf[pos++] = ' ';
        }
        for (int i = 0; i < strlen(numstr); i++) {
            if (pos < sizeof(numbuf) - 1) numbuf[pos++] = numstr[i];
        }
        numbuf[pos] = '\0';

        // formar la trama final
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "ST,NT,%skg\r\n", numbuf);

        // enviar por puerto serie
        write(fd, buffer, strlen(buffer));

        // mostrar en consola
        printf("Enviado: %s", buffer);

        // --- Leer teclado para reset ---
        if (kbhit()) {
            char c = getchar();
            if (c == ' ') {
                valor = RESET_VALUE;
                printf(" -> Reset por barra espaciadora a %.1fkg\n", valor);
            }
        }

        sleep(1);
    }

    cleanup(0);
    return 0;
}
