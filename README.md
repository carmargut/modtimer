# modtimer
Módulo del kernel que genera números aleatorios y los inserta en una lista enlazada

El módulo permite que que un único programa "consuma" los números de la lista leyendo de la entrada ```/proc/modtimer```. El programa se bloqueará si la lista está vacía al hacer una lectura.
El proceso de generación de números (gestionado mediante un temporizador) está activo mientras un programa de usuario esté leyendo de la entrada `/proc`

- - - -

El módulo consta de tres capas:
 
* Top Half: Temporizador del kernel que genera secuencia de números y los inserta en un buffer circular acotado
* Bottom Half: Tarea diferida que transfiere los enteros del buffer circular a la lista enlazada.
* Upper Layer: Implementación de operaciones asociadas a las entradas `/proc` exportadas por el módulo.


