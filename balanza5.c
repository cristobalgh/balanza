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
#include <ctype.h>

#define SERIAL_PORT "/dev/ttyUSB0"
//#define SERIAL_PORT "/dev/ttyACM0"
#define BAUDRATE B9600

typedef struct {
    // Rango de valores
    double min_start;
    double max_start;
    double reset_limit;
    double reset_value;

    // Control de incremento
    int update_interval;   // segundos
    int step_value;        // paso entero
    int paused;

    // Formato de salida
    const char *prefix;
    const char *suffix;
    int num_width;         // ancho total fijo
    char pause_key;
    char reset_key;

    // Mensajes de consola
    const char *msg_default_values;
    const char *msg_usage;
    const char *msg_sending;
    const char *msg_pause;
    const char *msg_resume;
    const char *msg_reset;
    const char *msg_exit;

    // Códigos de color ANSI
    const char *color_pause;
    const char *color_resume;
    const char *color_reset;
    const char *color_reset_all;
} Config;

Config cfg = {
    .min_start     = -50.3,
    .max_start     = 543.5,
    .reset_limit   = 1350.8,
    .reset_value   = 0.0,

    .update_interval = 1,
    .step_value     = 1,
    .paused         = 0,

    .prefix        = "ST,NT,",
    .suffix        = "kg\r\n",
    .num_width     = 7,
    .pause_key     = 'p',
    .reset_key     = ' ',

    // Mensajes
    .msg_default_values = "Usando valores por defecto: intervalo=%ds, paso=%dkg\n",
    .msg_usage          = "Recuerda: puedes correr el programa así:\nsudo %s <intervalo 1-10s> <paso 1-10kg>\n",
    .msg_sending        = "Enviando cada %ds, paso %dkg (+ decimal aleatorio ±0.9). Ctrl+C para salir.\n",
    .msg_pause          = " -> Pausa: %s%s",
    .msg_resume         = " -> Reanuda: %s%s",
    .msg_reset          = " -> Reset manual: %s%s",
    .msg_exit           = "\nPuerto cerrado. Saliendo...\n",

    // Colores ANSI
    .color_pause    = "\033[33m",   // amarillo
    .color_resume   = "\033[32m",   // verde
    .color_reset    = "\033[31m",   // rojo
    .color_reset_all= "\033[0m"     // reinicia color
};

int fd;
int running = 1;
struct termios orig_termios;

void cleanup(int signo) {
    running = 0;
    if (fd > 0) close(fd);
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    printf("%s", cfg.msg_exit);
    exit(0);
}

void disable_raw_mode() { tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios); }
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
    return select(STDIN_FILENO+1, &fds, NULL, NULL, &tv) > 0;
}

int setup_serial(const char *device) {
    int fd = open(device, O_RDWR | O_NOCTTY);
    if (fd == -1) { perror("No se puede abrir el puerto serie"); exit(1); }
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

void format_num(double valor, char *out, int width) {
    double absval = fabs(valor);
    char numstr[32];
    snprintf(numstr, sizeof(numstr), "%.1f", absval);
    int espacios = width - (int)strlen(numstr);
    if (espacios < 0) espacios = 0;
    int pos = 0;
    out[pos++] = (valor >= 0 ? '+' : '-');
    for (int i=0; i<espacios; i++) out[pos++] = ' ';
    strcpy(&out[pos], numstr);
}

int main(int argc, char *argv[]) {
    if (argc == 3) {
        cfg.update_interval = atoi(argv[1]);
        cfg.step_value = atoi(argv[2]);
        if (cfg.update_interval < 1 || cfg.update_interval > 10) {
            fprintf(stderr, "Error: intervalo debe estar entre 1 y 10 segundos.\n");
            exit(1);
        }
        if (cfg.step_value < 1 || cfg.step_value > 10) {
            fprintf(stderr, "Error: paso debe ser entero entre 1 y 10 kg.\n");
            exit(1);
        }
    } else {
        printf(cfg.msg_default_values, cfg.update_interval, cfg.step_value);
        printf(cfg.msg_usage, argv[0]);
    }

    signal(SIGINT, cleanup);
    srand(time(NULL));
    enable_raw_mode();
    fd = setup_serial(SERIAL_PORT);

    double valor = cfg.min_start + (rand() / (double)RAND_MAX) * (cfg.max_start - cfg.min_start);
    char numbuf[64];
    char buffer[128];

    printf(cfg.msg_sending, cfg.update_interval, cfg.step_value);

    while (running) {
        if (!cfg.paused) {
            double decimal_rand = ((rand() % 19) - 9) / 10.0;
            valor += cfg.step_value + decimal_rand;

            if (fabs(valor) >= cfg.reset_limit) valor = cfg.reset_value;

            format_num(valor, numbuf, cfg.num_width);
            snprintf(buffer, sizeof(buffer), "%s%skg\r\n", cfg.prefix, numbuf);

            write(fd, buffer, strlen(buffer));
            printf("Enviado: %s", buffer);
        }

        if (kbhit()) {
            char c = getchar();
            format_num(valor, numbuf, cfg.num_width);

            if (c == cfg.reset_key) {
                valor = cfg.reset_value;
                printf("%s", cfg.color_reset);
                printf(cfg.msg_reset, numbuf, cfg.suffix);
                printf("%s", cfg.color_reset_all);
            } else if (c == cfg.pause_key || c == toupper(cfg.pause_key)) {
                cfg.paused = !cfg.paused;
                if (cfg.paused) {
                    printf("%s", cfg.color_pause);
                    printf(cfg.msg_pause, numbuf, cfg.suffix);
                    printf("%s", cfg.color_reset_all);
                } else {
                    printf("%s", cfg.color_resume);
                    printf(cfg.msg_resume, numbuf, cfg.suffix);
                    printf("%s", cfg.color_reset_all);
                }
            }
        }

        sleep(cfg.update_interval);
    }

    cleanup(0);
    return 0;
}
