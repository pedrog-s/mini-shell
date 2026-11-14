#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>     // getenv, setenv, exit
#include <string.h>
#include <unistd.h>     // chdir, getcwd, fork, execvp, pause, write
#include <sys/types.h>  // pid_t
#include <sys/wait.h>   // waitpid, macros WIFEXITED, WEXITSTATUS, WIFSIGNALED, WTERMSIG
#include <errno.h>      // errno, strerror
#include <signal.h>     // signal, SIGINT, SIGCHLD, SIG_IGN, kill, SIGTSTP, SIGSTOP
#include <fcntl.h>      // open, O_WRONLY, O_CREAT, O_TRUNC

#define PROMPT '$'
#define COMMAND_LINE_SIZE 1024
#define ARGS_SIZE 64
#define GRIS  "\x1b[38;5;245m"
#define ROJO  "\x1b[31m"
#define RESET "\033[0m"
#define COLOR_PROMPT  "\x1b[36m"   //color del prompt: cian

//debug dels nivells
#define DEBUGN1 0   //nivell 1
#define DEBUGN2 0   //nivell 2
#define DEBUGN3 0   //nivell 3
#define DEBUGN4 0   //nivell 4
#define DEBUGN5 0   //nivell 5
#define DEBUGN6 0   //nivell 6

#define N_JOBS 64

/*
-Estructura que representa la informació d'un treball (job)
-pid: identificador del procés fill
-estado: 'E' executant, 'D' aturat (stopped), 'N' no utilitzat, 'F' finalitzat
-cmd: la línia de comanda tal i com es va invocar (per missatges i jobs)
*/
struct info_job {
    pid_t pid;
    char estado;  
    char cmd[COMMAND_LINE_SIZE]; 
};

/* 
  Taula de treballs gestionats pel shell. L'índex 0 s'utilitza especialment
  per a representar el procés en foreground
  SI hi ha un treball valid en aquesta posició, el seu pid > 0
*/
static struct info_job jobs_list[N_JOBS];

/*
   Nom amb què s'ha invocat el shell 
   utilitzat per evitar enviar
   senyals al mateix shell per error
*/
static char mi_shell[COMMAND_LINE_SIZE];

/* 
Nombre actual de treballs a la taula
*/
static int n_job = 0;

/* Declaració anticipada de la funció que processa comandes internes. */
int check_internal(char **args);

/* ==================== NIVELL 5 gestio de la llista de treballs ========= */

int jobs_list_add(pid_t pid, char estado, char *cmd) {
    if (n_job >= N_JOBS - 1) {
        fprintf(stderr, ROJO "jobs_list llena, no se puede añadir más trabajos\n" RESET);
        return -1;
    }

    n_job++;
    jobs_list[n_job].pid = pid;
    jobs_list[n_job].estado = estado;
    memset(jobs_list[n_job].cmd, '\0', COMMAND_LINE_SIZE);
    if (cmd != NULL) {
        strncpy(jobs_list[n_job].cmd, cmd, COMMAND_LINE_SIZE - 1);
    }

    printf("[%d] %d %c %s\n", n_job, pid, estado, jobs_list[n_job].cmd);
    fflush(stdout);

    return 0;
}

int jobs_list_find(pid_t pid) {
    for (int i = 1; i <= n_job; i++) {
        if (jobs_list[i].pid == pid) {
            return i;
        }
    }
    return -1; //no trobat
}

int jobs_list_remove(int pos) {
    if (pos < 1 || pos > n_job) {
        return -1;
    }

    if (pos < n_job) {
        jobs_list[pos] = jobs_list[n_job];   //movem l'ultim a la posicio esborrada
    }

    jobs_list[n_job].pid = 0;
    jobs_list[n_job].estado = 'N';
    memset(jobs_list[n_job].cmd, '\0', COMMAND_LINE_SIZE);
    n_job--;

    return 0;
}

/* ==================== NIVELL 1 mostrar prompt i llegir entrada === */

/*
  llegeix una línia d'entrada mostrant el prompt. Aquesta funció encapsula
  la crida a `fgets` i elimina el salt de línia final si existeix
  Retorna punter a la línia llegida o finalitza el shell si s'arriba EOF.
 */
