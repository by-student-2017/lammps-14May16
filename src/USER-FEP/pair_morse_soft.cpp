/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pair_morse_soft.h"
#include "atom.h"
#include "comm.h"
#include "force.h"
#include "neigh_list.h"
#include "memory.h"
#include "error.h"

using namespace LAMMPS_NS;

/* ----------------------------------------------------------------------
   Contributing author: Stefan Paquay (TU/e)
------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------- */

PairMorseSoft::~PairMorseSoft()
{
  if(allocated){
    memory->destroy(lambda);
  }
}

/* ---------------------------------------------------------------------- */

void PairMorseSoft::compute(int eflag, int vflag)
{
  int i,j,ii,jj,inum,jnum,itype,jtype;
  double xtmp,ytmp,ztmp,delx,dely,delz,evdwl,fpair;
  double rsq,r,dr,dexp,factor_lj;
  double dp,la,Vp,Vp2,Vpp;
  
  int *ilist,*jlist,*numneigh,**firstneigh;

  evdwl = 0.0;
  if (eflag || vflag) ev_setup(eflag,vflag);
  else evflag = vflag_fdotr = 0;

  double **x = atom->x;
  double **f = atom->f;
  int *type = atom->type;
  int nlocal = atom->nlocal;
  double *special_lj = force->special_lj;
  int newton_pair = force->newton_pair;

  inum = list->inum;
  ilist = list->ilist;
  numneigh = list->numneigh;
  firstneigh = list->firstneigh;

  // loop over neighbors of my atoms

  for (ii = 0; ii < inum; ii++) {
    i = ilist[ii];
    xtmp = x[i][0];
    ytmp = x[i][1];
    ztmp = x[i][2];
    itype = type[i];
    jlist = firstneigh[i];
    jnum = numneigh[i];

    for (jj = 0; jj < jnum; jj++) {
      j = jlist[jj];
      factor_lj = special_lj[sbmask(j)];
      j &= NEIGHMASK;

      delx = xtmp - x[j][0];
      dely = ytmp - x[j][1];
      delz = ztmp - x[j][2];
      rsq = delx*delx + dely*dely + delz*delz;
      jtype = type[j];

      if (rsq < cutsq[itype][jtype]) {
        r = sqrt(rsq);
        dr = r - r0[itype][jtype];
        dexp = exp(alpha[itype][jtype] * dr); // Careful, 1/(original thing)!

        la = 1.0 - lambda[itype][jtype];
        la *= la;
        la *= alpha[itype][jtype];

        Vp  = 1.0 / ( la + dexp );
        Vpp = alpha[itype][jtype]*dexp;
        Vp2 = Vp*Vp;

        dp = d0[itype][jtype]*pow(lambda[itype][jtype],power);
        
        fpair = dp*2.0*Vp2*( Vp - 1.0 );
        fpair *= factor_lj*Vpp/r;

        f[i][0] += delx*fpair;
        f[i][1] += dely*fpair;
        f[i][2] += delz*fpair;
        if (newton_pair || j < nlocal) {
          f[j][0] -= delx*fpair;
          f[j][1] -= dely*fpair;
          f[j][2] -= delz*fpair;
        }

        if (eflag) {
          evdwl = dp*Vp*(Vp - 2.0);
          evdwl *= factor_lj;
        }

        if (evflag) ev_tally(i,j,nlocal,newton_pair,
                             evdwl,0.0,fpair,delx,dely,delz);
      }
    }
  }

  if (vflag_fdotr) virial_fdotr_compute();
}

/* ----------------------------------------------------------------------
   allocate all arrays
------------------------------------------------------------------------- */

void PairMorseSoft::allocate()
{
  PairMorse::allocate();
  int n = atom->ntypes;
  memory->create(lambda,n+1,n+1,"pair:lambda");
  
}

/* ----------------------------------------------------------------------
   set coeffs for one or more type pairs
------------------------------------------------------------------------- */

void PairMorseSoft::coeff(int narg, char **arg)
{
  if (narg < 6 || narg > 7) error->all(FLERR,"Incorrect args for pair coefficients");
  if (!allocated) allocate();
 
  int ilo,ihi,jlo,jhi;
  force->bounds(arg[0],atom->ntypes,ilo,ihi);
  force->bounds(arg[1],atom->ntypes,jlo,jhi);

  double d0_one     = force->numeric(FLERR,arg[2]);
  double alpha_one  = force->numeric(FLERR,arg[3]);
  double r0_one     = force->numeric(FLERR,arg[4]);
  double lambda_one = force->numeric(FLERR,arg[5]);

  double cut_one = cut_global;
  if (narg == 7) cut_one = force->numeric(FLERR,arg[6]);

  int count = 0;
  for (int i = ilo; i <= ihi; i++) {
    for (int j = MAX(jlo,i); j <= jhi; j++) {
      d0[i][j]      = d0_one;
      alpha[i][j]   = alpha_one;
      r0[i][j]      = r0_one;
      lambda[i][j]  = lambda_one;
      cut[i][j]     = cut_one;
      setflag[i][j] = 1;
      count++;
    }
  }

  if (count == 0) error->all(FLERR,"Incorrect args for pair coefficients");
}

/* ----------------------------------------------------------------------
   Set global stuff.
------------------------------------------------------------------------- */

void PairMorseSoft::settings(int narg, char **arg)
{
  if (narg != 3) error->all(FLERR,"Illegal pair_style command");

  power = force->inumeric(FLERR,arg[0]);
  scale = force->numeric(FLERR,arg[1]);
  cut_global = force->numeric(FLERR,arg[2]);

  // reset cutoffs that have been explicitly set

  if (allocated) {
    int i,j;
    for (i = 1; i <= atom->ntypes; i++)
      for (j = i+1; j <= atom->ntypes; j++)
        if (setflag[i][j]) cut[i][j] = cut_global;
  }
}


