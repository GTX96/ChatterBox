/**
 * @file  gestione_utenti.c
 * @brief Implementazione modulo per la gestione degli utenti di chatty
 * @author Gionatha Sturba 531274
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 * originale dell'autore
 */
#include<string.h>
#include<stdio.h>
#include<pthread.h>
#include<stdlib.h>
#include<errno.h>
#include<ctype.h>
#include<unistd.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<dirent.h>
#include<math.h>
#include<string.h>
#include"utenti.h"
#include"config.h"
#include"utils.h"

#define DEBUG
/* FUNZIONI DI SUPPORTO */
/**
 * @function remove_directory
 * @brief Rimuove una directory e tutti i file al suo interno.
 * @param path path della directory da eiminare
 * @return 0 in caso di successo, -1 in caso di errore settando errno
 */
static int remove_directory(char *path)
{
    DIR *d = opendir(path);
    size_t path_len = strlen(path);
    int r = 0; //variabile per esito ricorsione
    int rc;

    //errore apertura directory
    error_handler_1(d,NULL,-1);

    struct dirent *p; //puntatore agli elementi dentro una directory

    while (r == 0 && (p=readdir(d)) != NULL)
    {
        int r2 = 0; //altra variabile per ricorsione
        char *buf; //buffer dove salvo i path per le sottodirectory
        size_t len; //lunghezza buffer

        //salto le cartelle . e ..
        if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
            continue;

        len = path_len + strlen(p->d_name) + 2;
        buf = malloc(len);

        if(buf == NULL)
        {
            closedir(d);
            return -1;
        }

        struct stat statbuf;

        //scrivo dentro al buffer il path della sottodirectory che sto per analizzare
        rc = snprintf(buf, len, "%s/%s", path, p->d_name);

        //errore snprintf
        if(rc < 0)
        {
            free(buf);
            closedir(d);
            errno = EIO;
            return -1;
        }

        //analizzo con stat l'elemento corrente
        rc = stat(buf, &statbuf);

        //errore stat
        if(rc == -1)
        {
            free(buf);
            closedir(d);
            return -1;
        }

        //se si tratta di una directory,ricorsivamente la elimino
        if (S_ISDIR(statbuf.st_mode))
            r2 = remove_directory(buf);
        else //se si tratta di un semplice file,lo elimino
            r2 = unlink(buf);

        free(buf);

        //aggiorno r con l'esito della ricorsione
        r = r2;
    }

    //casi di errore
    if(r == -1 || (p == NULL && errno != 0))
    {
        closedir(d);
        return -1;
    }

    //chiudo directory attuale
    closedir(d);

   //caso di directory vuota,posso ora eliminarla con rmdir
   if (r == 0)
   {
      r = rmdir(path);
   }

   return r;
}

/**
 * @function add_name
 * @brief Aggiunge il nome dell'utente all'interno del buffer
 * @param buffer puntatore al buffer
 * @param buffer_size puntatore alla size attuale allocata dal buffer
 * @param new_size puntatore alla nuova size del buffer,dopo aver inserito il nome
 * @param name nickname da inserire nel buffer
 * @return puntatore alla prossima posizione libera per inserire nel buffer,altrimenti NULL.
 */
static char *add_name(char buffer[],size_t *buffer_size,int *new_size,const char name[])
{
    int i = 0; //per scorrere stringhe e contare numero di byte scritti

    //finche non arrivo all'ultima lettera del nickname, ed ho spazio nel buffer
    while(name[i] != '\0' && *buffer_size > 0)
    {
        //inserisco lettera
        buffer[i] = name[i];

        //aggiorno indice per scorrere il nome
        i++;

        //decremento dimensione del buffer
        --*buffer_size;
    }

    //se non ho piu spazio nel buffer
    if(*buffer_size == 0)
        return NULL;
    else
    {
        //metto carattere terminatore
        buffer[i] = '\0';

        //decremento dimensione del buffer
        --*buffer_size;

        //aggiorno indici
        ++i;
    }

    //aggiungo spazi dopo il nick,per occupare tutti i byte
    while( ( (MAX_NAME_LENGTH + 1 - (i)) > 0 ) && *buffer_size > 0)
    {
        buffer[i] = ' ';

        ++i;

        --*buffer_size;
    }

    //se non ho piu' spazio nel buffer,e non ho finito di mettere gli spazi
    if(*buffer_size == 0 && (MAX_NAME_LENGTH + 1 - (i)) != 0 )
        return NULL;

    //altrimenti aggiorno la nuova size del buffer..
    *new_size += i;

    //ritorno la posizione attuale nel buffer,per poter inserire nuovamente
    return (buffer + i);
}