char *read_line(char *line){
    printf(COLOR_PROMPT"%c "RESET, PROMPT);
    fflush(stdout);

    char *ptr = fgets(line, COMMAND_LINE_SIZE, stdin);

    // Si arribam EOF sortim netament
    if (ptr == NULL) {
        if (feof(stdin)) {
            printf("\nBye Bye\n");
            exit(0);
        }
        return NULL;
    }

    // Eliminam el `\n` final si existeix per facilitar el tractament
    size_t len = strlen(ptr);
    if (len > 0 && ptr[len - 1] == '\n') {
        ptr[len - 1] = '\0';
    }

    return ptr;
}

/* ==================== NIVELL 1 separar la línia en arguments == */

int parse_args(char **args, char *line) {
    int ntokens = 0;
    const char *delim = " \t\n\r";

    //si no s'ha passat cap linia deixam args buit i tornam
    if (line == NULL) {
        args[0] = NULL;
        #if DEBUGN1
        fprintf(stderr, GRIS "[parse_args()→ token 0: (null)]\n" RESET);
        #endif
        return 0;
    }

    //tokenitzam la línia utilitzant els separadors definits. s'usa strtok
    //sobre la còpia de la línia ja que strtok modifica el buffer
    char *tok = strtok(line, delim);

    //separam la linia en tokens utilitzant els separadors definits
    while (tok != NULL && ntokens < ARGS_SIZE - 1) {
        args[ntokens] = tok;
        #if DEBUGN1
        fprintf(stderr, GRIS "[parse_args()→ token %d: %s]\n" RESET,
                ntokens, args[ntokens]);
        #endif

        //si el token comença per '#' el tractam com a comentari i acabam
        if (tok[0] == '#') {
            args[ntokens] = NULL;
            #if DEBUGN1
            fprintf(stderr, GRIS "[parse_args()→ token %d corregido: (null)]\n" RESET,
                    ntokens);
            #endif
            ntokens++;
            break;
        }

        ntokens++;
        tok = strtok(NULL, delim);
    }

    //miram que l'ultim punter sigui NULL
    if (ntokens == 0 || args[ntokens - 1] != NULL) {
        args[ntokens] = NULL;
        #if DEBUGN1
        fprintf(stderr, GRIS "[parse_args()→ token %d: (null)]\n" RESET, ntokens);
        #endif
    }

    //contam quants son valids 
    int real_tokens = 0;
    while (real_tokens < ntokens && args[real_tokens] != NULL) {
        real_tokens++;
    }

    return real_tokens;
}

/* ==================== NIVELL 2 comandes internes == */

/// cd canvia el directori actual si no es passa argument es canvia a HOME
int internal_cd(char **args) {
    char cwd[COMMAND_LINE_SIZE];   //buffer per guardar el directori actual
    const char *path = args[1];    //destí al qual volem anar

    //si no s'ha passat cap argument anam a HOME
    if (path == NULL) {
        path = getenv("HOME");
        if (path == NULL) {
            fprintf(stderr, ROJO "cd: variable HOME no definida\n" RESET);
            return 1;
        }
    }

    //intentam canviar de directori si falla imprimim l'error 
    if (chdir(path) == -1) {
        fprintf(stderr, ROJO "chdir() error: %s\n" RESET, strerror(errno));
        return 1;
    }

    //mostram el nou directori actual per depuració si getcwd funciona
    if (getcwd(cwd, COMMAND_LINE_SIZE) != NULL) {
        #if DEBUGN2
        fprintf(stderr, GRIS "[internal_cd()→ PWD: %s]\n" RESET, cwd);
        #endif
    } else {
        fprintf(stderr, ROJO "getcwd() error: %s\n" RESET, strerror(errno));
    }
    //comanda interna processada
    return 1;  
}

/* ==================== NIVELL 2 variables de l'entorn == */

