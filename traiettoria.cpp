/**
  *
  * (c) Riccardo Bertossa, 2019
  *
  *   Use at  your own risk.
  *
  *   If you modified the code, I could be happy if you contribute on github!
  *
**/





#include "config.h"

#include "traiettoria.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <iostream>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <cmath>
#include <cerrno>
#include <algorithm>
#ifdef HAVEfftw3
#include <fftw3.h>
#else
#include <fftw.h>
#endif

#include "cronometro.h"
#include "lammps_struct.h"
#include <stdexcept>





Traiettoria::Traiettoria(std::string filename)
{
    struct stat sb;

    fsize=0;
    file=0;
    timestep_corrente=0;
    timestep_indicizzato=0;
    timestep_finestra=0;
    tstep_size=0;
    natoms=0;
    n_timesteps=0;
    timesteps=0;
    timesteps_lammps=0;
    buffer_tipi=0;
    buffer_tipi_id=0;
    buffer_posizioni=0;
    buffer_velocita=0;
    buffer_scatola=0;
    ntypes=0;
    ok=false;
    dati_caricati=false;
    indexed_all=false;
    min_type=0;
    max_type=0;
    masse=0;
    cariche=0;
    wrap_pbc=false;
    calculate_center_of_mass=false;
    buffer_posizioni_cm=0;
    buffer_velocita_cm=0;

    fd=open(filename.c_str(), O_RDONLY);
    if (fd==-1) {
        throw std::runtime_error("Error opening the trajectory \""+filename+"\"\n");
    }

    if (fstat(fd, &sb)==-1) {
        close(fd);
        fd=-1;
        throw std::runtime_error("Error in finding trajectory file size \""+filename+"\"\n");
    } else {
        fsize=sb.st_size;
        std::cerr << "Trajectory file size \""<< filename << "\": "<< fsize<<"\n";
    }

    file = (char*) mmap(0,fsize,PROT_READ,MAP_PRIVATE,fd,0);

    if (file==MAP_FAILED) {
        fd=-1;
        close(fd);
        throw std::runtime_error("Chiamata mmap fallita per il file \""+filename+"\".\n");
    }

    Intestazione_timestep * t0;


    size_t dimensione_timestep = leggi_pezzo_intestazione(0,t0);
    natoms=t0->natoms;

    buffer_tipi=new int [natoms];
    buffer_tipi_id=new int [natoms];

    for (unsigned int i=0;i<natoms;i++) {
        buffer_tipi[i]=buffer_tipi_id[i]=-1;
    }

    init_buffer_tipi();

    //stima del numero di timesteps
    n_timesteps=sb.st_size/dimensione_timestep;

    //alloco la memoria per l'indice
    timesteps=new size_t [n_timesteps];
    timesteps_lammps= new int64_t [n_timesteps];
    for (unsigned int i=0;i<n_timesteps;i++){
        timesteps[i]=0;
        timesteps_lammps[i]=0;
    }

    //trovo la dimensione della pagina (per allineare l'indirizzo di memoria da consigliare con madvise
    pagesize=sysconf(_SC_PAGESIZE);

    ok=true;
}


void Traiettoria::init_buffer_tipi() {

   Intestazione_timestep * intestazione=0;
   Chunk * pezzi=0;
   size_t offset = leggi_pezzo(0,intestazione,pezzi);
   natoms=intestazione->natoms;
   id_map.clear();
   // controlla che gli id degli atomi siano consistenti e crea una mappa
   unsigned int id_=0;
   for (unsigned int ichunk=0;ichunk<intestazione->nchunk;ichunk++){
       for (int iatomo=0;iatomo<pezzi[ichunk].n_atomi;iatomo++) {
           if (id_>=natoms) {
               std::cerr << "Errore: la dichiarazione del numero di atomi non corrisponde alla somma degli atomi letti in ciascun chunk!\n";
               abort();
           }
           int id=round(pezzi[ichunk].atomi[iatomo].id);
           if (id_map.find(id)==id_map.end()){
               id_map[id]=id_;
               id_++;
           }else {
               std::cerr << "Errore nella lettura degli id degli atomi: ho trovato un id doppio! ("<<id<<")\n";
               abort();
           }
       }
   }

   std::cerr << "Tipi e id degli atomi letti:\n";

        for (unsigned int ichunk=0;ichunk<intestazione->nchunk;ichunk++){
            for (int iatomo=0;iatomo<pezzi[ichunk].n_atomi;iatomo++) {
                int id=id_map.at(round(pezzi[ichunk].atomi[iatomo].id));
                int tipo=round(pezzi[ichunk].atomi[iatomo].tipo);
                buffer_tipi[id]=tipo;
            }
        }

   get_ntypes();

   for (unsigned int ichunk=0;ichunk<intestazione->nchunk;ichunk++){
       for (int iatomo=0;iatomo<pezzi[ichunk].n_atomi;iatomo++) {
           int id=id_map.at(round(pezzi[ichunk].atomi[iatomo].id));
           int tipo=round(pezzi[ichunk].atomi[iatomo].tipo);
           std::cerr << id<<":\tid = " <<round(pezzi[ichunk].atomi[iatomo].id)<<
                              "\ttipo = "<<tipo<<"\titipo ="<< buffer_tipi_id[id] <<"\n";
       }
   }
   delete [] pezzi;


}

