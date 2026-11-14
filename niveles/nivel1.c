#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Constantes esteticas
#define PROMPT '$'
#define COMMAND_LINE_SIZE 1024
#define ARGS_SIZE 64
#define GRIS  "\x1b[38;5;245m"
#define ROJO  "\x1b[31m"
#define RESET "\033[0m"
#define COLOR_PROMPT  "\x1b[36m" 

//Activar mensajes de depuracion
#define DEBUGN1 1

//Imprimir el prompt y lee una linea completa desde la entrada
char *read_line(char *line) {
    //Imprimir promt + color
    printf(COLOR_PROMPT "%c " RESET, PROMPT);
    fflush(stdout);// Hace la impresion inmediata

    //Lee hasta encontrar un salto de linea o tamaño max
    char *ptr = fgets(line, COMMAND_LINE_SIZE, stdin);

    //Error o usuario pulsa Ctrl+D
    if (ptr == NULL) {
        if (feof(stdin)) { //Si es fin de archivo
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

    //Obtener el restos de tokens
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
            ntokens++;  //NULL es el cierre
            break;      //Salimos del bucle
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

//"dummies" imprimen que se han ejecutado
int internal_cd(char **args){
    #if DEBUGN1
    fprintf(stderr, GRIS "[internal_cd()→ Esta función cambiará de directorio]\n" RESET);
    #endif
    return 1;
}

int internal_export(char **args) {
    #if DEBUGN1
    fprintf(stderr, GRIS "[internal_export()→ Esta función asignará valores a variables de entorno]\n" RESET);
    #endif
    return 1;
}

int internal_source(char **args) {
    #if DEBUGN1
    fprintf(stderr, GRIS "[internal_source()→ Esta función ejecutará un fichero de líneas de comandos]\n" RESET);
    #endif
    return 1;
}

int internal_jobs(void) {
    #if DEBUGN1
    fprintf(stderr, GRIS "[internal_jobs()→ Esta función mostrará el PID de los procesos que no estén en foreground]\n" RESET);
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
    int ntokens = parse_args(args, line);

    if (ntokens <= 0){
        return 0;   //Linea vacia o solo comentarios
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
    while (1) {
        if (read_line(line)) {
            execute_line(line);
        }
    }
    return 0;
}