/* export: assigna una variable d'entorn. Ús: export NOMBRE=VALOR */
int internal_export(char **args){

    if (args[1] == NULL) {
        fprintf(stderr, ROJO "Error de sintaxis. Uso: export NOMBRE=VALOR\n" RESET);
        return 1;
    }

    char *nombre = strtok(args[1], "=");
    char *valor = strtok(NULL, "");

    #if DEBUGN2
    fprintf(stderr, GRIS "[internal_export()→ nombre: %s]\n" RESET,
            nombre ? nombre : "(null)");
    fprintf(stderr, GRIS "[internal_export()→ valor: %s]\n" RESET,
            valor ? valor : "(null)");
    #endif

    //si no hi ha nom o valor correctes retornam error de sintaxi
    if (nombre == NULL || valor == NULL) {
        fprintf(stderr, ROJO "Error de sintaxis. Uso: export NOMBRE=VALOR\n" RESET);
        return 1;
    }

    #if DEBUGN2
    char *old = getenv(nombre);
    if (old != NULL){
        fprintf(stderr, GRIS "[internal_export()→ valor antiguo de %s: %s]\n" RESET,
                nombre, old);
    } 
    else {
        fprintf(stderr, GRIS "[internal_export()→ valor antiguo de %s: (null)]\n" RESET,
                nombre);
    }
    #endif

    //assignam o sobreescrivim la variable d'entorn 
    if (setenv(nombre, valor, 1) == -1) {
        fprintf(stderr, ROJO "setenv() error: %s\n" RESET, strerror(errno));
        return 1;
    }

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
    //comanda interna processada
    return 1;
}

/* ====================  comandes internes  ==================== */

int internal_jobs(void) {
    //mostrar els treballs en background/aturats
    for (int i = 1; i <= n_job; i++) {
        if (jobs_list[i].pid > 0) {
            printf("[%d] %d %c %s\n",
                   i,
                   jobs_list[i].pid,
                   jobs_list[i].estado,
                   jobs_list[i].cmd);
        }
    }
    return 1;
}

/* ==================== NIVELL 6  ======== */

int internal_fg(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, ROJO "fg: error de sintaxis. Uso: fg <num_trabajo>\n" RESET);
        return 1;
    }

    int pos = atoi(args[1]);

    if (pos <= 0 || pos > n_job || jobs_list[pos].pid <= 0) {
        fprintf(stderr, ROJO "fg %s: no existe ese trabajo\n" RESET, args[1]);
        return 1;
    }

    //i el treball està aturat (D) li enviam SIGCONT per reanudarlo
    if (jobs_list[pos].estado == 'D') {
        char mensaje[4096];
        if (kill(jobs_list[pos].pid, SIGCONT) == 0) {
            #if DEBUGN6
            sprintf(mensaje, GRIS
                    "[internal_fg()→ Señal 18 (SIGCONT) enviada a %d (%s)]\n" RESET,
                    jobs_list[pos].pid, jobs_list[pos].cmd);
            write(2, mensaje, strlen(mensaje));
            #endif
            (void)mensaje;// evitamos warning
        } else {
            perror("kill");
        }
    }

    /* Portam aquest treball al foreground el copiam a jobs_list[0] per
       marcar-lo temporalment com a procés en foreground i esperam que acabi
    */
    jobs_list[0].pid = jobs_list[pos].pid;
    jobs_list[0].estado = 'E';
    memset(jobs_list[0].cmd, '\0', COMMAND_LINE_SIZE);
    strncpy(jobs_list[0].cmd, jobs_list[pos].cmd, COMMAND_LINE_SIZE - 1);

    //treim " &" de la comanda si existeix per una presentació neta
    size_t len = strlen(jobs_list[0].cmd);
    if (len >= 2 && strcmp(&jobs_list[0].cmd[len - 2], " &") == 0) {
        jobs_list[0].cmd[len - 2] = '\0';
    }

    //eliminam el treball de la llista de background (ja està en foreground)
    jobs_list_remove(pos);

    //mostram la comanda que esta en foreground 
    printf("%s\n", jobs_list[0].cmd);
    fflush(stdout);

    /* Bloquejam l'execució del shell fins que el reaper posi a 0
       jobs_list[0].pid quan el fill hagi finalitzat o hagi estat aturat
     */
    while (jobs_list[0].pid > 0) {
        pause();
    }

    return 1;
}

/* ==================== NIVELL 6 ======= */

