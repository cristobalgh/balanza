#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>

// ====== CONFIGURACIÓN ======
#define VALOR_MIN_INICIO   -29675.7f
#define VALOR_MAX_INICIO    29876.8f
#define LIMITE_RESETEO      30000.0f
#define VALOR_RESET_BAR     0.0f
// =========================

int fd = -1;
struct termios oldt;

void cerrar_puerto(int sig) {
    if (fd != -1) close(fd);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\nPuerto serie cerrado. Saliendo...\n");
    exit(0);
}

float rand_range(float min, float max) {
    return min + ((float)rand() / RAND_MAX) * (max - min);
}

int kbhit() {
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
}

int main() {
    struct termios options;
    signal(SIGINT, cerrar_puerto);

    fd = open("/dev/ttyS0", O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) { perror("No se pudo abrir el puerto serie"); return 1; }

    tcgetattr(fd, &options);
    cfsetispeed(&options, B9600); cfsetospeed(&options, B9600);
    options.c_cflag &= ~PARENB; options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE; options.c_cflag |= CS8;
    options.c_cflag &= ~CRTSCTS; options.c_cflag |= (CLOCAL | CREAD);
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;
    tcsetattr(fd, TCSANOW, &options);

    struct termios newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt; newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    srand(time(NULL));

    char buffer[64];
    char numbuf[16];
    float valor = rand_range(VALOR_MIN_INICIO, VALOR_MAX_INICIO);

    while (1) {
        float incremento = rand_range(0.2f, 0.5f);
        valor += incremento;

        float dec_fluct = rand_range(-0.1f, 0.1f);
        float valor_final = valor + dec_fluct;

        if (valor_final > LIMITE_RESETEO) valor = valor_final = VALOR_RESET_BAR;

        char signo = (valor_final >= 0.0f) ? '+' : '-';
        float abs_val = (valor_final >= 0.0f) ? valor_final : -valor_final;

        char numstr[16];
        snprintf(numstr, sizeof(numstr), "%.1f", abs_val);

        // Construcción segura del número con relleno para 7 caracteres entre signo y kg
        int len_numstr = strlen(numstr);
        int espacios = 7 - len_numstr; // espacio entre signo y primer dígito
        if (espacios < 0) espacios = 0;
        if (espacios > 7) espacios = 7;

        int pos = 0;
        numbuf[pos++] = signo;            // signo
        for (int i = 0; i < espacios; i++) numbuf[pos++] = ' '; // relleno
        strncpy(numbuf + pos, numstr, sizeof(numbuf) - pos - 1);
        pos += strlen(numstr);
        numbuf[pos] = '\0';

        snprintf(buffer, sizeof(buffer), "ST,NT,%skg\r\n", numbuf);

        int w = write(fd, buffer, strlen(buffer));
        if (w < 0) { perror("Error escribiendo en el puerto serie"); break; }

        printf("Enviando: %s", buffer);

        if (kbhit()) {
            char c;
            read(STDIN_FILENO, &c, 1);
            if (c == ' ') {
                valor = VALOR_RESET_BAR;
                printf(" -> Reset a %.1fkg\n", VALOR_RESET_BAR);
            }
        }

        sleep(1);
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    close(fd);
    return 0;
}
