# Cliente FTP Concurrente

Este proyecto implementa un cliente FTP capaz de realizar transferencias de archivos en segundo plano sin bloquear la terminal del usuario.

## Archivos
- `ZunigaJ-clienteFTP.c`: Código fuente principal del cliente.
- `connectTCP.c`, `passiveTCP.c`, etc.: Librerías de conexión de sockets.
- `Makefile`: Script de compilación automatizada.

## Compilación
Para compilar el proyecto, asegúrese de estar en el directorio de los archivos y ejecute:

```bash
make