int internal_bg(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, ROJO "bg: error de sintaxis. Uso: bg <num_trabajo>\n" RESET);
        return 1;
    }

    int pos = atoi(args[1]);

    if (pos <= 0 || pos > n_job || jobs_list[pos].pid <= 0) {
        fprintf(stderr, ROJO "bg %s: no existe ese trabajo\n" RESET, args[1]);
        return 1;
    }

    if (jobs_list[pos].estado == 'E') {
        fprintf(stderr, ROJO "bg %s: el trabajo ya está en segundo plano\n" RESET, args[1]);
        return 1;
    }

    /* marcam l'estat com a executant i enviam SIGCONT per reprendrelo en
       background. Afegim " &" a la comanda per a la presentació a l'usuari
    */
    jobs_list[pos].estado = 'E';

    size_t len = strlen(jobs_list[pos].cmd);
    if (len + 2 < COMMAND_LINE_SIZE) {
        if (!(len >= 2 && strcmp(&jobs_list[pos].cmd[len - 2], " &") == 0)) {
            strcat(jobs_list[pos].cmd, " &");
        }
    }

    char mensaje[4096];
    if (kill(jobs_list[pos].pid, SIGCONT) == 0) {
        #if DEBUGN6
        sprintf(mensaje, GRIS
                "[internal_bg()→ señal 18 (SIGCONT) enviada a %d (%s)]\n" RESET,
                jobs_list[pos].pid, jobs_list[pos].cmd);
        write(2, mensaje, strlen(mensaje));
        #endif
        (void)mensaje;
    } else {
        perror("kill");
    }

    //mostrar el treball que s'ha reanimat
    printf("[%d] %d %c %s\n",
           pos,
           jobs_list[pos].pid,
           jobs_list[pos].estado,
           jobs_list[pos].cmd);
    fflush(stdout);

    return 1;
}

/* ==================== NIVELL 5 detectam el "&" =============== */

int is_background(char **args) {
    int i = 0;
    while (args[i] != NULL) {
        i++;
    }
    //comprovam si l'ultim argument es "&"
    if (i > 0 && strcmp(args[i - 1], "&") == 0) {
        args[i - 1] = NULL;
        return 1;
    }
    return 0;
}
/* ==================== NIVELL 6: detectam ">" ================= */

int is_output_redirection(char **args) {
    int i = 0;
    //anam mirant argument per argument
    while (args[i] != NULL) {
        if (strcmp(args[i], ">") == 0) {
            //comprovam que hi hagi un nom de fitxer despres de ">"
            if (args[i + 1] == NULL) {
                fprintf(stderr, ROJO "Error de sintaxis. Falta el nombre de fichero tras '>'\n" RESET);
                exit(EXIT_FAILURE); 
            }

            #if DEBUGN6
            fprintf(stderr, GRIS "[is_output_redirection()→ redirigiendo a %s]\n" RESET,
                    args[i + 1]);
            #endif

            //obrim el fitxer amb els flags correctes (escriure, crear si no existeix, truncar)
            int fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                fprintf(stderr, ROJO "open() error: %s\n" RESET, strerror(errno));
                exit(EXIT_FAILURE);
            }
            
            //redirigim la sortida al fitxer obert
            //connectam el fitxer a la sortida estàndard (1)
            if (dup2(fd, 1) < 0) {
                fprintf(stderr, ROJO "dup2() error: %s\n" RESET, strerror(errno));
                close(fd);
                exit(EXIT_FAILURE);
            }

            //tancam el descriptor original que ja no fa falta perque l'hem duplicat
            close(fd);

            //eliminam els arguments ">" i el nom del fitxer perquè execvp no el trobi
            args[i] = NULL;
            return 1;
        }
        i++;
    }
    return 0;
}

/* ==================== NIVELL 4/5 ================== */