/*
void Traiettoria::set_calculate_center_of_mass(bool t){
    calculate_center_of_mass=t;
    //rialloca l'array se necessario

}

bool Traiettoria::get_calculate_center_of_mass(){

}
*/

/**
  * Restituisce l'indirizzo allineato alla memoria.
  * Allinea la memoria alla pagina precedente.
**/

size_t Traiettoria::allinea_offset(const size_t & offset /// memoria da allineare
                                   ,size_t & differenza  /// qui viene memorizzata la differenza necessaria all'allineamento
                                   ){
    if (offset < pagesize){
        differenza=offset;
        return 0;
    } else {
        differenza=offset%pagesize;
        return offset-differenza;
    }
}

Traiettoria::~Traiettoria(){
    delete [] timesteps;
    delete [] timesteps_lammps;
    delete [] buffer_tipi;
    delete [] buffer_tipi_id;
    delete [] masse;
    delete [] cariche;
    delete [] buffer_scatola;
    fftw_free(buffer_posizioni);
    fftw_free(buffer_posizioni_cm);
    fftw_free(buffer_velocita_cm);
    fftw_free(buffer_velocita);

    if (file != 0)
        munmap(file,fsize);

}

/**
  * Sistema i puntatori con i dati del timestep e
  * restituisce la dimensione in byte del pezzo letto
  * (quindi il pezzo successivo si troverà ad un offset di "partenza" + "il risultato di questa chiamata")
**/
size_t Traiettoria::leggi_pezzo(const size_t &partenza /// offset da cui partire (in "file")
                                ,Intestazione_timestep * &timestep /// qui viene restituito un oggetto Intestazione_timestep, DA NON DEALLOCARE DOPO (è dentro "file")
                                ,Chunk * &chunk /// qui viene restituito un array di oggetti Chunk, da deallocare dopo (chunk conterrà un array di Atomo, da non deallocare -- anche questo è dentro "file")
                                ){

    size_t letti=0;

    if (partenza+sizeof(Intestazione_timestep)>fsize) {
        std::cerr << "Errore! Sto cercando di leggere oltre la fine del file!\n";
        abort();
    }

    timestep=(Intestazione_timestep*) (file+partenza);
    letti+=sizeof(Intestazione_timestep);
    if (natoms != 0 && natoms!=timestep->natoms) {
        std::cerr << "Errore! Il numero di atomi è cambiato nella traiettoria! (LAMMPS timestep: "<< timestep->timestep<< ") \n";
        abort();
    }
    natoms=timestep->natoms;


    if (timestep->triclinic) {
        triclinic=true;
    } else {
        triclinic=false;
    }

    if (triclinic) {
        std::cerr << "Lettura del file con cella di tipo \"triclinc\" non implementata!\n";
        return 0;
    }

    chunk=new Chunk[timestep->nchunk];

    for (int i=0;i<timestep->nchunk;i++){
        if (*(int*)(file+partenza+letti)%NDOUBLE_ATOMO !=0 ) {
            std::cerr << "Errore nella determinazione del numero di atomi: il numero di dati nel pezzo non e' divisibile per il numero di dati per atomo.\n";
        }
        chunk[i].n_atomi=(*(int*)(file+partenza+letti))/NDOUBLE_ATOMO;
        letti+=sizeof(int);
        chunk[i].atomi=(Atomo*) (file+partenza+letti);
        letti+=chunk[i].n_atomi*sizeof(Atomo);

        // qui chunk è configurato correttamente e contiene i dati
    }

    return letti;

}