/* FUNZIONI INTERFACCIA */
utenti_registrati_t *inizializzaUtentiRegistrati(int msg_size,int file_size,int hist_size,struct statistics *statistics,pthread_mutex_t *mtx_stat,char *dirpath)
{
    int rc;
    struct stat st;

    //controllo parametri
    if(msg_size <= 0 || file_size <= 0 || hist_size <= 0 || statistics == NULL || mtx_stat == NULL ||
        dirpath == NULL || strlen(dirpath) > MAX_SERVER_DIR_LENGTH || numOfDigits(hist_size) > MAX_ID_LENGTH )
    {
        errno = EINVAL;
        return NULL;
    }

    utenti_registrati_t *utenti = malloc(sizeof(utenti_registrati_t));

    //allocazione andata male
    error_handler_1(utenti,NULL,NULL);

    //alloco spazio per elenco,iniziaizzando valori statici all'interno
    utenti->elenco = calloc(MAX_USERS,sizeof(utente_t));

    //esito allocazione
    if(utenti->elenco == NULL)
    {
        free(utenti);
        return NULL;
    }

    //inizializzo tutti i mutex
    for (size_t i = 0; i < MAX_USERS; i++)
    {
        rc = pthread_mutex_init(&utenti->elenco[i].mtx,NULL);

        //errore init mutex
        if(rc)
        {
            //distruggo mutex creati fin ora
            for (size_t j = 0; j < i; j++)
            {
                pthread_mutex_destroy(&utenti->elenco[j].mtx);
            }

            free(utenti->elenco);
            free(utenti);

            errno = rc;
            return NULL;
        }
    }

    utenti->stat = statistics;
    utenti->mtx_stat = mtx_stat;
    utenti->max_msg_size = msg_size;
    utenti->max_file_size = file_size;
    utenti->max_hist_msgs = hist_size;
    strncpy(utenti->media_dir,dirpath,MAX_SERVER_DIR_LENGTH);

    //creo la directory del server per mantenere info sugli utenti,se non e' gia' presente
    if (stat(dirpath, &st) == -1)
    {
        errno = 0;
        rc = mkdir(dirpath, 0777);

        //errore creazione directory
        if(rc == -1)
        {
            int curr_error = errno;

            //dealloco tutto
            for (size_t i = 0; i < MAX_USERS; i++)
            {
                pthread_mutex_destroy(&utenti->elenco[i].mtx);
            }

            free(utenti->elenco);
            free(utenti);
            errno = curr_error;

            return NULL;
        }
    }

    return utenti;
}

utente_t *cercaUtente(char *name,utenti_registrati_t *Utenti,int *pos)
{
    int rc;

    //controllo parametri
    if(name == NULL)
    {
        errno = EPERM;
        return NULL;
    }

    if(Utenti == NULL)
    {
        errno = EINVAL;
        return NULL;
    }

    //se pos e' specificato,fin quando non lo trovo la posizione sara' -1
    if(pos != NULL)
        *pos = -1;

    //ottengo posizione tramite hash
    int hashIndex = hash(name,MAX_USERS);
    int count = 0; //contatore di supporto

    //prendo lock sull'utente che analizzo
    rc = pthread_mutex_lock(&Utenti->elenco[hashIndex].mtx);

    //errore lock
    error_handler_3(rc,NULL);

    //fin quando trovo utenti inizializzati
    while(Utenti->elenco[hashIndex].isInit)
    {
        //se i nick sono uguali,abbiamo trovato l'utente
        if(strcmp(Utenti->elenco[hashIndex].nickname,name) == 0)
        {
            //ritorno la posizione in cui si trova nell'elenco l'utente cercato
            if(pos != NULL)
                *pos = hashIndex;

            //ritorno puntatore all'utente senza rilasciare la lock
            return &Utenti->elenco[hashIndex];
        }

        //rilascio la lock dell'utente attuale per andare ad analizzare il prossimo utente
        pthread_mutex_unlock(&Utenti->elenco[hashIndex].mtx);

        //altrimenti proseguo nella ricerca,aggiornando l'indice
        ++hashIndex;
        //per non traboccare
        hashIndex %= MAX_USERS;
        count++;

        //caso in cui ho controllato tutti gli utenti e non ho trovato quello che mi interessava
        if(count == MAX_USERS)
        {
            break;
        }

        //prendo lock per prossimo utente
        rc = pthread_mutex_lock(&Utenti->elenco[hashIndex].mtx);

        //errore lock
        error_handler_3(rc,NULL);
    }

    //rilascio lock sull'utente su cui mi trovo
    pthread_mutex_unlock(&Utenti->elenco[hashIndex].mtx);

    //utente non registrato
    return NULL;
}

