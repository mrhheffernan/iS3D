/*    run.cpp   */
/*
********************************************************************************
This is the public version of the resonance decay calculation using the output
generated by the hydrodynamical code azhydro0p2.  The majority of the code was
developed by Josef Sollfrank and Peter Kolb.  Additional work and comments
were added by Evan Frodermann, September 2005.
Please refer to the papers
J. Sollfrank, P. Koch, and U. Heinz, Phys. Lett B 252 (1990) and
J. Sollfrank, P. Koch, and U. Heinz, Z. Phys. C 52 (1991)
for a description of the formalism utilized in this program.
********************************************************************************
*/

/* This has been modified for iS3D, a 3D Cooper Frye and Resonance Decay Code*/
/* Derek Everett, Michael McNelis (2018)*/

#include <iostream>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "run.h"
#include "functions.h"
#include "decay.h"

//*****************************************************
//This is the main program that calls all the needed subroutines.

int main()
{

   FILE *paramFile;

   char outdir[FILEDIM];
   char specFile[FILEDIM];
   char dummy[200];

   int max, maxdecay, bound;

   printf("Starting resonance decays \n");
   //Read in the data from parameters.dat, including the results folder and the spectra data
   paramFile = fopen("parameters.dat", "r");
   fscanf(paramFile, "%s%s", specFile, dummy);
   //std::cout << specfile << std::endl;
   fscanf(paramFile, "%s%s", outdir, dummy);
   fscanf(paramFile, "%i%s", &bound, dummy);
   fclose(paramFile);
   //Read in the spectra and decays using "resoweak.dat" as a database of particles
   readSpectra(specFile, &max, &maxdecay);
   //The main module that calculates the resonance decay feed-down
   calc_reso_decays(max, maxdecay, bound);
   //Writes the spectra to specified data files.
   writeSpectra(max,outdir);

   printf("Resonance decays finished in ? seconds! \n");

   return 0;	/* ok */
}