//recogim els fills que no han estat bloquejats i actualitzam la llista de treballs
void reaper(int signum)
{
    (void)signum;

    //rearmam el senyal per si de cas
    signal(SIGCHLD, reaper);

    pid_t ended;
    int status;
    char mensaje[4096];

    //bucle per recollir tots els fills (zombies) que hagin acabat
    while ((ended = waitpid(-1, &status, WNOHANG)) > 0) {

        #if DEBUGN4
        sprintf(mensaje, GRIS
                "[reaper()→ recibida señal %d (SIGCHLD)]\n" RESET,
                signum);
        write(2, mensaje, strlen(mensaje));
        #endif

        int es_foreground = 0;
        int pos = -1;

        //cercam qui ha acabat
        if (ended == jobs_list[0].pid) {
            es_foreground = 1;
        } else {
            pos = jobs_list_find(ended);
        }
        
        //mostram el tipus de finalització si l'han matat o ha acabat normal
        if (WIFEXITED(status)) {
            if (es_foreground) {
                #if DEBUGN4
                sprintf(mensaje, GRIS
                        "[reaper()→ Proceso hijo %d en foreground (%s) finalizado con exit code %d]\n" RESET,
                        ended, jobs_list[0].cmd, WEXITSTATUS(status));
                write(2, mensaje, strlen(mensaje));
                #endif
            } else {
                #if DEBUGN4
                sprintf(mensaje, GRIS
                        "[reaper()→ Proceso hijo %d en background (%s) finalizado con exit code %d]\n" RESET,
                        ended,
                        (pos > 0 ? jobs_list[pos].cmd : ""),
                        WEXITSTATUS(status));
                write(2, mensaje, strlen(mensaje));
                #endif
            }
        } else if (WIFSIGNALED(status)) {
            if (es_foreground) {
                #if DEBUGN4
                sprintf(mensaje, GRIS
                        "[reaper()→ Proceso hijo %d en foreground (%s) finalizado por señal %d]\n" RESET,
                        ended, jobs_list[0].cmd, WTERMSIG(status));
                write(2, mensaje, strlen(mensaje));
                #endif
            } else {
                #if DEBUGN4
                sprintf(mensaje, GRIS
                        "[reaper()→ Proceso hijo %d en background (%s) finalizado por señal %d]\n" RESET,
                        ended,
                        (pos > 0 ? jobs_list[pos].cmd : ""),
                        WTERMSIG(status));
                write(2, mensaje, strlen(mensaje));
                #endif
            }
        } else {
            #if DEBUGN4
            sprintf(mensaje, GRIS
                    "[reaper()→ Proceso hijo %d finalizado]\n" RESET,
                    ended);
            write(2, mensaje, strlen(mensaje));
            #endif
        }

        //actualitzam l'estructura de dades segons qui hagi acabat
        if (es_foreground) {
            jobs_list[0].pid = 0;
            jobs_list[0].estado = 'F';
            memset(jobs_list[0].cmd, '\0', COMMAND_LINE_SIZE);
        } else if (pos > 0) {
            int st = WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status);
            sprintf(mensaje,
                    "Terminado PID %d (%s) en jobs_list[%d] con status %d\n",
                    ended, jobs_list[pos].cmd, pos, st);
            write(1, mensaje, strlen(mensaje));
            //si era un job de la llista l'esborram
            jobs_list_remove(pos);
        }
    }
}

/* ==================== NIVELL 4 gestor ============ */

void ctrlc(int signum)
{
    (void)signum;

    signal(SIGINT, ctrlc);

#if DEBUGN4
    char mensaje[4096];

    sprintf(mensaje, GRIS
            "\n[ctrlc()→ Soy el proceso con PID %d (%s), el proceso en foreground es %d (%s)]\n" RESET,
            getpid(), mi_shell, jobs_list[0].pid, jobs_list[0].cmd);
    write(2, mensaje, strlen(mensaje));

    sprintf(mensaje, GRIS
            "[ctrlc()→ recibida señal %d (SIGINT)]\n" RESET, signum);
    write(2, mensaje, strlen(mensaje));
#endif

    //miram si hi ha algú corrent en primer pla
    if (jobs_list[0].pid > 0) {

        //si el proces en foreground es el shell, no l'aturam ja que seria un suïcidi
        if (strcmp(jobs_list[0].cmd, mi_shell)== 0) {
#if DEBUGN4
            sprintf(mensaje, GRIS "[ctrlc()→ Señal 15 no enviada por %d (%s) debido a que su proceso en foreground es el shell]\n" RESET,
                    getpid(), mi_shell);
            write(2, mensaje, strlen(mensaje));
#endif

        } else {
            //enviam SIGTERM al fill en foreground
            if (kill(jobs_list[0].pid, SIGTERM) == 0) {
#if DEBUGN4
                sprintf(mensaje, GRIS"[ctrlc()→ Señal 15 (SIGTERM) enviada a %d (%s) por %d (%s)]\n" RESET,
                        jobs_list[0].pid, jobs_list[0].cmd, getpid(), mi_shell);
                write(2, mensaje, strlen(mensaje));
#endif
                //afegim un salt de linia per a que el prompt surti bé
                write(1, "\n", 1);
            } else {
                perror("kill");
            }
        }

    } else {
#if DEBUGN4
        sprintf(mensaje,GRIS "[ctrlc()→ Señal 15 no enviada por %d (%s) debido a que no hay proceso en foreground]\n" RESET,
                getpid(), mi_shell);
        write(2, mensaje, strlen(mensaje));
#endif
    }
}


