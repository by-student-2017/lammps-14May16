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

/* ----------------------------------------------------------------------
   Contributing author: Trung Dac Nguyen
------------------------------------------------------------------------- */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "pair_coul_cut_gpu.h"
#include "atom.h"
#include "atom_vec.h"
#include "comm.h"
#include "force.h"
#include "neighbor.h"
#include "neigh_list.h"
#include "integrate.h"
#include "memory.h"
#include "error.h"
#include "neigh_request.h"
#include "universe.h"
#include "update.h"
#include "domain.h"
#include <string.h>
#include "gpu_extra.h"

using namespace LAMMPS_NS;

// External functions from cuda library for atom decomposition

int coul_gpu_init(const int ntypes, double **host_scale, double **cutsq,
                  double *special_coul, const int nlocal,
                  const int nall, const int max_nbors, const int maxspecial,
                  const double cell_size, int &gpu_mode, FILE *screen,
                  const double qqrd2e);
void coul_gpu_reinit(const int ntypes, double **host_scale);
void coul_gpu_clear();
int ** coul_gpu_compute_n(const int ago, const int inum,
                         const int nall, double **host_x, int *host_type,
                         double *sublo, double *subhi, tagint *tag, int **nspecial,
                         tagint **special, const bool eflag, const bool vflag,
                         const bool eatom, const bool vatom, int &host_start,
                         int **ilist, int **jnum, const double cpu_time,
                         bool &success, double *host_q, double *boxlo,
                         double *prd);
void coul_gpu_compute(const int ago, const int inum,
                      const int nall, double **host_x, int *host_type,
                     int *ilist, int *numj, int **firstneigh,
                     const bool eflag, const bool vflag, const bool eatom,
                     const bool vatom, int &host_start, const double cpu_time,
                     bool &success, double *host_q, const int nlocal,
                     double *boxlo, double *prd);
double coul_gpu_bytes();

/* ---------------------------------------------------------------------- */

PairCoulCutGPU::PairCoulCutGPU(LAMMPS *lmp) : PairCoulCut(lmp), gpu_mode(GPU_FORCE)
{
  respa_enable = 0;
  cpu_time = 0.0;
  GPU_EXTRA::gpu_ready(lmp->modify, lmp->error);
}

/* ----------------------------------------------------------------------
   free all arrays
------------------------------------------------------------------------- */

PairCoulCutGPU::~PairCoulCutGPU()
{
  coul_gpu_clear();
}

/* ---------------------------------------------------------------------- */

void PairCoulCutGPU::compute(int eflag, int vflag)
{
  if (eflag || vflag) ev_setup(eflag,vflag);
  else evflag = vflag_fdotr = 0;

  int nall = atom->nlocal + atom->nghost;
  int inum, host_start;

  bool success = true;
  int *ilist, *numneigh, **firstneigh;
  if (gpu_mode != GPU_FORCE) {
    inum = atom->nlocal;
    firstneigh = coul_gpu_compute_n(neighbor->ago, inum, nall, atom->x,
                                   atom->type, domain->sublo, domain->subhi,
                                   atom->tag, atom->nspecial, atom->special,
                                   eflag, vflag, eflag_atom, vflag_atom,
                                   host_start, &ilist, &numneigh, cpu_time,
                                   success, atom->q, domain->boxlo,
                                   domain->prd);
  } else {
    inum = list->inum;
    ilist = list->ilist;
    numneigh = list->numneigh;
    firstneigh = list->firstneigh;
    coul_gpu_compute(neighbor->ago, inum, nall, atom->x, atom->type,
                    ilist, numneigh, firstneigh, eflag, vflag, eflag_atom,
                    vflag_atom, host_start, cpu_time, success, atom->q,
                    atom->nlocal, domain->boxlo, domain->prd);
  }
  if (!success)
    error->one(FLERR,"Insufficient memory on accelerator");

  if (host_start<inum) {
    cpu_time = MPI_Wtime();
    cpu_compute(host_start, inum, eflag, vflag, ilist, numneigh, firstneigh);
    cpu_time = MPI_Wtime() - cpu_time;
  }
}

