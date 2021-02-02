/**
  *
  * (c) Riccardo Bertossa, 2019
  *
  *   Use at  your own risk.
  *
  *   If you modified the code, I could be happy if you contribute on github!
  *
**/



#include <cmath>
#include<thread>
#include <vector>
#include <fstream>
#include "msd.h"
#include "config.h"
#include "traiettoria.h"

#ifdef USE_MPI
#include "mp.h"
#endif
template <class T>
MSD<T>::MSD(T *t, unsigned int skip, unsigned int tmax, unsigned int nthreads, bool calcola_msd_centro_di_massa, bool calcola_msd_nel_sistema_del_centro_di_massa, bool calcola_msd_cross, bool debug) :
    traiettoria(t), lmax(tmax),skip(skip),nthread(nthreads), cm_msd(calcola_msd_centro_di_massa),debug(debug),cm_self(calcola_msd_nel_sistema_del_centro_di_massa), cross_msd(calcola_msd_cross), ntypes{0}
{
    if (calcola_msd_centro_di_massa && calcola_msd_cross)
        f_size=3;
    else if (calcola_msd_centro_di_massa && !calcola_msd_cross)
        f_size=2;
	else if (!calcola_msd_cross && calcola_msd_cross)
        f_size=2;
    else
        f_size=1;

}
template <class T>
unsigned int MSD<T>::numeroTimestepsOltreFineBlocco(unsigned int n_b){
    return (traiettoria->get_ntimesteps()/(n_b+1)+1 < lmax || lmax==0)? traiettoria->get_ntimesteps()/(n_b+1)+1 : lmax;
}
template <class T>
void MSD<T>::reset(const unsigned int numeroTimestepsPerBlocco) {

    leff=(numeroTimestepsPerBlocco<lmax || lmax==0)? numeroTimestepsPerBlocco : lmax;
    ntypes=traiettoria->get_ntypes();
    lunghezza_lista=leff*ntypes*f_size;

    ntimesteps=numeroTimestepsPerBlocco;
    delete [] lista;
    lista=new double [lunghezza_lista];
}