/* ----------------------------------------------------------------------
   init for one type pair i,j and corresponding j,i
------------------------------------------------------------------------- */

double PairMorseSoft::init_one(int i, int j)
{
  if (setflag[i][j] == 0) error->all(FLERR,"All pair coeffs are not set");

  morse1[i][j] = 2.0*d0[i][j]*alpha[i][j];

  if (offset_flag) {
    double alpha_dr = -alpha[i][j] * (cut[i][j] - r0[i][j]);
    offset[i][j] = d0[i][j] * (exp(2.0*alpha_dr) - 2.0*exp(alpha_dr));
  } else offset[i][j] = 0.0;

  d0[j][i]     = d0[i][j];
  alpha[j][i]  = alpha[i][j];
  r0[j][i]     = r0[i][j];
  morse1[j][i] = morse1[i][j];
  lambda[j][i] = lambda[i][j];
  offset[j][i] = offset[i][j];

  return cut[i][j];
}

/* ----------------------------------------------------------------------
   proc 0 writes to restart file
------------------------------------------------------------------------- */

void PairMorseSoft::write_restart(FILE *fp)
{
  write_restart_settings(fp);

  int i,j;
  for (i = 1; i <= atom->ntypes; i++) {
    for (j = i; j <= atom->ntypes; j++) {
      fwrite(&setflag[i][j],sizeof(int),1,fp);
      if (setflag[i][j]) {
        fwrite(&d0[i][j],sizeof(double),1,fp);
        fwrite(&alpha[i][j],sizeof(double),1,fp);
        fwrite(&r0[i][j],sizeof(double),1,fp);
        fwrite(&lambda[i][j],sizeof(double),1,fp);
        fwrite(&cut[i][j],sizeof(double),1,fp);
      }
    }
  }
}

/* ----------------------------------------------------------------------
   proc 0 reads from restart file, bcasts
------------------------------------------------------------------------- */

void PairMorseSoft::read_restart(FILE *fp)
{
  read_restart_settings(fp);

  allocate();

  int i,j;
  int me = comm->me;
  for (i = 1; i <= atom->ntypes; i++) {
    for (j = i; j <= atom->ntypes; j++) {
      if (me == 0) fread(&setflag[i][j],sizeof(int),1,fp);
      MPI_Bcast(&setflag[i][j],1,MPI_INT,0,world);
      if (setflag[i][j]) {
        if (me == 0) {
          fread(&d0[i][j],sizeof(double),1,fp);
          fread(&alpha[i][j],sizeof(double),1,fp);
          fread(&r0[i][j],sizeof(double),1,fp);
          fread(&lambda[i][j],sizeof(double),1,fp);
          fread(&cut[i][j],sizeof(double),1,fp);
        }
        MPI_Bcast(&d0[i][j],1,MPI_DOUBLE,0,world);
        MPI_Bcast(&alpha[i][j],1,MPI_DOUBLE,0,world);
        MPI_Bcast(&r0[i][j],1,MPI_DOUBLE,0,world);
        MPI_Bcast(&lambda[i][j],1,MPI_DOUBLE,0,world);
        MPI_Bcast(&cut[i][j],1,MPI_DOUBLE,0,world);
      }
    }
  }
}


/* ----------------------------------------------------------------------
   proc 0 writes to data file
------------------------------------------------------------------------- */

void PairMorseSoft::write_data(FILE *fp)
{
  for (int i = 1; i <= atom->ntypes; i++)
    fprintf(fp,"%d %g %g %g %g\n",i,d0[i][i],alpha[i][i],r0[i][i],
            lambda[i][i]);
}

/* ----------------------------------------------------------------------
   proc 0 writes all pairs to data file
------------------------------------------------------------------------- */

void PairMorseSoft::write_data_all(FILE *fp)
{
  for (int i = 1; i <= atom->ntypes; i++)
    for (int j = i; j <= atom->ntypes; j++)
      fprintf(fp,"%d %g %g %g %g\n",i,d0[i][j],alpha[i][j],r0[i][j],
              lambda[i][j]);
}

/* ---------------------------------------------------------------------- */

double PairMorseSoft::single(int i, int j, int itype, int jtype, double rsq,
                             double factor_coul, double factor_lj,
                             double &fforce)
{
  double r,dr,dexp,phi;
  double dp,la,Vp,Vp2,Vpp;
  
  r = sqrt(rsq);
  dr = r - r0[itype][jtype];
  dexp = exp(alpha[itype][jtype] * dr); // Careful, 1/(the original thing)!

  la = 1.0 - lambda[itype][jtype];
  la *= la;
  la *= alpha[itype][jtype];

  Vp  = 1.0 / ( la + dexp );
  Vpp = alpha[itype][jtype]*dexp;
  Vp2 = Vp*Vp;

  dp = d0[itype][jtype]*pow(lambda[itype][jtype],power);

  fforce = dp*2.0*Vp2*( Vp - 1.0 );
  fforce *= factor_lj*Vpp;

  phi = dp*Vp*(Vp - 2.0);
  
  return factor_lj*phi;
}

/* ---------------------------------------------------------------------- */

void *PairMorseSoft::extract(const char *str, int &dim)
{
  dim = 2;
  if (strcmp(str,"d0") == 0) return (void *) d0;
  if (strcmp(str,"r0") == 0) return (void *) r0;
  if (strcmp(str,"alpha") == 0) return (void *) alpha;
  if (strcmp(str,"lambda") == 0) return (void *) lambda;
  return NULL;
}