/**
  * Sistema i puntatori con i dati del timestep e
  * restituisce la dimensione in byte del pezzo letto, comprensiva dei chunk con i dati veri e propri che però non vengono restituiti
  * (quindi il pezzo successivo si troverà ad un offset di "partenza" + "il risultato di questa chiamata")
**/
size_t Traiettoria::leggi_pezzo_intestazione(const size_t &partenza /// offset da cui partire (in "file")
                                ,Intestazione_timestep * &timestep /// qui viene restituito un oggetto Intestazione_timestep, DA NON DEALLOCARE DOPO (è dentro "file")
                                ){

    if (partenza+sizeof(Intestazione_timestep)>fsize) {
        std::cerr << "Errore! Sto cercando di leggere oltre la fine del file!\n";
        abort();
    }
    size_t letti=0;

    timestep=(Intestazione_timestep*) (file+partenza);
    letti+=sizeof(Intestazione_timestep);

    if (natoms != 0 && natoms!=timestep->natoms) {
        std::cerr << "Errore! Il numero di atomi è cambiato nella traiettoria! (LAMMPS timestep: "<< timestep->timestep<< ") \n";
        abort();
    }
    natoms=timestep->natoms;


    if (timestep->triclinic) {
        triclinic=true;
    } else {
        triclinic=false;
    }

    if (triclinic) {
        std::cerr << "Lettura del file con cella di tipo \"triclinc\" non implementata!\n";
        abort();
        return 0;
    }


    for (int i=0;i<timestep->nchunk;i++){
        int n_double=(* (int*)(file+partenza+letti)); // numero di dati double nel pezzo he sto per leggere
        letti+=sizeof(int);
        letti+=n_double*sizeof(double);
    }

    return letti;

}


Traiettoria::Errori Traiettoria::imposta_dimensione_finestra_accesso(const int &Ntimesteps){
    if (!ok) {
        std::cerr << "mmap non inizializzata correttamente!\n";
        return non_inizializzato;
    }

    if (timestep_finestra==Ntimesteps)
        return Ok;

    dati_caricati=false;
    if (timestep_finestra!=Ntimesteps){

        //questo alloca la memoria in modo corretto per permettere l'utilizzo delle istruzioni SIMD in fftw3
        fftw_free(buffer_posizioni);
        fftw_free(buffer_posizioni_cm);
        fftw_free(buffer_velocita_cm);
        fftw_free(buffer_velocita);
        buffer_posizioni= (double*) fftw_malloc(sizeof(double)*natoms*3*Ntimesteps);
        buffer_posizioni_cm= (double*) fftw_malloc(sizeof(double)*3*Ntimesteps*ntypes);
        buffer_velocita_cm= (double*) fftw_malloc(sizeof(double)*3*Ntimesteps*ntypes);
        buffer_velocita=(double*) fftw_malloc(sizeof(double)*natoms*3*Ntimesteps);
        delete [] buffer_scatola;
        buffer_scatola= new double [6*Ntimesteps];
    }

    timestep_finestra=Ntimesteps;
    return Ok;

}

/**
  * alloca un array (timesteps) piu' lungo e copia il contenuto del vecchio indice in quello nuovo.
  * Inizializza a zero gli elementi in piu' che ci sono alla fine dell'array
**/
void Traiettoria::allunga_timesteps(unsigned int nuova_dimensione){
    if (nuova_dimensione<=n_timesteps)
        return;
    size_t *tmp=new size_t [nuova_dimensione];
    int64_t *tmp2=new int64_t [nuova_dimensione];
    for (unsigned int i=0;i<n_timesteps;i++){
        tmp[i]=timesteps[i];
        tmp2[i]=timesteps_lammps[i];
    }
    for (unsigned int i=n_timesteps;i<nuova_dimensione;i++){
        tmp[i]=0;
        tmp2[i]=0;
    }
    delete [] timesteps;
    delete [] timesteps_lammps;
    timesteps=tmp;
    timesteps_lammps=tmp2;
    n_timesteps=nuova_dimensione;

}

