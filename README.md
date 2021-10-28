# lammps-14May16


## Installation (ubuntu 18.04 LTS, ubuntu 20.04 LTS and Debian 10.0)
	git clone https://github.com/by-student-2017/lammps-14May16.git
	lammps-14May16
	chmod +x install.sh
	./install.sh


## run (cool treatment) (LiCoO2)
	cd ~/lammps-14May16/example_LiCoO
	mpirun -np 2 ~/lammps-14May16/src/lmp_mpi -in in.cool
- (Ovito -> File -> Load File -> "dataout.cool.cfg)


## run (cool treatment) (LiMn2O4)
	cd ~/lammps-14May16/example_LiMnO
	mpirun -np 2 ~/lammps-14May16/src/lmp_mpi -in in.cool
- (Ovito -> File -> Load File -> "dataout.cool.cfg)

## run (cool treatment) (Li3Mn2(CoO4)2)
	cd ~/lammps-14May16/example_LiCoMnO
	mpirun -np 2 ~/lammps-14May16/src/lmp_mpi -in in.cool
- (Ovito -> File -> Load File -> "dataout.cool.cfg)
- Li3Mn2(CoO4)2: https://materialsproject.org/materials/mp-778583/

## TiO2 (cool treatment)
	cd ~/lammps-14May16/example_TiO
	mpirun -np 2 ~/lammps-14May16/src/lmp_mpi -in in.cool
- (Ovito -> File -> Load File -> "dataout.cool.cfg)

## SiO2 (cool treatment)
	cd ~/lammps-14May16/example_SiO
	mpirun -np 2 ~/lammps-14May16/src/lmp_mpi -in in.cool
- (Ovito -> File -> Load File -> "dataout.cool.cfg)
- Attention !!!: In this package, "lattce (1,2) ='b3'" in "library.meam_alloy" of original and NIST is changed to "lattce (1,2) ='b1". Because "b3" is not supported by this lammps version. Please use it for actual research after confirming that it is working properly.

