#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>     
#include <string.h>
#include <unistd.h>     
#include <errno.h> 

//Constantes esteticas y de tamaño
#define PROMPT '$'
#define COMMAND_LINE_SIZE 1024
#define ARGS_SIZE 64
#define GRIS  "\x1b[38;5;245m"
#define ROJO  "\x1b[31m"
#define RESET "\033[0m"
#define COLOR_PROMPT  "\x1b[36m"   

//Niveles de depuracion
#define DEBUGN1 0   
#define DEBUGN2 1   

//Imprimir el prompt y lee una linea completa desde la entrada
char *read_line(char *line){
    //Imprimir promt + color
    printf(COLOR_PROMPT"%c "RESET, PROMPT);
    fflush(stdout); // Hace la impresion inmediata

    //Lee hasta encontrar un salto de linea o tamaño max
    char *ptr = fgets(line, COMMAND_LINE_SIZE, stdin);

    //Error o usuario pulsa Ctrl+D
    if (ptr == NULL) {
        if (feof(stdin)) {      //Si es fin de archivo
            printf("\nBye Bye\n");
            exit(0);
        }
        return NULL;
    }

    //Elimina el salto de linea
    size_t len = strlen(ptr);
    if (len > 0 && ptr[len - 1] == '\n') {
        ptr[len - 1] = '\0';
    }

    return ptr;
}

//Troceo de linea de comandos en tokens, ademas de manejar los comentarios
int parse_args(char **args, char *line) {
    int ntokens = 0;
    const char *delim = " \t\n\r";  //Delimitadores

    // Si la línea está vacía, inicializa el primer argumento a NULL
    if (line == NULL) {
        args[0] = NULL;
        #if DEBUGN1
        fprintf(stderr, GRIS "[parse_args()→ token 0: (null)]\n" RESET);
        #endif
        return 0;
    }
    //Obtiene el primer token
    char *tok = strtok(line, delim);

    //Obtener el resto de tokens
    while (tok != NULL && ntokens < ARGS_SIZE - 1) {
        args[ntokens] = tok;
        #if DEBUGN1
        fprintf(stderr, GRIS "[parse_args()→ token %d: %s]\n" RESET,
                ntokens, args[ntokens]);
        #endif

        //Gestor de comentarios
        if (tok[0] == '#') {
            args[ntokens] = NULL;
            #if DEBUGN1
            fprintf(stderr, GRIS "[parse_args()→ token %d corregido: (null)]\n" RESET,
                    ntokens);
            #endif
            ntokens++;   //NULL es el cierre
            break;       //Salimos del bucle
        }

        ntokens++;
        tok = strtok(NULL, delim);  //Siguiente token
    }

    // Asegurarse de que el array de argumentos termina con NULL
    if (ntokens == 0 || args[ntokens - 1] != NULL) {
        args[ntokens] = NULL;
        #if DEBUGN1
        fprintf(stderr, GRIS "[parse_args()→ token %d: (null)]\n" RESET, ntokens);
        #endif
    }

    //Contar cuantos tokens hay
    int real_tokens = 0;
    while (real_tokens < ntokens && args[real_tokens] != NULL) {
        real_tokens++;
    }

    return real_tokens;
}


int internal_cd(char **args) {
    char cwd[COMMAND_LINE_SIZE];    // buffer para guardar el directorio actual
    const char *path = args[1];     // destino al que queremos ir

    // si el usuario solo ha puesto "cd", queremos ir al HOME
    if (path == NULL) {
        path = getenv("HOME");
        if (path == NULL) {
            fprintf(stderr, ROJO "cd: variable HOME no definida\n" RESET);
            return 1;
        }
    }

    // intentamos cambiar de directorio, si falla devuelve -1
    if (chdir(path) == -1) {
        fprintf(stderr, ROJO "chdir() error: %s\n" RESET, strerror(errno));
        return 1;   // comando interno tratado aunque haya fallado
    }

    // mostramos el nuevo directorio actual (solo si debug esta activado)
    if (getcwd(cwd, COMMAND_LINE_SIZE) != NULL) {
        #if DEBUGN2
        fprintf(stderr, GRIS "[internal_cd()→ PWD: %s]\n" RESET, cwd);
        #endif
    } else {
        fprintf(stderr, ROJO "getcwd() error: %s\n" RESET, strerror(errno));
    }

    return 1;   // Era un comando interno
}