void Traiettoria::index_all() {

    if (timestep_indicizzato>=n_timesteps-1)
        return;

    int res=madvise(file,fsize,MADV_SEQUENTIAL);
    if ( res==-1) {
        std::cerr << "Errore in madvise MADV_SEQUENTIAL -- index_all(): "<< res<<"\n";
        perror(0);
    }
#ifdef DEBUG
    std::cerr << "Inizio a indicizzare tutti i timesteps... (può richiedere del tempo) ";
    std::cerr.flush();
    cronometro cron;
    cron.start();
#endif
    for (int itimestep=timestep_indicizzato+1;itimestep<n_timesteps;itimestep++){
        Intestazione_timestep * intestazione=0;
        size_t offset = leggi_pezzo_intestazione(timesteps[itimestep-1],intestazione);
        timesteps[itimestep]=timesteps[itimestep-1]+offset;
        timesteps_lammps[itimestep-1]=intestazione->timestep;
        timestep_indicizzato=itimestep-1;
    }

    //anche l'ultimo:
    if (timestep_indicizzato!=n_timesteps-1){
        Intestazione_timestep * intestazione=0;
        size_t offset = leggi_pezzo_intestazione(timesteps[n_timesteps-1],intestazione);
        timesteps_lammps[n_timesteps-1]=intestazione->timestep;
        timestep_indicizzato=n_timesteps-1;
    }
    res=madvise(file,fsize,MADV_NORMAL);
    if ( res==-1) {
        std::cerr << "Errore in madvise MADV_NORMAL -- index_all(): "<< res<<"\n";
        perror(0);
    }
#ifdef DEBUG
    cron.stop();
    std::cerr << " OK, tempo "<<cron.time()  << "s.\nIndicizzati tutti i timesteps fino a "<<timestep_indicizzato<<" compreso\n";
    std::cerr.flush();
#endif
}

