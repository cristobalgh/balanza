#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#define NOMBRE "PEDRO"          // Cambia a "PEDRO" en el otro computador
#define PUERTO "/dev/ttyUSB0"  // En cada PC es /dev/ttyUSB0

int configurar_serial(const char *puerto) {
    int fd = open(puerto, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        perror("Error abriendo puerto serial");
        exit(1);
    }

    struct termios opciones;
    tcgetattr(fd, &opciones);

    cfsetispeed(&opciones, B9600);
    cfsetospeed(&opciones, B9600);

    opciones.c_cflag &= ~PARENB; // sin paridad
    opciones.c_cflag &= ~CSTOPB; // 1 stop bit
    opciones.c_cflag &= ~CSIZE;
    opciones.c_cflag |= CS8;     // 8 bits
    opciones.c_cflag |= CREAD | CLOCAL;
    opciones.c_iflag = 0;
    opciones.c_oflag = 0;
    opciones.c_lflag = 0;

    tcflush(fd, TCIFLUSH);
    tcsetattr(fd, TCSANOW, &opciones);

    return fd;
}

int main() {
    int fd = configurar_serial(PUERTO);
    char buffer[256];

    while (1) {
        // Enviar mensaje identificándome
        char mensaje[64];
        snprintf(mensaje, sizeof(mensaje), "Hola, soy %s\n", NOMBRE);
        write(fd, mensaje, strlen(mensaje));

        // Leer si llega algo
        int n = read(fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            printf("%s recibió: %s", NOMBRE, buffer);
        }

        sleep(2);
    }

    close(fd);
    return 0;
}