/* ==================== NIVELL 5 gestor ========= */

void ctrlz(int signum)
{
    (void)signum;

    signal(SIGTSTP, ctrlz);

    #if DEBUGN5
    char mensaje[4096];

    sprintf(mensaje, GRIS
            "\n[ctrlz()→ Soy el proceso con PID %d, el proceso en foreground es %d (%s)]\n" RESET,
            getpid(), jobs_list[0].pid, jobs_list[0].cmd);
    write(2, mensaje, strlen(mensaje));

    sprintf(mensaje, GRIS"[ctrlz()→ recibida señal %d (SIGTSTP)]\n" RESET, SIGTSTP);
    write(2, mensaje, strlen(mensaje));
    #endif

    if (jobs_list[0].pid > 0) {
        //si el proces en foreground es el shell no l aturam
        if (strcmp(jobs_list[0].cmd, mi_shell) == 0) {
            #if DEBUGN5
            sprintf(mensaje, GRIS"[ctrlz()→ Señal %d (SIGSTOP) no enviada debido a que el proceso en foreground es el shell]\n" RESET,
                    SIGSTOP);
            write(2, mensaje, strlen(mensaje));
            #endif
        } else {
            //enviam SIGSTOP al fill en foreground
            if (kill(jobs_list[0].pid, SIGSTOP) == 0) {
                #if DEBUGN5
                sprintf(mensaje, GRIS"[ctrlz()→ Señal %d (SIGSTOP) enviada a %d (%s) por %d (%s)]\n" RESET,
                        SIGSTOP, jobs_list[0].pid, jobs_list[0].cmd, getpid(), mi_shell);
                write(2, mensaje, strlen(mensaje));
                #endif

                //marcam com a aturat el procés en foreground
                jobs_list[0].estado = 'D';
                
                write(1, "\n", 1);

                //l'afegim a la llista de jobs perquè no es perdi
                jobs_list_add(jobs_list[0].pid, 'D', jobs_list[0].cmd);

                //l'alliebram del foreground amb un lloc lliure
                jobs_list[0].pid = 0;
                jobs_list[0].estado = 'N';
                memset(jobs_list[0].cmd, '\0', COMMAND_LINE_SIZE);
            } else {
                perror("kill");
            }
        }
    } else {
        #if DEBUGN5
        sprintf(mensaje, GRIS"[ctrlz()→ Señal %d (SIGSTOP) no enviada debido a que no hay proceso en foreground]\n" RESET,
                SIGSTOP);
        write(2, mensaje, strlen(mensaje));
        #endif
    }
}

/* ==================== NIVELL 5/6 =========== */

