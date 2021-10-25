#!/bin/bash

sudo apt -y update
sudo apt -y install g++
sudo apt -y install gfortran
sudo apt -y install build-essential
sudo apt -y install libopenmpi-dev
sudo apt -y install unzip

cp ./2NNMEAM-QEQ/modified_source_code/* ./src
cp ./2NNMEAM-QEQ/modified_source_code/* ./src/QEQ

cd ./lib/meam
make -f Makefile.gfortran
cd ./../../src

make yes-meam
make yes-mc
make yes-USER-REAXC
make package-status
make mpi

