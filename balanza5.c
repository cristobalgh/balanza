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
#include <ctype.h>   // para toupper()

// ------------------ Configuración completa ------------------
typedef struct {
    // Puerto serie
    const char *serial_port;
    int baudrate;

    // Rango de inicio y reset
    double min_start;
    double max_start;
    double reset_limit;
    double reset_value;

    // Incrementos
    double increment_min;
    double increment_max;

    // Intervalo envío (μs)
    int interval_us;

    // Teclas de control
    char pause_key;
    char reset_key;

    // Prefijos, sufijos y formato
    const char *serial_prefix;
    const char *serial_suffix;
    const char *number_format;   // ejemplo: "%7.1f"  → ancho=7, decimales=1

    // Mensajes de consola
    const char *inicio;
    const char *reset_manual;
    const char *reset_auto;
    const char *pausado;
    const char *reanudar;
    const char *finalizado;
    const char *error_puerto;
    const char *error_escritura;
} Config;

Config cfg = {
    // Puerto
    .serial_port     = "/dev/ttyUSB0",
    .baudrate        = B9600,

    // Valores iniciales
    .min_start       = -50.3,
    .max_start       = 543.5,
    .reset_limit     = 1350.8,
    .reset_value     = 0.0,

    // Incrementos aleatorios
    .increment_min   = 0.2,
    .increment_max   = 1.5,

    // Intervalo (1 segundo)
    .interval_us     = 1000000,

    // Teclas
    .pause_key       = 'p',
    .reset_key       = ' ',

    // Prefijo, sufijo y formato
    .serial_prefix   = "ST,NT,",
    .serial_suffix   = "kg",
    .number_format   = "%7.1f",

    // Mensajes
    .inicio          = "Programa iniciado. Pausa/Reanuda con '%c', Reset con espacio.\n",
    .reset_manual    = " -> RESET aplicado: %.1f kg\n",
    .reset_auto      = " -> RESET automático: %.1f kg\n",
    .pausado         = " -> PAUSADO\n",
    .reanudar        = " -> REANUDADO\n",
    .finalizado      = "Programa finalizado\n",
    .error_puerto    = "Error abriendo puerto",
    .error_escritura = "Error escribiendo al puerto"
};

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
    printf("%s", cfg.finalizado);
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
        perror(cfg.error_puerto);
        exit(1);
    }
    fcntl(fd, F_SETFL, 0);

    struct termios options;
    tcgetattr(fd, &options);

    cfsetispeed(&options, cfg.baudrate);
    cfsetospeed(&options, cfg.baudrate);

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
    fd = setup_serial(cfg.serial_port);

    double valor = cfg.min_start + rand_double(0, cfg.max_start - cfg.min_start);

    printf(cfg.inicio, cfg.pause_key);

    while (running) {
        // --- Leer teclado ---
        if (kbhit()) {
            char c = getchar();
            if (c == cfg.reset_key) {
                valor = cfg.reset_value;
                printf(cfg.reset_manual, valor);
            } else if (c == cfg.pause_key || c == toupper(cfg.pause_key)) {
                paused = !paused;
                printf("%s", paused ? cfg.pausado : cfg.reanudar);
            }
        }

        if (!paused) {
            valor += rand_double(cfg.increment_min, cfg.increment_max);
            if (fabs(valor) >= cfg.reset_limit) {
                valor = cfg.reset_value;
                printf(cfg.reset_auto, valor);
            }
        }

        // --- Construir número con formato parametrizable ---
        char numbuf[32];
        snprintf(numbuf, sizeof(numbuf), cfg.number_format, fabs(valor));

        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%s%c%skg\r\n",
                 cfg.serial_prefix,
                 (valor >= 0 ? '+' : '-'),
                 numbuf);

        // Enviar por serial
        if (write(fd, buffer, strlen(buffer)) < 0) {
            perror(cfg.error_escritura);
            break;
        }

        // Mostrar en consola
        printf("Enviado: %s", buffer);

        usleep(cfg.interval_us);
    }

    cleanup(0);
    return 0;
}
