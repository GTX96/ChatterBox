/**
 * @file chatty_clients.c
 * @brief Implementazione delle funzioni per la gestione dei client da parte del server chatty.
 * @author Gionatha Sturba 531274
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 * originale dell'autore
 */

#include<stdlib.h>
#include<errno.h>
#include<pthread.h>
#include<string.h>
#include"message.h"
#include"chatty_task.h"
#include"config.h"
#include"ops.h"
#include"utenti.h"
#include"gruppi.h"
#include"messaggi_utenti.h"
#include"utils.h"

#define DEBUG

/* FUNZIONI INTERFACCIA */
int chatty_client_manager(message_t *message,int fd,chatty_arg_t *chatty)
{
    int rc;
    char *sender_name = message->hdr.sender;
    char *receiver_name = message->data.hdr.receiver;
    op_t op = message->hdr.op;
    utenti_registrati_t *utenti = chatty->utenti;
    gruppi_registrati_t *gruppi = chatty->gruppi;
    utente_t *sender;

    #ifdef DEBUG
        printf("op:%d\n",op);
    #endif

    //analizzo richiesta del client
    switch (op)
    {
        case REGISTER_OP:

            rc = registraUtente(sender_name,fd,utenti);

            //se l'utente risulta GIA' essere registrato con quel nick
            if(rc == 1)
            {
                #ifdef DEBUG
                    printf("register: 1 fail message\n");
                #endif
                rc = send_fail_message(fd,OP_NICK_ALREADY,utenti);
            }
            //registrazione utente fallita..
            else if(rc == -1)
            {
                //..nome non valido o elenco utenti pieno
                if(errno == EPERM || errno == ENOMEM)
                {
                    #ifdef DEBUG
                        printf("register: 2 fail message\n");
                    #endif
                    rc = send_fail_message(fd,OP_FAIL,utenti);
                }
                //..errore interno registrazione
                else{
                    return -1;
                }
            }
            //tutto andato bene,mando messaggio di ok al client e lista di utenti connessi
            else{
                rc = sendUserOnline(fd,utenti);
            }

            //errore operazione precedente
            error_handler_1(rc,-1,-1);

            break;

        case UNREGISTER_OP:

            rc = deregistraUtente(sender_name,utenti);

            //se l'utente risulta NON essere registrato con quel nick
            if(rc == 1)
            {
                #ifdef DEBUG
                    printf("deregister: 1 fail message\n");
                #endif
                rc = send_fail_message(fd,OP_NICK_UNKNOWN,utenti);
            }
            //registrazione utente fallita
            else if(rc == -1)
            {
                //..nome non valido
                if(errno == EPERM)
                {
                    #ifdef DEBUG
                        printf("deregister: 2 fail message\n");
                    #endif
                    rc = send_fail_message(fd,OP_FAIL,utenti);
                }
                //..errore interno deregistrazione
                else{
                    return -1;
                }
            }
            //operazione corretta, mando messaggio di OP_OK
            else{
                rc = send_ok_message(fd,"",0);
            }

            //errore operazione precedente
            error_handler_1(rc,-1,-1);

            break;

        case CONNECT_OP:

            rc = connectUtente(sender_name,fd,utenti);

            //utente non trovato
            if(rc == 1)
            {
                #ifdef DEBUG
                    printf("connect: 1 fail message\n");
                #endif
                rc = send_fail_message(fd,OP_NICK_UNKNOWN,utenti);
            }
            //connessione utente fallita
            else if(rc == -1)
            {
                //..nome non valido o utente gia' connesso
                if(errno == EPERM)
                {
                    #ifdef DEBUG
                        printf("connect: 2 fail message\n");
                    #endif
                    rc = send_fail_message(fd,OP_FAIL,utenti);
                }
                //..errore interno connessione
                else{
                    return -1;
                }
            }
            //invio OK,e la lista utenti
            else{
                rc = sendUserOnline(fd,utenti);
            }

            //errore operazione precedente
            error_handler_1(rc,-1,-1);

            break;

        case USRLIST_OP:

            //controllo sender sia registrato ed online
            sender = checkSender(sender_name,utenti,NULL);

            if(sender == NULL)
            {
                //se il nome del sender non e' valido,oppure il sender non e' online,oppure non e' registrato
                if(errno == EPERM || errno == ENETDOWN || errno == 0)
                {
                    #ifdef DEBUG
                        printf("userlist: 1 fail message\n");
                    #endif
                    rc = send_fail_message(fd,OP_FAIL,utenti);
                }
                //errore checkSender
                else{
                    return -1;
                }
            }
            //altrimenti sender valido,invio risposta e rilascio lock dell'utente
            else{
                pthread_mutex_unlock(&sender->mtx);
                rc = sendUserOnline(fd,utenti);
            }

            //controllo esito messaggio inviato
            error_handler_1(rc,-1,-1);

            break;

        case POSTTXT_OP:

            rc = inviaMessaggioUtente(sender_name,receiver_name,message->data.buf,message->data.hdr.len,TEXT_ID,utenti);

            //in caso di errore
            error_handler_1(rc,-1,-1);

            break;

        case POSTTXTALL_OP:

            rc = inviaMessaggioUtentiRegistrati(sender_name,message->data.buf,message->data.hdr.len,utenti);

            //in caso di errore
            error_handler_1(rc,-1,-1);
            break;

        case POSTFILE_OP:

            rc = inviaMessaggioUtente(sender_name,receiver_name,message->data.buf,message->data.hdr.len,FILE_ID,utenti);

            //in caso di errore
            error_handler_1(rc,-1,-1);

            break;

        case GETFILE_OP:

            rc = getFile(sender_name,message->data.buf,utenti);

            //in caso di errore
            error_handler_1(rc,-1,-1);

            break;

        case GETPREVMSGS_OP:

            rc = inviaMessaggiRemoti(sender_name,utenti);

            //in caso di errore
            error_handler_1(rc,-1,-1);

            break;

        case CREATEGROUP_OP:

            //controllo sender sia registrato ed online
            sender = checkSender(sender_name,utenti,NULL);

            if(sender == NULL)
            {
                //se il nome del sender non e' valido,oppure il sender non e' online,oppure non e' registrato
                if(errno == EPERM || errno == ENETDOWN || errno == 0)
                {
                    rc = send_fail_message(fd,OP_FAIL,utenti);
                }
                //errore checkSender
                else{
                    return -1;
                }
            }
            //altrimenti sender valido,invio risposta e rilascio lock dell'utente
            else{

                rc = RegistraGruppo(sender,receiver_name,gruppi);

                //rilacio lock sul sender
                pthread_mutex_unlock(&sender->mtx);

                //gruppo create correttamente
                if(rc == 0)
                {
                    rc = send_ok_message(fd,"",0);
                }

                //caso in cui l'utente e' iscritto a troppi gruppi oppure non c'e' piu spazio per registrare il gruppo
                //oppure il gruppo risulta gia' essere registrato con quel nome
                if( (rc == -1 && (errno == ENOMEM || errno == EPERM)) || rc == 1 )
                {
                    rc = send_fail_message(fd,OP_FAIL,utenti);
                }

            }

            //controllo esito messaggio inviato
            error_handler_1(rc,-1,-1);

            break;

        case ADDGROUP_OP:

            //controllo sender sia registrato ed online
            sender = checkSender(sender_name,utenti,NULL);

            if(sender == NULL)
            {
                //se il nome del sender non e' valido,oppure il sender non e' online,oppure non e' registrato
                if(errno == EPERM || errno == ENETDOWN || errno == 0)
                {
                    rc = send_fail_message(fd,OP_FAIL,utenti);
                }
                //errore checkSender
                else{
                    return -1;
                }
            }
            else
            {
                rc = iscrizioneGruppo(sender,receiver_name,gruppi);

                //rilacio lock sul sender
                pthread_mutex_unlock(&sender->mtx);

                //gruppo create correttamente
                if(rc == 0)
                {
                    rc = send_ok_message(fd,"",0);
                }

                //caso in cui l'utente e' iscritto a troppi gruppi oppure e' gia registrato a questo gruppo
                //oppure il gruppo risulta non esistere
                if( (rc == -1 && (errno == EALREADY || errno == EPERM)) || rc == 1 )
                {
                    rc = send_fail_message(fd,OP_FAIL,utenti);
                }
            }

            //controllo esito messaggio inviato
            error_handler_1(rc,-1,-1);

            break;

        case DELGROUP_OP:

            //controllo sender sia registrato ed online
            sender = checkSender(sender_name,utenti,NULL);

            if(sender == NULL)
            {
                //se il nome del sender non e' valido,oppure il sender non e' online,oppure non e' registrato
                if(errno == EPERM || errno == ENETDOWN || errno == 0)
                {
                    rc = send_fail_message(fd,OP_FAIL,utenti);
                }
                //errore checkSender
                else{
                    return -1;
                }
            }
            else
            {
                rc = disiscrizioneGruppo(sender,receiver_name,gruppi);

                //rilacio lock sul sender
                pthread_mutex_unlock(&sender->mtx);

                //gruppo create correttamente
                if(rc == 0)
                {
                    rc = send_ok_message(fd,"",0);
                }

                //caso in cui l'utente e' iscritto a troppi gruppi oppure e' gia registrato a questo gruppo
                //oppure il gruppo risulta non esistere
                if( (rc == -1 && (errno == ENOENT)) || rc == 1 )
                {
                    rc = send_fail_message(fd,OP_FAIL,utenti);
                }
            }

            //controllo esito messaggio inviato
            error_handler_1(rc,-1,-1);

            break;

        default:

            #ifdef DEBUG
                printf("default:sender = %s op: %d\n",sender_name,op);
                printf("default: 1 fail message\n");
            #endif
            rc = send_fail_message(fd,OP_FAIL,utenti);

            //controllo esito messaggio inviato
            error_handler_1(rc,-1,-1);
        break;
    }

    //libero eventuale memoria allocata nel buffer del messaggio per la lettura
    free(message->data.buf);

    mostraGruppi(gruppi);

    //client gestito correttamente
    return 0;
}

int chatty_disconnect_client(int fd,utenti_registrati_t *utenti)
{
    return disconnectUtente(fd,utenti);
}

int chatty_clients_overflow(int fd,utenti_registrati_t *utenti)
{
    //mando OP_FAIL per questo genere di errore
    #ifdef DEBUG
        printf("client overflow: fail message\n");
    #endif
    return send_fail_message(fd,OP_FAIL,utenti);
}