int internal_export(char **args){
    
    // comprobamos que hay al menos un argumento despues de "export"
    if (args[1] == NULL) {
        fprintf(stderr, ROJO "Error de sintaxis. Uso: export NOMBRE=VALOR\n" RESET);
        return 1;    // sigue siendo comando interno
    }

    // separamos NOMBRE y VALOR por el '='
    char *nombre = strtok(args[1], "=");
    char *valor = strtok(NULL, "");     // coge el resto de la cadena

    #if DEBUGN2
    fprintf(stderr, GRIS "[internal_export()→ nombre: %s]\n" RESET,
            nombre ? nombre : "(null)");
    fprintf(stderr, GRIS "[internal_export()→ valor: %s]\n" RESET,
            valor ? valor : "(null)");
    #endif

    //Validar que la separacion ha ido bien
    if (nombre == NULL || valor == NULL) {
        fprintf(stderr, ROJO "Error de sintaxis. Uso: export NOMBRE=VALOR\n" RESET);
        return 1;
    }

    // Mostramos valor antiguo para depurar
    #if DEBUGN2
    char *old = getenv(nombre);
    if (old != NULL){
        fprintf(stderr, GRIS "[internal_export()→ valor antiguo de %s: %s]\n" RESET,
                nombre, old);
    } else {
        fprintf(stderr, GRIS "[internal_export()→ valor antiguo de %s: (null)]\n" RESET,
                nombre);
    }
    #endif

    // asignar el nuevo valor, el 1 es para sobreescribir
    if (setenv(nombre, valor, 1) == -1) {
        fprintf(stderr, ROJO "setenv() error: %s\n" RESET, strerror(errno));
        return 1;
    }

    // mostramos el valor nuevo para confirmar
    #if DEBUGN2
    char *nuevo = getenv(nombre);
    if (nuevo != NULL) {
        fprintf(stderr, GRIS "[internal_export()→ valor nuevo de %s: %s]\n" RESET,
                nombre, nuevo);
    } else {
        fprintf(stderr, GRIS "[internal_export()→ valor nuevo de %s: (null)]\n" RESET,
                nombre);
    }
    #endif

    return 1;  // comando interno ejecutado 
}

int internal_source(char **args) {
    #if DEBUGN1
    fprintf(stderr, GRIS "[internal_source()→ Esta función leerá y ejecutará comandos de un fichero]\n" RESET);
    #endif
    return 1;
}

int internal_jobs(void) {
    #if DEBUGN1
    fprintf(stderr, GRIS "[internal_jobs()→ Esta función mostrará los trabajos en segundo plano]\n" RESET);
    #endif
    return 1;
}

int internal_fg(char **args) {
    #if DEBUGN1
    fprintf(stderr, GRIS "[internal_fg()→ Esta función pasará un trabajo al primer plano]\n" RESET);
    #endif
    return 1;
}

int internal_bg(char **args) {
    #if DEBUGN1
    fprintf(stderr, GRIS "[internal_bg()→ Esta función reanudará un trabajo en segundo plano]\n" RESET);
    #endif
    return 1;
}

//Comprueba si el comando metido es un comando interno
//Si lo es, llama a la función correspondiente
int check_internal(char **args){
    if (args == NULL || args[0] == NULL) {
        return 0;   //Linea vacia
    }

    //Comparaciones para detectar cual es el comando
    if (strcmp(args[0], "cd") == 0){
        return internal_cd(args);
    }
    if (strcmp(args[0], "export") == 0){
        return internal_export(args);
    }
    if (strcmp(args[0], "source") == 0){
        return internal_source(args);
    }
    if (strcmp(args[0], "jobs")== 0){
        return internal_jobs();          
    }
    if (strcmp(args[0], "fg") == 0) {
        return internal_fg(args);
    }
    if (strcmp(args[0], "bg") == 0) {
        return internal_bg(args);
    }

    //Sale de la shell directamente
    if (strcmp(args[0], "exit") == 0) {
        printf("Bye Bye\n");
        exit(0);
    }

    return 0;   //No es comando interno
}

//Parasea la línea y decide si ejecutar un comando interno o externo
int execute_line(char *line){
    char *args[ARGS_SIZE];
    //Trocear la linea
    int ntokens = parse_args(args, line);   // <- orden correcto

    if (ntokens <= 0){
        return 0;       //Linea vacia o solo comentarios
    }

    //Mirar si es cmd interno y ejecutar
    if (check_internal(args)){
        return 0;
    }

    return 0;
}


int main(void){
    char line[COMMAND_LINE_SIZE];
    //Bucle infinito de la shell
    while(1){
        if(read_line(line)){
            execute_line(line);
        }
    }
    return 0;
}