int execute_line(char *line){

    char *args[ARGS_SIZE];
    char line_copy[COMMAND_LINE_SIZE];  //copia de la linia original
    pid_t pid;

    //guardam una copia abans de separar perque strtok modifica el buffer
    memset(line_copy, '\0', COMMAND_LINE_SIZE);
    strncpy(line_copy, line, COMMAND_LINE_SIZE - 1);

    int ntokens = parse_args(args, line);
    if (ntokens <= 0){
        return 0;
    }

    //comprovam si la comanda es interna i l'executam directament
    if (check_internal(args)) {
        return 0;
    }

    //miram si hi ha un ampersand al final
    int bg = is_background(args);

    #if DEBUGN5
    fprintf(stderr, GRIS "[execute_line()→ PID padre: %d (%s)]\n" RESET,
            getpid(), mi_shell);
    #endif

    //cream el fill
    pid = fork();

    if (pid == 0){
        //codi del fill
        //el shell controla les senyals i feim que el fill les ignori per defecte
        signal(SIGINT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);

        //redireccionam la sortida si trobam un ">"
        is_output_redirection(args);

        //si falla mostram l'error i acabam el proces fill
        execvp(args[0], args);

        fprintf(stderr, ROJO "%s: %s\n" RESET, args[0], strerror(errno));
        exit(-1);

    } else if (pid > 0) {
        //codi del pare
        #if DEBUGN5
        fprintf(stderr, GRIS "[execute_line()→ PID hijo: %d (%s)]\n" RESET,
                pid, line_copy);
        #endif
        
        //el proces de pare espera si no es en background
        if (!bg) {
            //actualitzam la informació del foreground
            jobs_list[0].pid = pid;
            jobs_list[0].estado = 'E';
            memset(jobs_list[0].cmd, '\0', COMMAND_LINE_SIZE);
            strncpy(jobs_list[0].cmd, line_copy, COMMAND_LINE_SIZE - 1);
            
            //bloquetjam fins que acabi o el mati una senyal
            while (jobs_list[0].pid > 0) {
                pause();
            }
        } else {
            //si es background l'afegim a la llista i seguim
            jobs_list_add(pid, 'E', line_copy);
        }

    } else {
        fprintf(stderr, ROJO "fork() error: %s\n" RESET, strerror(errno));
        return -1;
    }

    return 0;
}


/* ==================== NIVELL 3 executar desde un fitxer == */

int internal_source(char **args) {
    //comprovar que nomes passam un nom de fitxer
    if (args[1] == NULL || args[2] != NULL) {
        fprintf(stderr, ROJO "Error de sintaxis. Uso: source <nombre_fichero>\n" RESET);
        //comanda interna processada
        return 1;
    }

    FILE *f = fopen(args[1], "r");
    if (f == NULL) {
        fprintf(stderr, ROJO "fopen() error: %s\n" RESET, strerror(errno));
        return 1;
    }

    //llegim cada linia del fitxer
    char line[COMMAND_LINE_SIZE];

    //anam llegint fins que s'acabi el fitxer
    while (fgets(line, COMMAND_LINE_SIZE, f) != NULL) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        #if DEBUGN3
        fprintf(stderr, GRIS "[internal_source()→ LINE: %s]\n" RESET, line);
        #endif

        fflush(f);
        //executam la linia com si l'haguessim escrit pel teclat
        execute_line(line);
    }

    fclose(f);
    return 1;
}

int check_internal(char **args){
    if (args == NULL || args[0] == NULL) {
        return 0;
    }

    //revisam quina comanda interna és i cridam a la funció corresponent
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
    if (strcmp(args[0], "exit") == 0) {
        printf("Bye Bye\n");
        exit(0);
    }

    return 0;
}

/* ==================== main ==================== */

int main(int argc, char *argv[]){
    char line[COMMAND_LINE_SIZE];

    //inicialitzar la taula de treballs tot a zero
    for (int i = 0; i < N_JOBS; i++) {
        jobs_list[i].pid = 0;
        jobs_list[i].estado = 'N';
        memset(jobs_list[i].cmd, '\0', COMMAND_LINE_SIZE);
    }
    n_job = 0;

    //guardar el nom amb que s'ha cridat al minishell
    memset(mi_shell, '\0', COMMAND_LINE_SIZE);
    if (argc > 0 && argv[0] != NULL) {
        strncpy(mi_shell, argv[0], COMMAND_LINE_SIZE - 1);
    } else {
        strncpy(mi_shell, "minishell", COMMAND_LINE_SIZE - 1);
    }

    //registram els gestors de senyals perquè no ens matin el shell fàcilment
    signal(SIGCHLD, reaper);
    signal(SIGINT,  ctrlc);
    signal(SIGTSTP, ctrlz);

    //bucle infinit: llegir i executar, així tot el rato
    while(1){
        if(read_line(line)){
            execute_line(line);
        }
    }
    return 0;
}