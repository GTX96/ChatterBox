/**
 * @file connections.c
 * @brief Implementazione comunicazione client server
 * @author Gionatha Sturba 531274
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 * originale dell'autore
 */

#include<unistd.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<errno.h>
#include<sys/un.h>
#include<string.h>
#include"connections.h"
#include"config.h"
#include"utils.h"

/**
 * @function connection_closed
 * @brief Controlla se la connessione con un client risulta essere terminata
 * @parama read_result valore ritornato dalla read
 * @return 1 connessione chiusa,0 connessione aperta.
 */
static inline int connection_closed(int read_result)
{
    //caso in cui la connessione con il client e' terminata
    if(read_result == 0 || ( (read_result == -1) && (errno == ECONNRESET || errno == EPIPE) ))
    {
        //risetto errno a 0
        errno = 0;
        return 1;
    }
    else
        return 0;
}

int openConnection(char* path, unsigned int ntimes, unsigned int secs)
{
    int fd;
    struct sockaddr_un sa;
    int retry = 0;

    //controllo parametri
    if(path == NULL || secs > MAX_SLEEPING || ntimes > MAX_RETRIES)
    {
        errno = EINVAL;
        return -1;
    }

    //inizializzo indirizzo
    strncpy(sa.sun_path,path,UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;

    //descrittore per la comunicazione con il server
    fd = socket(AF_UNIX,SOCK_STREAM,0);

    //errore socket
    error_handler_1(fd,-1,-1);

    //tento la connessine al server
    while(connect(fd,(struct sockaddr *)&sa,sizeof(sa)) == -1 && retry <= ntimes)
    {
        //server offline,tento di riconnettermi
        if(errno == ENOENT)
        {
            sleep(secs);
            retry++;
        }
        //problema di connessine
        else
        {
            return -1;
        }
    }

    //numero di retry superato
    if(retry > ntimes)
    {
        return -1;
    }

    //tutto ok,ritorno descrittore connessione
    return fd;
}

int readHeader(long connfd, message_hdr_t *hdr)
{
    int rc;
    //controllo parametri
    if(connfd < 0 || hdr == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    rc = read(connfd,hdr,sizeof(message_hdr_t));

    //connessione chiusa
    if(connection_closed(rc))
        return 0;
    //ritorno esito della read
    else
        return rc;
}

int readData(long fd, message_data_t *data)
{
    int rc;

    //controllo parametri
    if(fd < 0 || data == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    //leggo prima data header
    rc = read(fd,&data->hdr,sizeof(message_data_hdr_t));

    #ifdef DEBUG
        printf("readData 1 rc: %d\n",rc);
    #endif

    //connessione chiusa oppure errore nella read
    if(connection_closed(rc))
    {
        return 0;
    }
    else{
        //errore nella read
        if(errno != 0)
            return -1;
    }

    //se arrivo qui la connessione con il client e' ancora aperta

    //alloco spazio per il buffer del messaggio
    size_t len = (data->hdr.len);
    data->buf = malloc(len);
    memset(data->buf,0,len);

    //allocazione andata male
    error_handler_1(data->buf,NULL,-1);

    int byte_read = 0;

    while(byte_read < len)
    {
        rc = read(fd,(data->buf) + byte_read ,1);

        if(connection_closed(rc))
            break;

        byte_read += rc;
    }

    //errore nella read
    if(errno != 0)
        return -1;

    #ifdef DEBUG
        printf("readData 2 rc: %d\n",byte_read);
    #endif

    return byte_read;
}

int readMsg(long fd, message_t *msg)
{
    int rc;
    int byte_read = 0;

    //controllo parametri
    if(fd < 0 || msg == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    rc = readHeader(fd,&msg->hdr);

    //controllo connessione chiusa oppure errore nel readHeader
    if(rc <= 0)
    {
        return rc;
    }

    //altrimenti incremento numero di byte letti
    byte_read += rc;

    rc = readData(fd,&msg->data);

    //errore lettura data
    if(rc == -1)
        return -1;

    //incremento numero di byte letti
    byte_read += rc;

    return byte_read;
}

int sendData(long fd, message_data_t *msg)
{
    #ifdef DEBUG
        printf("inzio sendData\n" );
    #endif
    int rc;

    //controllo parametri
    if(fd < 0 || msg == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    //mando prima header,che contiene la lunghezza del buffer
    rc = write(fd,&msg->hdr,sizeof(message_data_hdr_t));

    #ifdef DEBUG
        printf("sendData 1 rc: %d\n",rc);
    #endif

    if(connection_closed(rc))
    {
        return 0;
    }

    //esito prima write
    error_handler_1(rc,-1,-1);

    //poi mando il buffer
    rc = write(fd,msg->buf,msg->hdr.len * sizeof(char));

    #ifdef DEBUG
        printf("sendData 2 rc: %d\n",rc);
    #endif

    if(connection_closed(rc))
    {
        return 0;
    }

    //esito seconda write
    error_handler_1(rc,-1,-1);

    #ifdef DEBUG
        printf("fine sendData\n" );
    #endif

    //tutto andato ok
    return rc;
}

int sendHeader(long fd,message_hdr_t *hdr)
{
    int rc;

    //setto errno a 0;
    errno = 0;

    //controllo parametri
    if( fd < 0 || hdr == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    //mando header
    rc =  write(fd,hdr,sizeof(message_hdr_t));


    if(connection_closed(rc))
    {
        return 0;
    }

    //esito write
    error_handler_1(rc,-1,-1);

    //tutto ok
    return rc;
}

int sendRequest(long fd, message_t *msg)
{
    int rc;

    //controllo parametri
    if(fd < 0 || msg == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    rc = sendHeader(fd,&msg->hdr);

    //controllo esito sendHeader
    error_handler_1(rc,-1,-1);

    //mando data del messaggio
    rc = sendData(fd,&msg->data);

    return rc;
}