/* ----------------------------------------------------------------------
   init specific to this pair style
------------------------------------------------------------------------- */

void PairCoulCutGPU::init_style()
{
  if (!atom->q_flag)
    error->all(FLERR,"Pair style coul/cut/gpu requires atom attribute q");

  if (force->newton_pair)
    error->all(FLERR,"Cannot use newton pair with coul/cut/gpu pair style");

  // Repeat cutsq calculation because done after call to init_style
  double maxcut = -1.0;
  double cut;
  for (int i = 1; i <= atom->ntypes; i++) {
    for (int j = i; j <= atom->ntypes; j++) {
      if (setflag[i][j] != 0 || (setflag[i][i] != 0 && setflag[j][j] != 0)) {
        cut = init_one(i,j);
        cut *= cut;
        if (cut > maxcut)
          maxcut = cut;
        cutsq[i][j] = cutsq[j][i] = cut;
      } else
        cutsq[i][j] = cutsq[j][i] = 0.0;
    }
  }
  double cell_size = sqrt(maxcut) + neighbor->skin;

  int maxspecial=0;
  if (atom->molecular)
    maxspecial=atom->maxspecial;
  int success = coul_gpu_init(atom->ntypes+1, scale, cutsq,
                             force->special_coul, atom->nlocal,
                             atom->nlocal+atom->nghost, 300, maxspecial,
                             cell_size, gpu_mode, screen, force->qqrd2e);
  GPU_EXTRA::check_flag(success,error,world);

  if (gpu_mode == GPU_FORCE) {
    int irequest = neighbor->request(this,instance_me);
    neighbor->requests[irequest]->half = 0;
    neighbor->requests[irequest]->full = 1;
  }
}

/* ---------------------------------------------------------------------- */

void PairCoulCutGPU::reinit()
{
  Pair::reinit();

  coul_gpu_reinit(atom->ntypes+1, scale);
}

/* ---------------------------------------------------------------------- */

double PairCoulCutGPU::memory_usage()
{
  double bytes = Pair::memory_usage();
  return bytes + coul_gpu_bytes();
}

/* ---------------------------------------------------------------------- */

void PairCoulCutGPU::cpu_compute(int start, int inum, int eflag, int vflag,
                                      int *ilist, int *numneigh,
                                      int **firstneigh)
{
  int i,j,ii,jj,jnum,itype,jtype;
  double qtmp,xtmp,ytmp,ztmp,delx,dely,delz,ecoul,fpair;
  double rsq,r2inv,forcecoul,factor_coul;
  int *jlist;

  ecoul = 0.0;

  double **x = atom->x;
  double **f = atom->f;
  double *q = atom->q;
  int *type = atom->type;
  double *special_coul = force->special_coul;
  double qqrd2e = force->qqrd2e;

  // loop over neighbors of my atoms

  for (ii = start; ii < inum; ii++) {
    i = ilist[ii];
    qtmp = q[i];
    xtmp = x[i][0];
    ytmp = x[i][1];
    ztmp = x[i][2];
    itype = type[i];
    jlist = firstneigh[i];
    jnum = numneigh[i];

    for (jj = 0; jj < jnum; jj++) {
      j = jlist[jj];
      factor_coul = special_coul[sbmask(j)];
      j &= NEIGHMASK;

      delx = xtmp - x[j][0];
      dely = ytmp - x[j][1];
      delz = ztmp - x[j][2];
      rsq = delx*delx + dely*dely + delz*delz;
      jtype = type[j];

      if (rsq < cutsq[itype][jtype]) {
        r2inv = 1.0/rsq;
        forcecoul = qqrd2e * qtmp*q[j]*sqrt(r2inv);
        fpair = factor_coul*forcecoul * r2inv;

        f[i][0] += delx*fpair;
        f[i][1] += dely*fpair;
        f[i][2] += delz*fpair;

        if (eflag) {
          ecoul = factor_coul * qqrd2e * qtmp*q[j]*sqrt(r2inv);
        }

        if (evflag) ev_tally_full(i,0.0,ecoul,fpair,delx,dely,delz);
      }
    }
  }
}