int registraUtente(char *name,unsigned int fd,utenti_registrati_t *Utenti)
{
    int rc;

    //controllo parametri
    if(name == NULL || strlen(name) > MAX_NAME_LENGTH)
    {
        errno = EPERM;
        return -1;
    }

    if(Utenti == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    //lock statistiche utenti
    rc = pthread_mutex_lock(Utenti->mtx_stat);

    //errore lock
    error_handler_3(rc,-1);

    //controllo che non abbiamo raggiunto il massimo degli utenti registrabili
    if(Utenti->stat->nusers == MAX_USERS)
    {
        pthread_mutex_unlock(Utenti->mtx_stat);
        errno = ENOMEM;
        return -1;
    }

    //rilascio lock statistiche
    pthread_mutex_unlock(Utenti->mtx_stat);

    //Controllo se il nick non sia gia' registrato
    utente_t *already_reg = cercaUtente(name,Utenti,NULL);

    //utente gia' registrato
    if(already_reg != NULL)
    {
        //rilascio lock utente trovato
        pthread_mutex_unlock(&already_reg->mtx);
        return 1;
    }
    else if(already_reg == NULL && errno != 0)
    {
        //c'e' stato un errore nella ricerca
        return -1;
    }

    //altrimenti nick non presente

    //hash per la ricerca
    int hashIndex = hash(name,MAX_USERS);

    //prendo lock sulla posizione ottenuta dall'hashing
    rc = pthread_mutex_lock(&Utenti->elenco[hashIndex].mtx);

    //errore lock
    error_handler_3(rc,-1);

    //finche trovo posizioni occupate,avanzo con l'indice
    while(Utenti->elenco[hashIndex].isInit)
    {
        //rilascio lock utente
        pthread_mutex_unlock(&Utenti->elenco[hashIndex].mtx);

        //aggiorno indice
        ++hashIndex;
        hashIndex %= MAX_USERS;

        //prendo lock prossimo utente
        rc = pthread_mutex_lock(&Utenti->elenco[hashIndex].mtx);

        //errore lock
        error_handler_3(rc,-1);
    }

    //posizione trovata, inserisco info relativo all'utente
    strncpy(Utenti->elenco[hashIndex].nickname,name,MAX_NAME_LENGTH);
    Utenti->elenco[hashIndex].isInit = 1;
    Utenti->elenco[hashIndex].isOnline = 1;
    Utenti->elenco[hashIndex].n_remote_message = 0;
    Utenti->elenco[hashIndex].fd = fd;
    Utenti->elenco[hashIndex].n_gruppi_iscritto = 0;

    //creo il path..
    rc = snprintf(Utenti->elenco[hashIndex].personal_dir,MAX_CLIENT_DIR_LENGHT,"%s/%s",Utenti->media_dir,name);

    //errore snprintf
    if(rc < 0)
    {
        //rilascio lock utente inserito
        pthread_mutex_unlock(&Utenti->elenco[hashIndex].mtx);
        errno = EIO;
        return -1;
    }

    //..e la cartella
    rc = mkdir(Utenti->elenco[hashIndex].personal_dir,0777);

    if(rc == -1)
    {
        //rilascio lock utente inserito
        pthread_mutex_unlock(&Utenti->elenco[hashIndex].mtx);
        return -1;
    }

    //rilascio lock utente inserito
    pthread_mutex_unlock(&Utenti->elenco[hashIndex].mtx);

    //lock statistiche utenti
    rc = pthread_mutex_lock(Utenti->mtx_stat);

    //errore lock
    error_handler_3(rc,-1);

    //incremento numero utenti registrati e utenti online
    ++(Utenti->stat->nusers);
    ++(Utenti->stat->nonline);

    //rilascio lock statistiche
    pthread_mutex_unlock(Utenti->mtx_stat);

    return 0;
}

int deregistraUtente(char *name,utenti_registrati_t *Utenti)
{
    int rc;

    utente_t *utente = cercaUtente(name,Utenti,NULL);

    if(utente == NULL)
    {
        //errore ricerca utente
        if(errno != 0)
            return -1;
        else//utente non registrato
            return 1;
    }

    //elimino la cartella personale dell'utente
    rc = remove_directory(utente->personal_dir);

    //setto flag per segnalare che la posizione da ora in poi e' libera
    utente->isInit = 0;

    //setto un fd invalido
    utente->fd = 0;

    //TODO rimuovere l'utente da tutti i gruppi a cui e' iscritto

    //rilascio lock utente
    pthread_mutex_unlock(&utente->mtx);

    //errore eliminazione directory
    error_handler_1(rc,-1,-1);

    //lock statistiche utenti
    rc = pthread_mutex_lock(Utenti->mtx_stat);

    //errore lock
    error_handler_3(rc,-1);

    //decremento utenti registrati e utenti online
    --(Utenti->stat->nusers);
    --(Utenti->stat->nonline);

    //rilascio lock statistiche
    pthread_mutex_unlock(Utenti->mtx_stat);

    return 0;
}

int connectUtente(char *name,unsigned int fd,utenti_registrati_t *Utenti)
{
    int rc;

    utente_t *utente;

    utente = cercaUtente(name,Utenti,NULL);

    if(utente == NULL)
    {
        //utente non registrato
        if(errno == 0)
            return 1;
        else //errore ricerca utente
            return -1;
    }

    //setto info su utente
    utente->isOnline = 1;
    utente->fd = fd;

    //rilascio lock utente
    pthread_mutex_unlock(&utente->mtx);

    //lock statistiche utenti
    rc = pthread_mutex_lock(Utenti->mtx_stat);

    //errore lock
    error_handler_3(rc,-1);

    //incremento numero utenti online
    ++(Utenti->stat->nonline);

    //rilascio lock statistiche
    pthread_mutex_unlock(Utenti->mtx_stat);

    return 0;
}

int disconnectUtente(unsigned int fd,utenti_registrati_t *Utenti)
{
    int rc,i = 0,find = 0;

    while(i < MAX_USERS && !find)
    {
        utente_t *utente = &Utenti->elenco[i];

        pthread_mutex_lock(&utente->mtx);

        if(utente->isInit)
        {
            //fd combacia
            if(utente->fd == fd)
            {
                utente->isOnline = 0;
                utente->fd = 0;
                find = 1;
            }
        }

        pthread_mutex_unlock(&utente->mtx);

        //se non l'abbiamo ancora trovato continuiamo a scorrere
        if(!find)
            i++;
    }

    //se non lo abbiamo trovato ritorniamo normalmente
    if(i == MAX_USERS)
        return 0;

    //lock statistiche utenti
    rc = pthread_mutex_lock(Utenti->mtx_stat);

    //errore lock
    error_handler_3(rc,-1);

    --(Utenti->stat->nonline);

    //rilascio lock statistiche
    pthread_mutex_unlock(Utenti->mtx_stat);

    return 0;
}

void mostraUtenti(utenti_registrati_t *Utenti)
{
    int rc;

    if(Utenti == NULL)
    {
        return;
    }


    printf("######################\n");

    for (size_t i = 0; i < MAX_USERS; i++)
    {
        //lock su utente
        rc = pthread_mutex_lock(&Utenti->elenco[i].mtx);

        //errore lock
        if(rc)
        {
            return;
        }

        //posizione non  vuota
        if(Utenti->elenco[i].isInit)
        {
            printf("%s ",Utenti->elenco[i].nickname);

            //online?
            if(Utenti->elenco[i].isOnline)
                printf("online ");
            else
                printf("offline ");

            printf("fd: %d\n",Utenti->elenco[i].fd);

        }

        //unlock su utente
        pthread_mutex_unlock(&Utenti->elenco[i].mtx);
    }

    printf("######################\n");

}

int mostraUtentiOnline(char *buff,size_t *size_buff,int *new_size,utenti_registrati_t *Utenti)
{
    int rc;
    char *parser; //puntatore di supporto

    if(buff == NULL || Utenti == NULL || size_buff == NULL || new_size == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    parser = buff;

    //scorro gli utenti registrati
    for (int i = 0; i < MAX_USERS; i++)
    {
        //lock su utente attuale
        rc = pthread_mutex_lock(&Utenti->elenco[i].mtx);

        //errore lock
        error_handler_3(rc,-1);

        //posizione non vuota
        if(Utenti->elenco[i].isInit)
        {
            //e' online
            if(Utenti->elenco[i].isOnline)
            {
                //aggiungo il nome di questo utente alla stringa degli utenti online,cioe' il buffer
                parser = add_name(parser,size_buff,new_size,Utenti->elenco[i].nickname);

                //caso in cui lo spazio nel buffer e' terminato
                if(parser == NULL)
                {
                    //unlock su utente
                    pthread_mutex_unlock(&Utenti->elenco[i].mtx);
                    buff = NULL;
                    errno = ENOBUFS;
                    return -1;
                }
            }
        }

        //unlock su utente
        pthread_mutex_unlock(&Utenti->elenco[i].mtx);
    }

    return 0;
}

int eliminaElencoUtenti(utenti_registrati_t *Utenti)
{
    int rc;

    if(Utenti == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    //dealloco tutti i mutex relativi agli utente
    for (size_t i = 0; i < MAX_USERS; i++)
    {
        rc = pthread_mutex_destroy(&Utenti->elenco[i].mtx);

        //esito init
        if(rc)
        {
            free(Utenti->elenco);
            free(Utenti);
            errno = rc;
            return -1;
        }
    }

    //libero memoria
    free(Utenti->elenco);
    free(Utenti);

    return 0;
}