Traiettoria::Errori Traiettoria::imposta_inizio_accesso(const int &timestep) {

    cronometro cron;
    cron.start();

    if (!ok) {
        std::cerr << "mmap non inizializzata correttamente!\n";
        return non_inizializzato;
    }

    if (timestep==timestep_corrente && dati_caricati)
        return Ok;

#ifdef DEBUG
    std::cerr << "Imposto l'accesso della traiettoria dal timestep "<< timestep << " al timestep "<<timestep+timestep_finestra  << " (escluso).\n";
    if (wrap_pbc)
        std::cerr << "Applico le condizioni al contorno periodiche.\n";
#endif
    //se timestep+timestep_finestra e' oltre il numero di timesteps permessi, alloca un nuovo array
    //solo se effettivamente c'e' la possibilita' che nella traiettoria ci siano ancora nuovi timesteps
    //(vuol dire che la stima del numero di timesteps svolta all'inizio era sbagliata)

    if (timestep+timestep_finestra>n_timesteps) {
        if (fsize-timesteps[timestep_corrente]<(timestep-timestep_corrente + timestep_finestra)*tstep_size) {
            allunga_timesteps(timestep+timestep_finestra+1);
        } else {
            std::cerr << "Non posso utilizzare timesteps oltre la fine del file! (forse le dimensioni della finestra sono troppo grandi oppure si e' andati troppo oltre)\n";
//            return oltre_fine_file;
        }
    }

    if (timestep>timestep_indicizzato) {

#ifdef DEBUG
        std::cerr << "Indicizzo i timesteps da "<< timestep_indicizzato+1 << " a " << timestep << "\n";
#endif
        //indicizza i timesteps che mancano
        for (int itimestep=timestep_indicizzato+1;itimestep<=timestep;itimestep++){
            Intestazione_timestep * intestazione=nullptr;
            size_t offset = leggi_pezzo_intestazione(timesteps[itimestep-1],intestazione);
            timesteps[itimestep]=timesteps[itimestep-1]+offset;
            timesteps_lammps[itimestep-1]=intestazione->timestep;
            timestep_indicizzato=itimestep;
        }
    }

    //avviso il kernel che tutta la zona del file precedente non mi serve più (e anche quella successiva, avviserò dopo)
    int res=madvise(file,timesteps[timestep],MADV_DONTNEED);
    if ( res==-1) {
        std::cerr << "Errore in madvise MADV_DONTNEED: "<< res<<"\n";
        perror(nullptr);
    }

    if (timestep < timestep_corrente && timesteps[timestep]+tstep_size*timestep_finestra*2 < fsize ) { // avviso che le zone di memoria successive non mi servono
        res=madvise(file+timesteps[timestep]+tstep_size*timestep_finestra*2,
                    fsize-timesteps[timestep]+tstep_size*timestep_finestra*2,MADV_DONTNEED);
        if ( res==-1) {
            std::cerr << "Errore in madvise MADV_DONTNEED: "<< res<<"\n";
            perror(0);
        }


    }

    //avviso il kernel che molto probabilmente mi serviranno presto le prossime due finestre
    size_t allinea=0;
    if (timesteps[timestep]+tstep_size*timestep_finestra*2<fsize){
        res=madvise(file+allinea_offset(timesteps[timestep],allinea),tstep_size*timestep_finestra*2+allinea,MADV_WILLNEED);   if ( res==-1) {
            std::cerr << "Errore in madvise MADV_WILLNEED: "<< res<<"\n";
            perror(0);
        }

    } else
        res=madvise(file+allinea_offset(timesteps[timestep],allinea),fsize-timesteps[timestep]+allinea,MADV_WILLNEED);

    if ( res==-1) {
        std::cerr << "Errore in madvise MADV_WILLNEED: "<< res<<"\n";
        perror(0);
    }


    //adesso indicizza e carica nel buffer i dati richiesti

    /*
     * carico solo i timesteps che non sono per caso già in memoria:
     * quando la finestra avanza spesso la parte finale della vecchia
     * finestra e la parte iniziale di quella nuova si sovrappongono
    */

    // trova l'intersezione con i timestep già caricati.
    // dati_caricati è vero se ho dei dati in memoria con la stessa dimensione della finestra

    int     timestep_copy_tstart=0,timestep_copy_tend=0,
            timestep_read_start=timestep, timestep_read_end=timestep+timestep_finestra,
            finestra_differenza=timestep_corrente-timestep;
    if (dati_caricati) {
        if (abs(finestra_differenza)<timestep_finestra) { // si sovrappongono
            if (finestra_differenza>0){
                timestep_copy_tstart=0;
                timestep_copy_tend=timestep_finestra-finestra_differenza;

                timestep_read_end=timestep_corrente;
                timestep_read_start=timestep;
            } else {
                timestep_copy_tstart=-finestra_differenza;
                timestep_copy_tend=timestep_finestra;

                timestep_read_start=timestep_corrente+timestep_finestra;
                timestep_read_end=timestep+timestep_finestra;
            }
#ifdef DEBUG
            std::cerr << "Ricopio i dati già letti da "<<timestep_corrente+timestep_copy_tstart << " a "
                      << timestep_corrente+timestep_copy_tend << ".\n";
#endif
        }

    }
    //copia i dati già letti
    for (int i=timestep_copy_tstart;i<timestep_copy_tend;i++) {
        for (int idata=0;idata<3*natoms;idata++) {
            buffer_velocita[(finestra_differenza+i)*3*natoms+idata] =
                    buffer_velocita[i*3*natoms + idata];
            buffer_posizioni[(finestra_differenza+i)*3*natoms+idata]=
                    buffer_posizioni[i*3*natoms + idata];
        }

        //anche la velocità del centro di massa
        for (unsigned int itype=0;itype<ntypes*3;itype++)
        buffer_posizioni_cm[(finestra_differenza+i)*3*ntypes+itype]=
                buffer_posizioni_cm[i*3*ntypes + itype];
        //anche la velocità del centro di massa
        for (unsigned int itype=0;itype<ntypes*3;itype++)
        buffer_velocita_cm[(finestra_differenza+i)*3*ntypes+itype]=
                buffer_velocita_cm[i*3*ntypes + itype];

    }

    //leggi quelli che non sono già in memoria (tutti se necessario)

    //contatore per calcolare la media del centro di massa
    unsigned int *cont_cm=new unsigned int[ntypes];

    for (int i=timestep_read_start;i<timestep_read_end;i++){
        int t=i-timestep;
        Intestazione_timestep * intestazione=0;
        Chunk * pezzi=0;
        //legge i vari pezzi del timestep e copia le posizioni e le velocita' degli atomi nei buffer

        size_t offset = leggi_pezzo(timesteps[i],intestazione,pezzi);
        timesteps_lammps[i]=intestazione->timestep;
        for (unsigned int iscatola=0;iscatola<6;iscatola++) {
            buffer_scatola[t*6+iscatola]=intestazione->scatola[iscatola];
        }
        double l[3]={intestazione->scatola[1]-intestazione->scatola[0],
                     intestazione->scatola[3]-intestazione->scatola[2],
                     intestazione->scatola[5]-intestazione->scatola[4]};
        //calcola anche la posizione e la velocità del centro di massa di ciascuna delle specie (dopo aver letto il tipo dell'atomo)
        //prima azzera la media, poi calcolala
        for (unsigned int itype=0;itype<ntypes*3;itype++){
            buffer_posizioni_cm[t*3*ntypes+itype]=0.0;
            buffer_velocita_cm[t*3*ntypes+itype]=0.0;
        }
        for (unsigned int itype=0;itype<ntypes;itype++){
            cont_cm[itype]=0;
        }
        for (unsigned int ichunk=0;ichunk<intestazione->nchunk;ichunk++){
            for (int iatomo=0;iatomo<pezzi[ichunk].n_atomi;iatomo++) {
                int id=id_map.at(round(pezzi[ichunk].atomi[iatomo].id));
                int tipo=round(pezzi[ichunk].atomi[iatomo].tipo);
                for (unsigned int icoord=0;icoord<3;icoord++){
                    if (wrap_pbc)
                        buffer_posizioni[t*3*natoms+id*3+icoord]=pezzi[ichunk].atomi[iatomo].posizione[icoord]-round(pezzi[ichunk].atomi[iatomo].posizione[icoord]/l[icoord])*l[icoord];
                    else
                        buffer_posizioni[t*3*natoms+id*3+icoord]=pezzi[ichunk].atomi[iatomo].posizione[icoord];
                }
                for (unsigned int icoord=0;icoord<3;icoord++)
                    buffer_velocita[t*3*natoms+id*3+icoord]=pezzi[ichunk].atomi[iatomo].velocita[icoord];

                if (buffer_tipi[id]!= tipo) {
                    std::cerr << "Errore: il tipo di atomo per l'id "<<id<<"e' cambiato da "<<buffer_tipi[id]<< " a "<<tipo<<" !\n";
                    buffer_tipi[id]=tipo;
                }

                //aggiorna la media delle posizioni e delle velocità del centro di massa
                unsigned int tipo_id=buffer_tipi_id[id];
                cont_cm[tipo_id]++;
                for (unsigned int icoord=0;icoord<3;icoord++){
                    buffer_posizioni_cm[t*3*ntypes+3*tipo_id+icoord]+=
                            (pezzi[ichunk].atomi[iatomo].posizione[icoord]
                             -buffer_posizioni_cm[t*3*ntypes+3*tipo_id+icoord])   /(cont_cm[tipo_id]);
                    buffer_velocita_cm[t*3*ntypes+3*tipo_id+icoord]+=
                            (pezzi[ichunk].atomi[iatomo].velocita[icoord]-
                             buffer_velocita_cm[t*3*ntypes+3*tipo_id+icoord])     /(cont_cm[tipo_id]);
                }
            }
        }

        delete [] pezzi;

        if(i+1<n_timesteps) timesteps[i+1]=timesteps[i]+offset;
        if(i+1>timestep_indicizzato) timestep_indicizzato=i+1;
    }

    delete [] cont_cm;

    dati_caricati=true;

    timestep_corrente=timestep;


    cron.stop();

#ifdef DEBUG
    std::cerr << "Tempo per la lettura: "<< cron.time()<<"s.\n";
#endif

    return Ok;
}



int64_t Traiettoria::get_timestep_lammps(unsigned int timestep) {
    if (timestep<=timestep_indicizzato) {
        return timesteps_lammps[timestep];
    } else if (timestep < n_timesteps) {
#ifdef DEBUG
        std::cerr << "Attenzione! Richiesto il numero di timestep LAMMPS di una zona del file non ancora letta! ("<<timestep<<", "<<timestep_indicizzato<<" ultimo letto)\n";
#endif
        return timesteps_lammps[timestep];
    } else {
        std::cerr << "Errore: richiesto il numero di timestep LAMMPS per un indice che probabilmente si trova oltre la fine del file! ("<<timestep<<", "<<n_timesteps<<" letti)\n";
        abort();
        return 0;
    }
}


double * Traiettoria::scatola_last() {
    if (timestep_finestra>0) {
        return &buffer_scatola[(unsigned int) 6*(timestep_finestra-1)];
    } else {
        std::cerr << "Errore: non ho nessun dato in memoria per soddisfare la richiesta (scatola_last)!\n";
        abort();
        return 0;
    }
}