template <class T>
void MSD<T>::calcola(unsigned int primo) {



    if (leff+ntimesteps+primo > traiettoria->get_ntimesteps()){
        throw std::runtime_error("trajectory is too short for this kind of calculation. Select a different starting timestep or lower the size of the average or the lenght of the time lag");
    }

    if (nthread<1) {
        unsigned int *cont=new unsigned int [ntypes*f_size];

        for (unsigned int t=0;t<leff;t++) {
            for (unsigned int i=0;i<ntypes*f_size;i++){
                lista[ntypes*t*f_size+i]=0.0;
                cont[i]=0;
            }
            for (unsigned int imedia=0;imedia<ntimesteps;imedia+=skip){
                for (unsigned int iatom=0;iatom<traiettoria->get_natoms();iatom++) {
                    double delta=(pow(traiettoria->posizioni(primo+imedia,iatom)[0]-traiettoria->posizioni(primo+imedia+t,iatom)[0],2)+
                            pow(traiettoria->posizioni(primo+imedia,iatom)[1]-traiettoria->posizioni(primo+imedia+t,iatom)[1],2)+
                            pow(traiettoria->posizioni(primo+imedia,iatom)[2]-traiettoria->posizioni(primo+imedia+t,iatom)[2],2))
                            -lista[ntypes*t*f_size+traiettoria->get_type(iatom)];
                    lista[ntypes*t*f_size+traiettoria->get_type(iatom)]+=delta/(++cont[traiettoria->get_type(iatom)]);

                }
            }
        }
        delete [] cont;
    } else { //nthread >1 (il codice precedente viene tenuto per verificare il corretto funzionamento di quello parallelo

        /*dividi il lavoro in gruppi
         t  --->  [0,leff[
         npassi     --->  leff

         ciascun gruppo avrà npassith=npassi/nthread passi
         l'ultimo deve finire alla fine
        */
        unsigned int npassith=leff/nthread;
        std::vector<std::thread> threads;

        for (unsigned int ith=0;ith<nthread;ith++) {
            threads.push_back(std::thread([&,ith](){
                unsigned int *cont=new unsigned int [ntypes*f_size];
                unsigned int ultimo= (ith != nthread-1 )?npassith*(ith+1):leff;

                for (unsigned int t=npassith*ith;t<ultimo;t++) {
                    for (unsigned int i=0;i<ntypes*f_size;i++){
                        lista[ntypes*t*f_size+i]=0.0;
                        cont[i]=0;
                    }
                    for (unsigned int imedia=0;imedia<ntimesteps;imedia+=skip){
                        if (cm_self){
                            for (unsigned int iatom=0;iatom<traiettoria->get_natoms();iatom++) {
                                unsigned int itype=traiettoria->get_type(iatom);
                                double delta=(pow(
                                               traiettoria->posizioni(primo+imedia,iatom)[0]-traiettoria->posizioni(primo+imedia+t,iatom)[0]
                                              -(traiettoria->posizioni_cm(primo+imedia,itype)[0]-traiettoria->posizioni_cm(primo+imedia+t,itype)[0])
                                        ,2)+
                                        pow(
                                            traiettoria->posizioni(primo+imedia,iatom)[1]-traiettoria->posizioni(primo+imedia+t,iatom)[1]
                                            -(traiettoria->posizioni_cm(primo+imedia,itype)[1]-traiettoria->posizioni_cm(primo+imedia+t,itype)[1])
                                        ,2)+
                                        pow(
                                            traiettoria->posizioni(primo+imedia,iatom)[2]-traiettoria->posizioni(primo+imedia+t,iatom)[2]
                                            -(traiettoria->posizioni_cm(primo+imedia,itype)[2]-traiettoria->posizioni_cm(primo+imedia+t,itype)[2])
                                        ,2))
                                        -lista[ntypes*t*f_size+itype];
                                lista[ntypes*t*f_size+itype]+=delta/(++cont[itype]);

                            }
                        }else{
                            for (unsigned int iatom=0;iatom<traiettoria->get_natoms();iatom++) {
                                unsigned int itype=traiettoria->get_type(iatom);
                                double delta=(pow(traiettoria->posizioni(primo+imedia,iatom)[0]-traiettoria->posizioni(primo+imedia+t,iatom)[0],2)+
                                        pow(traiettoria->posizioni(primo+imedia,iatom)[1]-traiettoria->posizioni(primo+imedia+t,iatom)[1],2)+
                                        pow(traiettoria->posizioni(primo+imedia,iatom)[2]-traiettoria->posizioni(primo+imedia+t,iatom)[2],2))
                                        -lista[ntypes*t*f_size+itype];
                                lista[ntypes*t*f_size+itype]+=delta/(++cont[itype]);

                            }
                        }
						if (cm_msd && cross_msd) {
							// cm_msd
                            for (unsigned int itype=0; itype < ntypes; itype++) {
                            double delta=(pow(traiettoria->posizioni_cm(primo+imedia,itype)[0]-traiettoria->posizioni_cm(primo+imedia+t,itype)[0],2)+
                                    pow(traiettoria->posizioni_cm(primo+imedia,itype)[1]-traiettoria->posizioni_cm(primo+imedia+t,itype)[1],2)+
                                    pow(traiettoria->posizioni_cm(primo+imedia,itype)[2]-traiettoria->posizioni_cm(primo+imedia+t,itype)[2],2))
                                    -lista[ntypes*t*f_size+ntypes+itype];
                                lista[ntypes*t*f_size + ntypes + itype]+=delta/(++cont[ntypes+itype]);
							}
							// cross_msd
                            for (unsigned int iatom=0; iatom < traiettoria->get_natoms(); iatom++) {
                                unsigned int itype = traiettoria->get_type(iatom);
                            	for (unsigned int jatom=0; jatom < traiettoria->get_natoms(); jatom++) {
                                	unsigned int jtype = traiettoria->get_type(jatom);
									if (itype == jtype) {
										double dxi = traiettoria->posizioni(primo + imedia, iatom)[0] - traiettoria->posizioni(primo + imedia + t, iatom)[0]
										double dxj = traiettoria->posizioni(primo + imedia, jatom)[0] - traiettoria->posizioni(primo + imedia + t, jatom)[0]
										double dyi = traiettoria->posizioni(primo + imedia, iatom)[1] - traiettoria->posizioni(primo + imedia + t, iatom)[1]
										double dyj = traiettoria->posizioni(primo + imedia, jatom)[1] - traiettoria->posizioni(primo + imedia + t, jatom)[1]
										double dzi = traiettoria->posizioni(primo + imedia, iatom)[2] - traiettoria->posizioni(primo + imedia + t, iatom)[2]
										double dzj = traiettoria->posizioni(primo + imedia, jatom)[2] - traiettoria->posizioni(primo + imedia + t, jatom)[2]
										double delta = (dxi*dxj + dyi*dyj + dzi*dzj) - lista[ntypes*t*f_size + itype];
                                		lista[ntypes*t*f_size + 2*ntypes + itype] += delta/(++cont[2*ntypes + itype]);
									}
								}
							}
						}else if (cm_msd && !cross_msd) {
							// only cm_msd
                            for (unsigned int itype=0; itype < ntypes; itype++) {
                            double delta=(pow(traiettoria->posizioni_cm(primo+imedia,itype)[0]-traiettoria->posizioni_cm(primo+imedia+t,itype)[0],2)+
                                    pow(traiettoria->posizioni_cm(primo+imedia,itype)[1]-traiettoria->posizioni_cm(primo+imedia+t,itype)[1],2)+
                                    pow(traiettoria->posizioni_cm(primo+imedia,itype)[2]-traiettoria->posizioni_cm(primo+imedia+t,itype)[2],2))
                                    -lista[ntypes*t*f_size+ntypes+itype];

                                lista[ntypes*t*f_size + ntypes + itype]+=delta/(++cont[ntypes+itype]);
                           }
                        }else if (!cm_msd && cross_msd) {
							// only cross msd
                            for (unsigned int iatom=0;iatom<traiettoria->get_natoms();iatom++) {
                                unsigned int itype=traiettoria->get_type(iatom);
                            	for (unsigned int jatom=0;jatom<traiettoria->get_natoms();jatom++) {
                                	unsigned int jtype=traiettoria->get_type(jatom);
									if (itype == jtype) {
										double dxi = traiettoria->posizioni(primo + imedia, iatom)[0] - traiettoria->posizioni(primo + imedia + t, iatom)[0]
										double dxj = traiettoria->posizioni(primo + imedia, jatom)[0] - traiettoria->posizioni(primo + imedia + t, jatom)[0]
										double dyi = traiettoria->posizioni(primo + imedia, iatom)[1] - traiettoria->posizioni(primo + imedia + t, iatom)[1]
										double dyj = traiettoria->posizioni(primo + imedia, jatom)[1] - traiettoria->posizioni(primo + imedia + t, jatom)[1]
										double dzi = traiettoria->posizioni(primo + imedia, iatom)[2] - traiettoria->posizioni(primo + imedia + t, iatom)[2]
										double dzj = traiettoria->posizioni(primo + imedia, jatom)[2] - traiettoria->posizioni(primo + imedia + t, jatom)[2]
										double delta = (dxi*dxj + dyi*dyj + dzi*dzj) - lista[ntypes*t*f_size + itype];
                                		lista[ntypes*t*f_size + ntypes + itype] += delta/(++cont[ntypes + itype]);
									}
								}
							}
						}
                    }
                }
                delete [] cont;
            }));
        }

        for (unsigned int ith=0;ith<nthread;ith++)
            threads[ith].join();

        threads.clear();

        if (debug) {
#ifndef USE_MPI
            std::ofstream out("msd.dump",std::ios::app);
#else
            std::ofstream out(Mp::mpi().outname("msd.dump"));
#endif
            for (unsigned int ts=0;ts<leff;ts++) {
                out << ts;
                for (unsigned int itype=0;itype<ntypes*f_size;itype++){
                    out <<" "<<lista[ntypes*ts*f_size+itype];
                }
                out << "\n";
            }
            out << "\n\n";
        }


    }
}
template <class T>
MSD<T> & MSD<T>::operator=(const MSD<T> &destra) {
    OperazioniSuLista<MSD<T> >::operator =( destra);
    return *this;
}

template class MSD<Traiettoria>;
#ifdef PYTHON_SUPPORT
#include "traiettoria_numpy.h"
template class MSD<Traiettoria_numpy>;
#endif
