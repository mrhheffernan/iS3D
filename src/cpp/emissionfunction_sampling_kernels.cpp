#include <iostream>
#include <sstream>
#include <string>
#include <fstream>
#include <cmath>
#include <iomanip>
#include <vector>
#include <stdio.h>
#include <random>
#include <chrono>
#include <limits>
#include <array>
#ifdef _OMP
#include <omp.h>
#endif
#include "main.h"
#include "readindata.h"
#include "emissionfunction.h"
#include "Stopwatch.h"
#include "arsenal.h"
#include "ParameterReader.h"
#include "deltafReader.h"
#include <gsl/gsl_sf_bessel.h> //for modified bessel functions
#include "gaussThermal.h"
#include "particle.h"
#include "viscous_correction.h"

#define AMOUNT_OF_OUTPUT 0 // smaller value means less outputs

using namespace std;

lrf_momentum Sample_Momentum_deltaf(double mass, double T, double alphaB, Shear_Tensor pimunu, double bulkPi, double eps, double pressure, double tau2, double sign, lrf_dsigma dsigmaLRF, double dsigma_magnitude, int INCLUDE_SHEAR_DELTAF, int INCLUDE_BULK_DELTAF, int INCLUDE_BARYONDIFF_DELTAF, int DF_MODE)
{
  // sampled LRF momentum
  lrf_momentum pLRF;

  // for acceptance / rejection loop
  bool rejected = true;

  double mass_squared = mass * mass;

  // dsigma LRF components
  double dat = dsigmaLRF.t;
  double dax = dsigmaLRF.x;
  double day = dsigmaLRF.y;
  double daz = dsigmaLRF.z;

  // pimunu LRF components
  double pixx = pimunu.pixx;
  double pixy = pimunu.pixy;
  double pixz = pimunu.pixz;
  double piyy = pimunu.piyy;
  double piyz = pimunu.piyz;
  double pizz = pimunu.pizz;

  // set df coefficients (viscous corrections)
  double c0 = 0.0;
  double c1 = 0.0;
  double c2 = 0.0;
  double c3 = 0.0;
  double c4 = 0.0;
  double F = 0.0;
  double G = 0.0;
  double betabulk = 0.0;
  double betaV = 0.0;
  double betapi = 0.0;

  /*
  switch(DF_MODE)
  {
    case 1: // 14-moment
    {
      // bulk coefficients
      c0 = df_coeff[0];
      c1 = df_coeff[1];
      c2 = df_coeff[2];
      // diffusion coefficients
      c3 = df_coeff[3];
      c4 = df_coeff[4];
      // shear coefficient evaluated later
      break;
    }
    case 2: // Chapman-Enskog
    {
      // bulk coefficients
      F = df_coeff[0];
      G = df_coeff[1];
      betabulk = df_coeff[2];
      // diffusion coefficient
      betaV = df_coeff[3];
      // shear coefficient
      betapi = df_coeff[4];
      break;
    }
    default:
    {
      cout << "Please choose df_mode = 1 or 2 in parameters.dat" << endl;
      exit(-1);
    }
  }
  */

  unsigned seed = chrono::system_clock::now().time_since_epoch().count();
  default_random_engine generator(seed);

  //pion routine
  //not Adaptive Rejection Sampling, but the naive method here...
  if (mass / T < 1.5)
  {
    // sample momentum until get an acceptance 
    while (rejected)
    {
      // draw (p, phi, costheta) from distribution p^2 * exp[-p/T] by
      // sampling three variables (r1,r2,r3) uniformly from [0,1)
      double r1 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
      double r2 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
      double r3 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);

      double l1 = log(r1);
      double l2 = log(r2);
      double l3 = log(r3);

      double p = - T * (l1 + l2 + l3);
      double E = sqrt(fabs(p * p + mass_squared));

      // calculate angles and pLRF components
      double costheta = (l1 - l2) / (l1 + l2);
      double phi = 2.0 * M_PI * pow(l1 + l2, 2) / pow(l1 + l2 + l3, 2);
      double sintheta = sqrt(fabs(1.0 - costheta * costheta));

      double px = p * sintheta * cos(phi);
      double py = p * sintheta * sin(phi);
      double pz = p * costheta;

      double pdotdsigma_abs = fabs(E * dat - px * dax - py * day - pz * daz);
      double rideal = pdotdsigma_abs / E / dsigma_magnitude;

     
      //viscous corrections: (is there some way compactify this into a function?)
      //::::::::::::::::::::::::::::::::::::::::
      double shear_weight = 1.0;
      double bulk_weight = 1.0;
      double diff_weight = 1.0;
      //FIX TEMPORARY
      if (INCLUDE_SHEAR_DELTAF) 
      {
        double A = 2.0 * T * T * (eps + pressure);
        double f0 = 1.0 / ( exp(E / T) + sign );
        //check contraction / signs etc...
        double pmupnupimunu = 2.0 * ( px*px*pixx + px*py*pixy + px*pz*pixz + py*py*piyy + py*pz*piyz + pz*pz*pizz );
        double num = A + ( 1.0 + sign * f0 ) * pmupnupimunu;
        double pmupnupimunu_max = E * E * pimunu.compute_max();
        double den = A + (1.0 + (sign * f0) ) * pmupnupimunu_max;
        shear_weight = num / den;
      }
      if (INCLUDE_BULK_DELTAF)
      {
        double H = 1.0;
        double f0 = 1.0 / ( exp(E / T) + sign );
        double num = 1.0 + ( 1.0 + sign * f0 ) * H * bulkPi;
        double Hmax = 1.0;
        double den = 1.0 + ( 1.0 + sign * f0 ) * Hmax * bulkPi;
        bulk_weight = num / den;
      }
      if (INCLUDE_BARYONDIFF_DELTAF) diff_weight = 1.0;
      //::::::::::::::::::::::::::::::::::::::::

      double viscous_weight = shear_weight * bulk_weight * diff_weight;     // could this go negative?...if so what to do? 


      //here the pion weight should include the Bose enhancement factor
      //this formula assumes zero chemical potential mu = 0
      //TO DO - generalize for nonzero chemical potential
      double weight = rideal * viscous_weight * exp(p/T) / (exp(E/T) - 1.0);
      double propose = generate_canonical<double, numeric_limits<double>::digits>(generator);

      if(!(rideal >= 0.0 && rideal <= 1.0))
      {
        printf("Error: rideal = %f out of bounds\n", rideal);
        exit(-1);
      }
      if(!(weight >= 0.0 && weight <= 1.0))
      {
        printf("Error: weight = %f out of bounds\n", weight);
        exit(-1);
      }

      // check p acceptance
      if(propose < weight)
      {
        pLRF.x = px;
        pLRF.y = py;
        pLRF.z = pz;
        break;        // end rejected loop
      } // p acceptance

    } // while loop
  } //if (mass / T < 1.5)

  // light hadrons, but heavier than pions
  else if (mass / T < 2.0) // fixed bug on 7/12
  {
    // sample momentum until get an acceptance 
    while(rejected)
    {
      // draw (p, phi, costheta) from distribution p^2 * exp[-p/T] by
      // sampling three variables (r1,r2,r3) uniformly from [0,1)
      double r1 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
      double r2 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
      double r3 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);

      double l1 = log(r1);
      double l2 = log(r2);
      double l3 = log(r3);

      double p = - T * (l1 + l2 + l3);
      double E = sqrt(fabs(p * p + mass_squared));

      double phi = 2.0 * M_PI * pow(l1 + l2, 2) / pow(l1 + l2 + l3, 2);
      double costheta = (l1 - l2) / (l1 + l2);
      double sintheta = sqrt(fabs(1.0 - costheta * costheta));

      double px = p * sintheta * cos(phi);
      double py = p * sintheta * sin(phi);
      double pz = p * costheta;

      double pdotdsigma_abs = fabs(E * dat - px * dax - py * day - pz * daz);
      double rideal = pdotdsigma_abs / E / dsigma_magnitude;

      //viscous corrections: (is there some way compactify this into a function?)
      //::::::::::::::::::::::::::::::::::::::::
      double shear_weight = 1.0;
      double bulk_weight = 1.0;
      double diff_weight = 1.0;
      //FIX TEMPORARY
      if (INCLUDE_SHEAR_DELTAF) 
      {
        double A = 2.0 * T * T * (eps + pressure);
        double f0 = 1.0 / ( exp(E / T) + sign );
        //check contraction / signs etc...
        double pmupnupimunu = 2.0 * ( px*px*pixx + px*py*pixy + px*pz*pixz + py*py*piyy + py*pz*piyz + pz*pz*pizz );
        double num = A + ( 1.0 + sign * f0 ) * pmupnupimunu;
        double pmupnupimunu_max = E * E * pimunu.compute_max();
        double den = A + (1.0 + (sign * f0) ) * pmupnupimunu_max;
        shear_weight = num / den;
      }
      if (INCLUDE_BULK_DELTAF)
      {
        double H = 1.0;
        double f0 = 1.0 / ( exp(E / T) + sign );
        double num = 1.0 + ( 1.0 + sign * f0 ) * H * bulkPi;
        double Hmax = 1.0;
        double den = 1.0 + ( 1.0 + sign * f0 ) * Hmax * bulkPi;
        bulk_weight = num / den;
      }
      if (INCLUDE_BARYONDIFF_DELTAF) diff_weight = 1.0;
      //::::::::::::::::::::::::::::::::::::::::

      double viscous_weight = shear_weight * bulk_weight * diff_weight;     // could this go negative?...if so what to do? 

      double weight = rideal * viscous_weight * exp((p - E) / T);
      double propose = generate_canonical<double, numeric_limits<double>::digits>(generator);

      if(!(rideal >= 0.0 && rideal <= 1.0))
      {
        printf("Error: rideal = %f out of bounds\n", rideal);
        exit(-1);
      }
      if(!(weight >= 0.0 && weight <= 1.0))
      {
        printf("Error: weight = %f out of bounds\n", weight);
        exit(-1);
      }

      // check pLRF acceptance
      if(propose < weight)
      {
        pLRF.x = px;
        pLRF.y = py;
        pLRF.z = pz;
        break;        // exit rejection loop 
      } 

    } // rejection loop
  } // mass / T < 0.6

  //heavy hadrons
  //use variable transformation described in LongGang's Sampler Notes
  else
  {
    // determine which part of integrand dominates
    double I1 = mass_squared;
    double I2 = 2.0 * mass * T;
    double I3 = 2.0 * T * T;
    double Itot = I1 + I2 + I3;

    double propose_distribution = generate_canonical<double, numeric_limits<double>::digits>(generator);

    // accept-reject distributions to sample momentum from based on integrated weights
    if(propose_distribution < I1 / Itot)
    {
      // draw k from distribution exp(-k/T) by sampling
      // one variable r1 uniformly from [0,1):
      //    k = -T * log(r1)
      // sample LRF angles costheta = [-1,1] and phi = [0,2pi) uniformly
      uniform_real_distribution<double> phi_distribution(0.0 , 2.0 * M_PI);
      uniform_real_distribution<double> costheta_distribution(-1.0 , nextafter(1.0, numeric_limits<double>::max()));

      // sample momentum until get an acceptance 
      while(rejected)
      {
        double r1 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
        double phi = phi_distribution(generator);
        double costheta = costheta_distribution(generator);

        double k = - T * log(r1);
        double sintheta = sqrt(fabs(1.0 - costheta * costheta));

        double E = k + mass;
        double p = sqrt(fabs(E * E - mass_squared));

        double px = p * sintheta * cos(phi);
        double py = p * sintheta * sin(phi);
        double pz = p * costheta;

        double pdotdsigma_abs = fabs(E * dat - px * dax - py * day - pz * daz);
        double rideal = pdotdsigma_abs / E / dsigma_magnitude;

        //viscous corrections: (is there some way compactify this into a function?)
        //::::::::::::::::::::::::::::::::::::::::
        double shear_weight = 1.0;
        double bulk_weight = 1.0;
        double diff_weight = 1.0;
        //FIX TEMPORARY
        if (INCLUDE_SHEAR_DELTAF) 
        {
          double A = 2.0 * T * T * (eps + pressure);
          double f0 = 1.0 / ( exp(E / T) + sign );
          //check contraction / signs etc...
          double pmupnupimunu = 2.0 * ( px*px*pixx + px*py*pixy + px*pz*pixz + py*py*piyy + py*pz*piyz + pz*pz*pizz );
          double num = A + ( 1.0 + sign * f0 ) * pmupnupimunu;
          double pmupnupimunu_max = E * E * pimunu.compute_max();
          double den = A + (1.0 + (sign * f0) ) * pmupnupimunu_max;
          shear_weight = num / den;
        }
        if (INCLUDE_BULK_DELTAF)
        {
          double H = 1.0;
          double f0 = 1.0 / ( exp(E / T) + sign );
          double num = 1.0 + ( 1.0 + sign * f0 ) * H * bulkPi;
          double Hmax = 1.0;
          double den = 1.0 + ( 1.0 + sign * f0 ) * Hmax * bulkPi;
          bulk_weight = num / den;
        }
        if (INCLUDE_BARYONDIFF_DELTAF) diff_weight = 1.0;
        //::::::::::::::::::::::::::::::::::::::::

        double viscous_weight = shear_weight * bulk_weight * diff_weight;     // could this go negative?...if so what to do? 

        double weight = rideal * viscous_weight * (p / E);
        double propose = generate_canonical<double, numeric_limits<double>::digits>(generator);

        if(!(rideal >= 0.0 && rideal <= 1.0))
        {
          printf("Error: rideal = %f out of bounds\n", rideal);
          exit(-1);
        }
        if(!(weight >= 0.0 && weight <= 1.0))
        {
          printf("Error: weight = %f out of bounds\n", weight);
          exit(-1);
        }

        // check pLRF acceptance
        if(propose < weight)
        {
          pLRF.x = px;
          pLRF.y = py;
          pLRF.z = pz;
          break;        // exit rejection loop 
        } 

      } // rejection loop
    } // distribution 1
    else if (propose_distribution < (I1 + I2) / Itot)
    {
      // draw (k,phi) from distribution k * exp(-k/T) by
      // sampling two variables (r1,r2) uniformly from [0,1):
      //    k = -T * log(r1)
      //    phi = 2 * \pi * log(r1) / (log(r1) + log(r2)); (double check this formula!!)
      // sample LRF angle costheta = [-1,1] uniformly
      uniform_real_distribution<double> costheta_distribution(-1.0 , nextafter(1.0, numeric_limits<double>::max()));

      // sample momentum until get acceptance 
      while (rejected)
      {
        double r1 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
        double r2 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
        double costheta = costheta_distribution(generator);

        double l1 = log(r1);
        double l2 = log(r2);

        double k = - T * (l1 + l2);
        double phi = 2.0 * M_PI * l1 / (l1 + l2);
        double sintheta = sqrt(fabs(1.0 - costheta * costheta));

        double E = k + mass;
        double p = sqrt(fabs(E * E - mass_squared));

        double px = p * sintheta * cos(phi);
        double py = p * sintheta * sin(phi);
        double pz = p * costheta;

        double pdotdsigma_abs = fabs(E * dat - px * dax - py * day - pz * daz);
        double rideal = pdotdsigma_abs / E / dsigma_magnitude;

        //viscous corrections: (is there some way compactify this into a function?)
        //::::::::::::::::::::::::::::::::::::::::
        double shear_weight = 1.0;
        double bulk_weight = 1.0;
        double diff_weight = 1.0;
        //FIX TEMPORARY
        if (INCLUDE_SHEAR_DELTAF) 
        {
          double A = 2.0 * T * T * (eps + pressure);
          double f0 = 1.0 / ( exp(E / T) + sign );
          //check contraction / signs etc...
          double pmupnupimunu = 2.0 * ( px*px*pixx + px*py*pixy + px*pz*pixz + py*py*piyy + py*pz*piyz + pz*pz*pizz );
          double num = A + ( 1.0 + sign * f0 ) * pmupnupimunu;
          double pmupnupimunu_max = E * E * pimunu.compute_max();
          double den = A + (1.0 + (sign * f0) ) * pmupnupimunu_max;
          shear_weight = num / den;
        }
        if (INCLUDE_BULK_DELTAF)
        {
          double H = 1.0;
          double f0 = 1.0 / ( exp(E / T) + sign );
          double num = 1.0 + ( 1.0 + sign * f0 ) * H * bulkPi;
          double Hmax = 1.0;
          double den = 1.0 + ( 1.0 + sign * f0 ) * Hmax * bulkPi;
          bulk_weight = num / den;
        }
        if (INCLUDE_BARYONDIFF_DELTAF) diff_weight = 1.0;
        //::::::::::::::::::::::::::::::::::::::::

        double viscous_weight = shear_weight * bulk_weight * diff_weight;     // could this go negative?...if so what to do? 

        double weight = rideal * viscous_weight * (p / E);
        double propose = generate_canonical<double, numeric_limits<double>::digits>(generator);

        if(!(rideal >= 0.0 && rideal <= 1.0))
        {
          printf("Error: rideal = %f out of bounds\n", rideal);
          exit(-1);
        }
        if(!(weight >= 0.0 && weight <= 1.0))
        {
          printf("Error: weight = %f out of bounds\n", weight);
          exit(-1);
        }

        // check pLRF acceptance
        if(propose < weight)
        {
          pLRF.x = px;
          pLRF.y = py;
          pLRF.z = pz;
          break;        // exit rejection loop 
        } 

      } // rejection loop
    } // distribution 2
    else
    {
      // draw (k,phi,costheta) from k^2 * exp(-k/T) distribution by
      // sampling three variables (r1,r2,r3) uniformly from [0,1):
      //    k = - T * (log(r1) + log(r2) + log(r3))
      //    phi = 2 * \pi * (log(r1) + log(r2))^2 / (log(r1) + log(r2) + log(r3))^2
      //    costheta = (log(r1) - log(r2)) / (log(r1) + log(r2))

      // sample momentum until get acceptance 
      while(rejected)
      {
        double r1 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
        double r2 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
        double r3 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);

        double l1 = log(r1);
        double l2 = log(r2);
        double l3 = log(r3);

        double k = - T * (l1 + l2 + l3);
        double phi = 2.0 * M_PI * pow(l1 + l2, 2) / pow(l1 + l2 + l3, 2);
        double costheta = (l1 - l2) / (l1 + l2);
        double sintheta = sqrt(1.0 - costheta * costheta);

        double E = k + mass;
        double p = sqrt(fabs(E * E - mass_squared));

        double px = p * sintheta * cos(phi);
        double py = p * sintheta * sin(phi);
        double pz = p * costheta;

        double pdotdsigma_abs = fabs(E * dat - px * dax - py * day - pz * daz);
        double rideal = pdotdsigma_abs / E / dsigma_magnitude;

        //viscous corrections: (is there some way compactify this into a function?)
        //::::::::::::::::::::::::::::::::::::::::
        double shear_weight = 1.0;
        double bulk_weight = 1.0;
        double diff_weight = 1.0;
        //FIX TEMPORARY
        if (INCLUDE_SHEAR_DELTAF) 
        {
          double A = 2.0 * T * T * (eps + pressure);
          double f0 = 1.0 / ( exp(E / T) + sign );
          //check contraction / signs etc...
          double pmupnupimunu = 2.0 * ( px*px*pixx + px*py*pixy + px*pz*pixz + py*py*piyy + py*pz*piyz + pz*pz*pizz );
          double num = A + ( 1.0 + sign * f0 ) * pmupnupimunu;
          double pmupnupimunu_max = E * E * pimunu.compute_max();
          double den = A + (1.0 + (sign * f0) ) * pmupnupimunu_max;
          shear_weight = num / den;
        }
        if (INCLUDE_BULK_DELTAF)
        {
          double H = 1.0;
          double f0 = 1.0 / ( exp(E / T) + sign );
          double num = 1.0 + ( 1.0 + sign * f0 ) * H * bulkPi;
          double Hmax = 1.0;
          double den = 1.0 + ( 1.0 + sign * f0 ) * Hmax * bulkPi;
          bulk_weight = num / den;
        }
        if (INCLUDE_BARYONDIFF_DELTAF) diff_weight = 1.0;
        //::::::::::::::::::::::::::::::::::::::::

        double viscous_weight = shear_weight * bulk_weight * diff_weight;     // could this go negative?...if so what to do? 

        double weight = rideal * viscous_weight * (p / E);
        double propose = generate_canonical<double, numeric_limits<double>::digits>(generator);

        if(!(rideal >= 0.0 && rideal <= 1.0))
        {
          printf("Error: rideal = %f out of bounds\n", rideal);
          exit(-1);
        }
        if(!(weight >= 0.0 && weight <= 1.0))
        {
          printf("Error: weight = %f out of bounds\n", weight);
          exit(-1);
        }

        // check pLRF acceptance 
        if(propose < weight)
        {
          pLRF.x = px;
          pLRF.y = py;
          pLRF.z = pz;
          break;        // exit rejection loop 
        } 
        
      }
    } // distribution 3
  } // heavy hadron
  return pLRF;
}


lrf_momentum Rescale_Momentum(lrf_momentum pLRF_mod, double mass_squared, double baryon, Shear_Stress_Tensor pimunu, Baryon_Diffusion_Current Vmu, double shear_coeff, double bulk_coeff, double diff_coeff, double baryon_enthalpy_ratio)
{
    // rescale modified momentum p_mod with matrix Mij
    double E_mod = pLRF_mod.E;
    double px_mod = pLRF_mod.x;
    double py_mod = pLRF_mod.y;
    double pz_mod = pLRF_mod.z;

    // LRF momentum (ideal by default or no viscous corrections)
    double px_LRF = px_mod;
    double py_LRF = py_mod;
    double pz_LRF = pz_mod;

    // LRF shear stress components
    double pixx_LRF = pimunu.pixx_LRF;
    double pixy_LRF = pimunu.pixy_LRF;
    double pixz_LRF = pimunu.pixz_LRF;
    double piyy_LRF = pimunu.piyy_LRF;
    double piyz_LRF = pimunu.piyz_LRF;
    double pizz_LRF = pimunu.pizz_LRF;

    // LRF baryon diffusion components
    double Vx_LRF = Vmu.Vx_LRF;
    double Vy_LRF = Vmu.Vy_LRF;
    double Vz_LRF = Vmu.Vz_LRF;
    double diffusion_factor = diff_coeff * (E_mod * baryon_enthalpy_ratio + baryon);

    // rescale momentum:
    //::::::::::::::::::::::::::::::::::::::::
    // add shear terms
    px_LRF += shear_coeff * (pixx_LRF * px_mod  +  pixy_LRF * py_mod  +  pixz_LRF * pz_mod);
    py_LRF += shear_coeff * (pixy_LRF * px_mod  +  piyy_LRF * py_mod  +  piyz_LRF * pz_mod);
    pz_LRF += shear_coeff * (pixz_LRF * px_mod  +  piyz_LRF * py_mod  +  pizz_LRF * pz_mod);

    // add bulk terms
    px_LRF += bulk_coeff * px_mod;
    py_LRF += bulk_coeff * py_mod;
    pz_LRF += bulk_coeff * pz_mod;

    // add baryon diffusion terms
    px_LRF += diffusion_factor * Vx_LRF;
    py_LRF += diffusion_factor * Vy_LRF;
    pz_LRF += diffusion_factor * Vz_LRF;
    //::::::::::::::::::::::::::::::::::::::::

    // set LRF momentum
    lrf_momentum pLRF;
    pLRF.x = px_LRF;
    pLRF.y = py_LRF;
    pLRF.z = pz_LRF;
    pLRF.E = sqrt(mass_squared  +  px_LRF * px_LRF  +  py_LRF * py_LRF  +  pz_LRF * pz_LRF);

    return pLRF;
}


lrf_momentum Sample_Momentum_mod(double mass, double baryon, double T_mod, double alphaB_mod, dsigma_Vector ds, Shear_Stress_Tensor pimunu, Baryon_Diffusion_Current Vmu, double shear_coeff, double bulk_coeff, double diff_coeff, double baryon_enthalpy_ratio)
{
  double two_pi = 2.0 * M_PI; 

  lrf_momentum pLRF;                          // sampled LRF momentum
  lrf_momentum pLRF_mod;                      // sampled modified LRF momentum

  bool rejected = true;                       // for acceptance / rejection loop

  double mass_squared = mass * mass;          // mass squared

  double dst = ds.dsigmat_LRF;                // dsigma LRF components
  double dsx = ds.dsigmax_LRF;
  double dsy = ds.dsigmay_LRF;
  double dsz = ds.dsigmaz_LRF;
  double ds_mag = ds.dsigma_magnitude;

  unsigned seed = chrono::system_clock::now().time_since_epoch().count();
  default_random_engine generator(seed);

  // pion routine (not adaptive rejection sampling, but naive method here.. ~ massless Fermi distribution)
  if(mass / T_mod < 1.5)
  {
    while(rejected)
    {
      // draw (p_mod, phi_mod, costheta_mod) from distribution p_mod^2 * exp(-p_mod / T_mod) by
      // sampling three variables (r1,r2,r3) uniformly from [0,1)
      double r1 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
      double r2 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
      double r3 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);

      double l1 = log(r1);
      double l2 = log(r2);
      double l3 = log(r3);

      double p_mod = - T_mod * (l1 + l2 + l3);                  // modified 3-momentum magnitude
      double E_mod = sqrt(fabs(p_mod * p_mod + mass_squared));  // modified energy
      
      // calculate modified angles and p_mod LRF components
      double phi_mod = two_pi * (l1 + l2) * (l1 + l2) / ((l1 + l2 + l3) * (l1 + l2 + l3));
      double costheta_mod = (l1 - l2) / (l1 + l2);
      double sintheta_mod = sqrt(fabs(1.0 - costheta_mod * costheta_mod));

      pLRF_mod.E = E_mod;
      pLRF_mod.x = p_mod * sintheta_mod * cos(phi_mod);
      pLRF_mod.y = p_mod * sintheta_mod * sin(phi_mod);
      pLRF_mod.z = p_mod * costheta_mod;
      
      // momentum rescaling (* highlight *)
      pLRF = Rescale_Momentum(pLRF_mod, mass_squared, baryon, pimunu, Vmu, shear_coeff, bulk_coeff, diff_coeff, baryon_enthalpy_ratio);

      double E = pLRF.E;
      double p = sqrt(fabs(E * E - mass_squared)); 

      double pdsigma_abs = fabs(E * dst - pLRF.x * dsx - pLRF.y * dsy - pLRF.z * dsz);
      double rideal = pdsigma_abs / E / ds_mag;

      //here the pion weight should include the Bose enhancement factor
      //this formula assumes zero chemical potential mu = 0
      //TO DO - generalize for nonzero chemical potential
      double weight = rideal * exp(p_mod / T_mod) / (exp(E_mod / T_mod) - 1.0);  
      double propose = generate_canonical<double, numeric_limits<double>::digits>(generator);

      if(!(rideal >= 0.0 && rideal <= 1.0)){printf("Error: rideal = %f out of bounds\n", rideal);exit(-1);}
      if(!(weight >= 0.0 && weight <= 1.0)){printf("Error: weight = %f out of bounds\n", weight);exit(-1);}

      // check pLRF acceptance
      if(propose < weight) 
      {
        break;  // exit rejection loop 
      } 

    } // rejection loop
  } //if (mass / T_mod < 1.5)

  // light hadrons, but heavier than pions (~ massless Boltzmann distribution)
  else if (mass / T_mod < 2.0) 
  {
    while(rejected)
    {
      // draw (p_mod, phi_mod, costheta_mod) from distribution p_mod^2 * exp(-p_mod / T_mod) by
      // sampling three variables (r1,r2,r3) uniformly from [0,1)
      double r1 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
      double r2 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
      double r3 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);

      double l1 = log(r1);
      double l2 = log(r2);
      double l3 = log(r3);

      double p_mod = - T_mod * (l1 + l2 + l3);
      double E_mod = sqrt(fabs(p_mod * p_mod + mass_squared));

      double phi_mod = two_pi * (l1 + l2) * (l1 + l2) / ((l1 + l2 + l3) * (l1 + l2 + l3));
      double costheta_mod = (l1 - l2) / (l1 + l2);
      double sintheta_mod = sqrt(fabs(1.0 - costheta_mod * costheta_mod));

      pLRF_mod.E = E_mod; 
      pLRF_mod.x = p_mod * sintheta_mod * cos(phi_mod);
      pLRF_mod.y = p_mod * sintheta_mod * sin(phi_mod);
      pLRF_mod.z = p_mod * costheta_mod;
      
      // momentum rescaling (* highlight *)
      pLRF = Rescale_Momentum(pLRF_mod, mass_squared, baryon, pimunu, Vmu, shear_coeff, bulk_coeff, diff_coeff, baryon_enthalpy_ratio);

      double pdsigma_abs = fabs(pLRF.E * dst - pLRF.x * dsx - pLRF.y * dsy - pLRF.z * dsz);
      double rideal = pdsigma_abs / pLRF.E / ds_mag;

      double weight = rideal * exp((p_mod - E_mod) / T_mod);
      double propose = generate_canonical<double, numeric_limits<double>::digits>(generator);

      if(!(rideal >= 0.0 && rideal <= 1.0)){printf("Error: rideal = %f out of bounds\n", rideal);exit(-1);}
      if(!(weight >= 0.0 && weight <= 1.0)){printf("Error: weight = %f out of bounds\n", weight);exit(-1);}

      // check pLRF acceptance
      if(propose < weight)
      {
        break;  // exit rejection loop 
      } 

    } // rejection loop
  } // mass / T_mod < 2.0 

  //heavy hadrons
  //use variable transformation described in LongGang's Sampler Notes
  else
  {
    // determine which part of integrand dominates
    double I1 = mass_squared;
    double I2 = 2.0 * mass * T_mod;
    double I3 = 2.0 * T_mod * T_mod;    // do these formulas change for the mod case other than using T_mod? (detM?...)
    double Itot = I1 + I2 + I3;

    double propose_distribution = generate_canonical<double, numeric_limits<double>::digits>(generator);

    // accept-reject distributions to sample momentum from based on integrated weights
    if(propose_distribution < I1 / Itot)
    {
      // draw k_mod from distribution exp(-k_mod / T_mod) by sampling
      // one variable r1 uniformly from [0,1):
      //    k_mod = -T_mod * log(r1)
      // sample modified LRF angles costheta_mod = [-1,1] and phi_mod = [0,2pi) uniformly
      uniform_real_distribution<double> phi_distribution(0.0, two_pi);
      uniform_real_distribution<double> costheta_distribution(-1.0, nextafter(1.0, numeric_limits<double>::max()));

      while(rejected)
      {
        double r1 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
        double phi_mod = phi_distribution(generator);
        double costheta_mod = costheta_distribution(generator);

        double k_mod = - T_mod * log(r1);
        double sintheta_mod = sqrt(fabs(1.0 - costheta_mod * costheta_mod));

        double E_mod = k_mod + mass;
        double p_mod = sqrt(fabs(E_mod * E_mod - mass_squared));

        pLRF_mod.E = E_mod; 
        pLRF_mod.x = p_mod * sintheta_mod * cos(phi_mod);
        pLRF_mod.y = p_mod * sintheta_mod * sin(phi_mod);
        pLRF_mod.z = p_mod * costheta_mod;

        // momentum rescaling (* highlight *)
        pLRF = Rescale_Momentum(pLRF_mod, mass_squared, baryon, pimunu, Vmu, shear_coeff, bulk_coeff, diff_coeff, baryon_enthalpy_ratio);

        double pdsigma_abs = fabs(pLRF.E * dst - pLRF.x * dsx - pLRF.y * dsy - pLRF.z * dsz);
        double rideal = pdsigma_abs / pLRF.E / ds_mag;

        double weight = rideal * (p_mod / E_mod);
        double propose = generate_canonical<double, numeric_limits<double>::digits>(generator);

        if(!(rideal >= 0.0 && rideal <= 1.0)){printf("Error: rideal = %f out of bounds\n", rideal);exit(-1);}
        if(!(weight >= 0.0 && weight <= 1.0)){printf("Error: weight = %f out of bounds\n", weight);exit(-1);}

        // check pLRF acceptance
        if(propose < weight)
        {
          break;  // exit rejection loop 
        }

      } // rejection loop
    } // distribution 1
    else if (propose_distribution < (I1 + I2) / Itot)
    {
      // draw (k_mod,phi_mod) from distribution k_mod * exp(-k_mod/T_mod) by
      // sampling two variables (r1,r2) uniformly from [0,1):
      //    k_mod = - T_mod * log(r1)
      //    phi_mod = 2 * pi * log(r1) / (log(r1) + log(r2)); 
      // sample LRF angle costheta_mod = [-1,1] uniformly
      uniform_real_distribution<double> costheta_distribution(-1.0, nextafter(1.0, numeric_limits<double>::max()));

      bool rejected = true;
      while (rejected)
      {
        double r1 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
        double r2 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
        double costheta_mod = costheta_distribution(generator);

        double l1 = log(r1);
        double l2 = log(r2);

        double k_mod = - T_mod * (l1 + l2);
        double phi_mod = two_pi * l1 / (l1 + l2);
        double sintheta_mod = sqrt(fabs(1.0 - costheta_mod * costheta_mod));

        double E_mod = k_mod + mass;
        double p_mod = sqrt(fabs(E_mod * E_mod - mass_squared));

        pLRF_mod.E = E_mod; 
        pLRF_mod.x = p_mod * sintheta_mod * cos(phi_mod);
        pLRF_mod.y = p_mod * sintheta_mod * sin(phi_mod);
        pLRF_mod.z = p_mod * costheta_mod;

        // momentum rescaling (* highlight *)
        pLRF = Rescale_Momentum(pLRF_mod, mass_squared, baryon, pimunu, Vmu, shear_coeff, bulk_coeff, diff_coeff, baryon_enthalpy_ratio);

        double pdsigma_abs = fabs(pLRF.E * dst - pLRF.x * dsx - pLRF.y * dsy - pLRF.z * dsz);
        double rideal = pdsigma_abs / pLRF.E / ds_mag;

        double weight = rideal * (p_mod / E_mod);
        double propose = generate_canonical<double, numeric_limits<double>::digits>(generator);

        if(!(rideal >= 0.0 && rideal <= 1.0)){printf("Error: rideal = %f out of bounds\n", rideal);exit(-1);}
        if(!(weight >= 0.0 && weight <= 1.0)){printf("Error: weight = %f out of bounds\n", weight);exit(-1);}

        // check pLRF acceptance
        if(propose < weight)
        {
          break;
        }

      } // rejection loop
    } // distribution 2
    else
    {
      // draw (k_mod,phi_mod,costheta_mod) from k_mod^2 * exp(-k_mod / T_mod) distribution by
      // sampling three variables (r1,r2,r3) uniformly from [0,1):
      //    k_mod = - T_mod * (log(r1) + log(r2) + log(r3))
      //    phi_mod = 2 * \pi * (log(r1) + log(r2))^2 / (log(r1) + log(r2) + log(r3))^2
      //    costheta_mod = (log(r1) - log(r2)) / (log(r1) + log(r2))

      while(rejected)
      {
        double r1 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
        double r2 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);
        double r3 = 1.0 - generate_canonical<double, numeric_limits<double>::digits>(generator);

        double l1 = log(r1);
        double l2 = log(r2);
        double l3 = log(r3);

        double k_mod = - T_mod * (l1 + l2 + l3);
        double phi_mod = 2.0 * M_PI * (l1 + l2) * (l1 + l2) / ((l1 + l2 + l3) * (l1 + l2 + l3));
        double costheta_mod = (l1 - l2) / (l1 + l2);
        double sintheta_mod = sqrt(1.0 - costheta_mod * costheta_mod);

        double E_mod = k_mod + mass;
        double p_mod = sqrt(fabs(E_mod * E_mod - mass_squared));

        pLRF_mod.E = E_mod; 
        pLRF_mod.x = p_mod * sintheta_mod * cos(phi_mod);
        pLRF_mod.y = p_mod * sintheta_mod * sin(phi_mod);
        pLRF_mod.z = p_mod * costheta_mod;

        // momentum rescaling (* highlight *)
        pLRF = Rescale_Momentum(pLRF_mod, mass_squared, baryon, pimunu, Vmu, shear_coeff, bulk_coeff, diff_coeff, baryon_enthalpy_ratio);

        double pdsigma_abs = fabs(pLRF.E * dst - pLRF.x * dsx - pLRF.y * dsy - pLRF.z * dsz);
        double rideal = pdsigma_abs / pLRF.E / ds_mag;

        double weight = rideal * (p_mod / E_mod);
        double propose = generate_canonical<double, numeric_limits<double>::digits>(generator);

        if(!(rideal >= 0.0 && rideal <= 1.0)){printf("Error: rideal = %f out of bounds\n", rideal);exit(-1);}
        if(!(weight >= 0.0 && weight <= 1.0)){printf("Error: weight = %f out of bounds\n", weight);exit(-1);}

        // check pLRF acceptance 
        if(propose < weight)
        {
          break;  // exit rejection loop 
        }

      } // rejection loop 
    } // distribution 3
  } // heavy hadron
  return pLRF;
}


void EmissionFunctionArray::sample_dN_pTdpTdphidy(double *Mass, double *Sign, double *Degeneracy, double *Baryon, int *MCID,
  double *T_fo, double *P_fo, double *E_fo, double *tau_fo, double *x_fo, double *y_fo, double *eta_fo, double *ut_fo, double *ux_fo, double *uy_fo, double *un_fo,
  double *dat_fo, double *dax_fo, double *day_fo, double *dan_fo,
  double *pitt_fo, double *pitx_fo, double *pity_fo, double *pitn_fo, double *pixx_fo, double *pixy_fo, double *pixn_fo, double *piyy_fo, double *piyn_fo, double *pinn_fo, double *bulkPi_fo,
  double *muB_fo, double *nB_fo, double *Vt_fo, double *Vx_fo, double *Vy_fo, double *Vn_fo, double *df_coeff,
  int pbar_pts, double *pbar_root1, double *pbar_weight1, double *pbar_root2, double *pbar_weight2, double *pbar_root3, double *pbar_weight3)
  {
    printf("sampling particles from vhydro with df...\n");
    int npart = number_of_chosen_particles;
    double two_pi2_hbarC3 = 2.0 * pow(M_PI,2) * pow(hbarC,3);

    //set seed of random devices using clock time to produce nonidentical events
    unsigned seed1 = chrono::system_clock::now().time_since_epoch().count();
    unsigned seed2 = chrono::system_clock::now().time_since_epoch().count();
    default_random_engine gen1(seed1);
    default_random_engine gen2(seed2);

    int eta_pts = 1;
    if(DIMENSION == 2) eta_pts = eta_tab_length;
    double etaValues[eta_pts];
    double etaDeltaWeights[eta_pts];    // eta_weight * delta_eta
    double delta_eta = (eta_tab->get(1,2)) - (eta_tab->get(1,1));  // assume uniform grid

    if(DIMENSION == 2)
    {
      for(int ieta = 0; ieta < eta_pts; ieta++)
      {
        etaValues[ieta] = eta_tab->get(1, ieta + 1);
        etaDeltaWeights[ieta] = (eta_tab->get(2, ieta + 1)) * delta_eta;
      }
    }
    else if(DIMENSION == 3)
    {
      etaValues[0] = 0.0;       // below, will load eta_fo
      etaDeltaWeights[0] = 1.0; // 1.0 for 3+1d
    }

    // set df coefficients (viscous corrections)
    double c0 = 0.0;
    double c1 = 0.0;
    double c2 = 0.0;
    double c3 = 0.0;
    double c4 = 0.0;
    double F = 0.0;
    double G = 0.0;
    double betabulk = 0.0;
    double betaV = 0.0;
    double betapi = 0.0;

    switch(DF_MODE)
    {
      case 1: // 14-moment
      {
        // bulk coefficients
        c0 = df_coeff[0];
        c1 = df_coeff[1];
        c2 = df_coeff[2];
        // diffusion coefficients
        c3 = df_coeff[3];
        c4 = df_coeff[4];
        // shear coefficient evaluated later
        break;
      }
      case 2: // Chapman-Enskog
      {
        // bulk coefficients
        F = df_coeff[0];
        G = df_coeff[1];
        betabulk = df_coeff[2];
        // diffusion coefficient
        betaV = df_coeff[3];
        // shear coefficient
        betapi = df_coeff[4];
        break;
      }
      default:
      {
        cout << "Please choose df_mode = 1 or 2 in parameters.dat" << endl;
        exit(-1);
      }
    }

    //loop over all freezeout cells
    #pragma omp parallel for
    for (int icell = 0; icell < FO_length; icell++)
    {
      double tau = tau_fo[icell];         // longitudinal proper time
      double x = x_fo[icell];             // x position
      double y = y_fo[icell];             // y position
      double tau2 = tau * tau;

      if(DIMENSION == 3)
      {
        etaValues[0] = eta_fo[icell];     // spacetime rapidity from surface file
      }

      double dat = dat_fo[icell];         // covariant normal surface vector dsigma_mu
      double dax = dax_fo[icell];
      double day = day_fo[icell];
      double dan = dan_fo[icell];         // dan should be 0 in 2+1d case

      double ux = ux_fo[icell];           // contravariant fluid velocity u^mu
      double uy = uy_fo[icell];           // enforce normalization
      double un = un_fo[icell];           // u^\eta
      double ux2 = ux * ux;
      double uy2 = uy * uy;
      double uperp =  sqrt(ux2 + uy2);
      double ut = sqrt(fabs(1.0 + uperp * uperp + tau2*un*un)); // u^\tau

      double ut2 = ut * ut;
      double utperp = sqrt(1.0 + uperp * uperp);  // ut as if un = 0

      double T = T_fo[icell];             // temperature
      double E = E_fo[icell];             // energy density
      double P = P_fo[icell];             // pressure

      double pixx = pixx_fo[icell];       // contravariant shear stress tensor pi^munu
      double pixy = pixy_fo[icell];       // enforce orthogonality and tracelessness
      double pixn = pixn_fo[icell];
      double piyy = piyy_fo[icell];
      double piyn = piyn_fo[icell];
      double pinn = (pixx*(ux2 - ut2) + piyy*(uy2 - ut2) + 2.0*(pixy*ux*uy + tau2*un*(pixn*ux + piyn*uy))) / (tau2 * utperp * utperp);
      double pitn = (pixn*ux + piyn*uy + tau2*pinn*un) / ut;
      double pity = (pixy*ux + piyy*uy + tau2*piyn*un) / ut;
      double pitx = (pixx*ux + pixy*uy + tau2*pixn*un) / ut;
      double pitt = (pitx*ux + pity*uy + tau2*pitn*un) / ut;

      double bulkPi = bulkPi_fo[icell];   // bulk pressure

      double muB = 0.0;
      double alphaB = 0.0;                // baryon chemical potential
      double nB = 0.0;                    // net baryon density
      double Vt = 0.0;                    // baryon diffusion (enforce orthogonality)
      double Vx = 0.0;
      double Vy = 0.0;
      double Vn = 0.0;
      double Vdotdsigma = 0.0;
      double baryon_enthalpy_ratio = 0.0;

      if(INCLUDE_BARYON)
      {
        muB = muB_fo[icell];
        alphaB = muB / T;

        if(INCLUDE_BARYONDIFF_DELTAF)
        {
          nB = nB_fo[icell];
          Vx = Vx_fo[icell];
          Vy = Vy_fo[icell];
          Vn = Vn_fo[icell];
          Vt = (Vx*ux + Vy*uy + tau2*Vn*un) / ut;
          baryon_enthalpy_ratio = nB / (E + P);
        }
      }

      // set milne basis vectors:
      double Xt, Xx, Xy, Xn;  // X^mu
      double Yx, Yy;          // Y^mu
      double Zt, Zn;          // Z^mu

      double sinhL = tau * un / utperp;
      double coshL = ut / utperp;

      Xt = uperp * coshL;
      Xn = uperp * sinhL / tau;
      Zt = sinhL;
      Zn = coshL / tau;

      if(uperp < 1.e-5) // stops (ux=0)/(uperp=0) nans
      {
        Xx = 1.0; Xy = 0.0;
        Yx = 0.0; Yy = 1.0;
      }
      else
      {
        Xx = utperp * ux / uperp;
        Xy = utperp * uy / uperp;
        Yx = - uy / uperp;
        Yy = ux / uperp;
      }

      // dsigma_mu in the LRF (delta_eta_weight factored out because it drops out in r_ideal)
      //    dat_LRF = u^mu . dsigma_mu
      //    dai_LRF = - X_i^mu . dsigma_mu
      //    dsigma_time = u.dsigma
      //    dsigma_space = sqrt((u.dsigma)^2 - dsigma.dsigma)
      lrf_dsigma dsigmaLRF;
      dsigmaLRF.t = dat * ut  +  dax * ux  +  day * uy  +  dan * un;
      dsigmaLRF.x = -(dat * Xt  +  dax * Xx  +  day * Xy  +  dan * Xn);
      dsigmaLRF.y = -(dax * Yx  +  day * Yy);
      dsigmaLRF.z = -(dat * Zt  +  dan * Zn);

      double dsigma_time = dsigmaLRF.t;
      double dsigma_space = sqrt(dsigmaLRF.x * dsigmaLRF.x +  dsigmaLRF.y * dsigmaLRF.y  +  dsigmaLRF.z * dsigmaLRF.z);
      double dsigma_magnitude = dsigma_time + dsigma_space;

      // loop over eta points
      for(int ieta = 0; ieta < eta_pts; ieta++)
      {
        double delta_eta_weight = etaDeltaWeights[ieta];
        double udotdsigma = delta_eta_weight * dsigma_time;
        if(udotdsigma <= 0.0)
        {
          break;
        }
        double eta = etaValues[ieta];
        double cosheta = cosh(eta);   // I could make a table beforehand
        double sinheta = sinh(eta);

        double Vdotdsigma = 0.0;

        // V^\mu d^3sigma_\mu
        if (INCLUDE_BARYON && INCLUDE_BARYONDIFF_DELTAF)
        {
          Vdotdsigma = delta_eta_weight * (dat * Vt + dax * Vx + day * Vy + dan * Vn);
        }

        // a list to hold the mean number of each species in FO cell
        std::vector<double> density_list;
        density_list.resize(npart);

        // sum the total mean number of hadrons in FO cell:
        double dN_tot = 0.0;

        // loop over hadrons
        for(int ipart = 0; ipart < npart; ipart++)
        {
          // set particle properties
          double mass = Mass[ipart];    // (GeV)
          double mass2 = mass * mass;
          double mbar = mass / T;
          double sign = Sign[ipart];
          double degeneracy = Degeneracy[ipart];
          double baryon = 0.0;
          double chem = 0.0;           // chemical potential term in feq
          if(INCLUDE_BARYON)
          {
            baryon = Baryon[ipart];
            chem = baryon * alphaB;
          }

          // first calculate thermal equilibrium particles
          double neq = 0.0;
          double dN_thermal = 0.0;

          // for light particles need more than leading order term in BE or FD expansion
          int jmax = 2;
          if(mass / T < 2.0) jmax = 10;

          double sign_factor = -sign;

          for(int j = 1; j < jmax; j++) // truncate expansion of BE or FD distribution
          {
            double k = (double)j;
            sign_factor *= (-sign);
            neq += (sign_factor * exp(k * chem) * gsl_sf_bessel_Kn(2, k * mbar) / k); // fixed chem term 7/15
          }
          neq *= (degeneracy * mass2 * T / two_pi2_hbarC3);

          dN_thermal = udotdsigma * neq;

          // bulk pressure: density correction
          double dN_bulk = 0.0;
          if(INCLUDE_BULK_DELTAF)
          {
            switch(DF_MODE)
            {
              case 1: // 14 moment (not sure what the status is)
              {
                double J10_fact = pow(T,3) / two_pi2_hbarC3;
                double J20_fact = pow(T,4) / two_pi2_hbarC3;
                double J30_fact = pow(T,5) / two_pi2_hbarC3;
                double J10 = degeneracy * J10_fact * GaussThermal(J10_int, pbar_root1, pbar_weight1, pbar_pts, mbar, alphaB, baryon, sign);
                double J20 = degeneracy * J20_fact * GaussThermal(J20_int, pbar_root2, pbar_weight2, pbar_pts, mbar, alphaB, baryon, sign);
                double J30 = degeneracy * J30_fact * GaussThermal(J30_int, pbar_root3, pbar_weight3, pbar_pts, mbar, alphaB, baryon, sign);

                dN_bulk = udotdsigma * bulkPi * ((c0 - c2) * mass2 * J10 + (c1 * baryon * J20) + (4.0 * c2 - c0) * J30);
                break;
              }
              case 2: // Chapman-Enskog
              {
                double J10_fact = pow(T,3) / two_pi2_hbarC3;
                double J20_fact = pow(T,4) / two_pi2_hbarC3;
                double J10 = degeneracy * J10_fact * GaussThermal(J10_int, pbar_root1, pbar_weight1, pbar_pts, mbar, alphaB, baryon, sign);
                double J20 = degeneracy * J20_fact * GaussThermal(J20_int, pbar_root2, pbar_weight2, pbar_pts, mbar, alphaB, baryon, sign);
                dN_bulk = udotdsigma * (bulkPi / betabulk) * (neq + (baryon * J10 * G) + (J20 * F / T / T));

                break;
              }
              default:
              {
                cout << "Please choose df_mode = 1 or 2 in parameters.dat" << endl;
                exit(-1);
              }
            } //switch(DF_MODE)
          } //if(INCLUDE_BULK_DELTAF)

          // baryon diffusion: density correction
          double dN_diff = 0.0;
          if(INCLUDE_BARYONDIFF_DELTAF)
          {
            switch(DF_MODE)
            {
              case 1: // 14 moment
              {
                // it's probably faster to use J31
                double J31_fact = pow(T,5) / two_pi2_hbarC3 / 3.0;
                double J31 = degeneracy * J31_fact * GaussThermal(J31_int, pbar_root3, pbar_weight3, pbar_pts, mbar, alphaB, baryon, sign);
                // these coefficients need to be loaded.
                // c3 ~ cV / V
                // c4 ~ 2cW / V
                dN_diff = - (Vdotdsigma / betaV) * ((baryon * c3 * neq * T) + (c4 * J31));
                break;
              }
              case 2: // Chapman Enskog
              {
                double J11_fact = pow(T,3) / two_pi2_hbarC3 / 3.0;
                double J11 = degeneracy * J11_fact * GaussThermal(J11_int, pbar_root1, pbar_weight1, pbar_pts, mbar, alphaB, baryon, sign);
                dN_diff = - (Vdotdsigma / betaV) * (neq * T * baryon_enthalpy_ratio - baryon * J11);
                break;
              }
              default:
              {
                cout << "Please choose df_mode = 1 or 2 in parameters.dat" << endl;
                exit(-1);
              }
            } //switch(DF_MODE)
          } //if(INCLUDE_BARYONDIFF_DELTAF)

          // mean number of particles of species ipart
          // save to list to later sample inidividual species
          double dN = dN_thermal + dN_bulk + dN_diff;
          density_list[ipart] = dN;

          // add to total mean number of hadrons for FO cell
          dN_tot += dN;
        } // loop over hadrons (ipart)


        // now sample the number of particles (all species)
        std::poisson_distribution<> poisson_distr(dN_tot);
        //sample total number of hadrons in FO cell
        int N_hadrons = poisson_distr(gen1);

        //make a discrete distribution with weights according to particle densities
        std::discrete_distribution<int> particle_numbers(density_list.begin(), density_list.end());

        for (int n = 0; n < N_hadrons; n++)
        {
          // sample discrete distribution
          int idx_sampled = particle_numbers(gen2); //this gives the index of the sampled particle
          // get the mass of the sampled particle
          double mass = Mass[idx_sampled];  // (GeV)
          double mass2 = mass * mass;
          // get the MC ID of the sampled particle
          int mcid = MCID[idx_sampled];
          double sign = Sign[idx_sampled];

          //now we need to boost the shear stress to LRF to compute the vicous sampling weight
          // 9/14: this could be done in the outer loops:
          Shear_Tensor pimunu;
          pimunu.pitt = pitt;
          pimunu.pitx = pitx;
          pimunu.pity = pity;
          pimunu.pitn = pitn;
          pimunu.pixx = pixx;
          pimunu.pixy = pixy;
          pimunu.pixn = pixn;
          pimunu.piyy = piyy;
          pimunu.piyn = piyn;
          pimunu.pinn = pinn;

          //FIX DUMMY ARGUMENT
          pimunu.boost_to_lrf(Xt, Xx, Xy, Xn, Yx, Yy, Zt, Zn, tau2);
          //double pi_max = pimunu.compute_max();

          // sample LRF Momentum with Scott Pratt's Trick - See LongGang's Sampler Notes
          // perhap divide this into sample_momentum_from_distribution_x()
          lrf_momentum p_LRF = Sample_Momentum_deltaf(mass, T, alphaB, pimunu, bulkPi, E, P, tau2,
                                              sign, dsigmaLRF, dsigma_magnitude, INCLUDE_SHEAR_DELTAF, INCLUDE_BULK_DELTAF, INCLUDE_BARYONDIFF_DELTAF, DF_MODE);

          double px_LRF = p_LRF.x;
          double py_LRF = p_LRF.y;
          double pz_LRF = p_LRF.z;
          double E_LRF = sqrt(mass2 + px_LRF * px_LRF + py_LRF * py_LRF + pz_LRF * pz_LRF);

          // lab frame milne p^mu
          double ptau = E_LRF * ut + px_LRF * Xt + pz_LRF * Zt;
          double px = E_LRF * ux + px_LRF * Xx + py_LRF * Yx;
          double py = E_LRF * uy + px_LRF * Xy + py_LRF * Yy;
          double pn = E_LRF * un + px_LRF * Xn + pz_LRF * Zn;

          // lab frame cartesian p^mu
          double E = (ptau * cosheta) + (tau * pn * sinheta);
          double pz = (tau * pn * cosheta) + (ptau * sinheta);

          // keep p.dsigma > 0 and throw out p.dsigma < 0 sampled particles

          // milne coordinate formula
          // for case of boost invariance: delta_eta_weight can be factored out
          // double pdotdsigma = delta_eta_weight * (ptau * dat + px * dax + py * pday + pn * dan);
          double pdotdsigma = ptau * dat + px * dax + py * day + pn * dan;

          // add sampled particle to particle_list
          if (pdotdsigma >= 0.0)
          {
            // a new particle
            Sampled_Particle new_particle;
            new_particle.mcID = mcid;
            new_particle.tau = tau;
            new_particle.x = x;
            new_particle.y = y;
            new_particle.eta = eta;

            new_particle.t = tau * cosh(eta);
            new_particle.z = tau * sinh(eta);

            new_particle.mass = mass;
            new_particle.px = px;
            new_particle.py = py;
            new_particle.pz = pz;

            //new_particle.E = E; //is it possible for this to be too small or negative?
            //try enforcing mass shell condition on E
            new_particle.E = sqrtf(mass*mass + px*px + py*py + pz*pz);

            //add to particle list
            //CAREFUL push_back is not a thread-safe operation
            //how should we modify for GPU version?
            #pragma omp critical
            particle_list.push_back(new_particle);
          } // if(pdotsigma >= 0)
        } // for (int n = 0; n < N_hadrons; n++)
      } // for(int ieta = 0; ieta < eta_ptsl ieta++)
  } // for (int icell = 0; icell < FO_length; icell++)
}


void EmissionFunctionArray::sample_dN_pTdpTdphidy_feqmod(double *Mass, double *Sign, double *Degeneracy, double *Baryon, int *MCID,
  double *T_fo, double *P_fo, double *E_fo, double *tau_fo, double *x_fo, double *y_fo, double *eta_fo, double *ut_fo, double *ux_fo, double *uy_fo, double *un_fo,
  double *dat_fo, double *dax_fo, double *day_fo, double *dan_fo,
  double *pitt_fo, double *pitx_fo, double *pity_fo, double *pitn_fo, double *pixx_fo, double *pixy_fo, double *pixn_fo, double *piyy_fo, double *piyn_fo, double *pinn_fo, double *bulkPi_fo,
  double *muB_fo, double *nB_fo, double *Vt_fo, double *Vx_fo, double *Vy_fo, double *Vn_fo, double *df_coeff,
  int pbar_pts, double *pbar_root1, double *pbar_weight1, double *pbar_root2, double *pbar_weight2)
  {
    int npart = number_of_chosen_particles;
    double two_pi2_hbarC3 = 2.0 * pow(M_PI,2) * pow(hbarC,3);

    int eta_pts = 1;
    if(DIMENSION == 2)
    {
      eta_pts = eta_tab_length;
    }
    double etaValues[eta_pts];
    double etaDeltaWeights[eta_pts];                               // eta_weight * delta_eta
    double delta_eta = (eta_tab->get(1,2)) - (eta_tab->get(1,1));  // assume uniform grid

    if(DIMENSION == 2)
    {
      for(int ieta = 0; ieta < eta_pts; ieta++)
      {
        etaValues[ieta] = eta_tab->get(1, ieta + 1);
        etaDeltaWeights[ieta] = (eta_tab->get(2, ieta + 1)) * delta_eta;
      }
    }
    else if(DIMENSION == 3)
    {
      etaValues[0] = 0.0;       // placeholder for eta_fo below
      etaDeltaWeights[0] = 1.0; // default 1.0 for 3+1d
    }

    // set df coefficients:
    double F = df_coeff[0];
    double G = df_coeff[1];
    double betabulk = df_coeff[2];
    double betaV = df_coeff[3];
    double betapi = df_coeff[4];

    // loop over all freezeout cells (icell)
    #pragma omp parallel for
    for(int icell = 0; icell < FO_length; icell++)
    {
      // FO cell coordinates:
      double tau = tau_fo[icell];         // longitudinal proper time
      double x = x_fo[icell];             // x coordinate
      double y = y_fo[icell];             // y coordinate
      double tau2 = tau * tau;

      if(DIMENSION == 3)
      {
        etaValues[0] = eta_fo[icell];     // spacetime rapidity from surface file
      }

      double dat = dat_fo[icell];         // covariant normal surface vector dsigma_mu
      double dax = dax_fo[icell];
      double day = day_fo[icell];
      double dan = dan_fo[icell];

      double ux = ux_fo[icell];           // fluid velocity u^mu
      double uy = uy_fo[icell];           // enforce normalization
      double un = un_fo[icell];           // u^eta
      double ut = sqrt(fabs(1.0  +  ux * ux  +  uy * uy  +  tau2 * un * un)); //u^tau

      double ut2 = ut * ut;               // useful expressions
      double ux2 = ux * ux;
      double uy2 = uy * uy;
      double uperp =  sqrt(ux2 + uy2);
      double utperp = sqrt(1.0 + uperp * uperp);

      // set milne basis vectors
      Milne_Basis_Vectors basis_vectors(ut, ux, uy, un, uperp, utperp, tau);

      double T = T_fo[icell];             // temperature
      double E = E_fo[icell];             // energy density
      double P = P_fo[icell];             // pressure

      double T3 = T * T * T;              // useful expressions
      double T4 = T3 * T; 

      double pitt = 0.0;                  // shear stress tensor pi^munu
      double pitx = 0.0;                  // enforce orthogonality and tracelessness
      double pity = 0.0;
      double pitn = 0.0;
      double pixx = 0.0;
      double pixy = 0.0;
      double pixn = 0.0;
      double piyy = 0.0;
      double piyn = 0.0;
      double pinn = 0.0;

      if(INCLUDE_SHEAR_DELTAF)
      {
        double pixx = pixx_fo[icell];
        double pixy = pixy_fo[icell];
        double pixn = pixn_fo[icell];
        double piyy = piyy_fo[icell];
        double piyn = piyn_fo[icell];
        double pinn = (pixx*(ux2 - ut2) + piyy*(uy2 - ut2) + 2.0*(pixy*ux*uy + tau2*un*(pixn*ux + piyn*uy))) / (tau2 * utperp * utperp);
        double pitn = (pixn*ux + piyn*uy + tau2*pinn*un) / ut;
        double pity = (pixy*ux + piyy*uy + tau2*piyn*un) / ut;
        double pitx = (pixx*ux + pixy*uy + tau2*pixn*un) / ut;
        double pitt = (pitx*ux + pity*uy + tau2*pitn*un) / ut;
      }

      double bulkPi = 0.0;

      if(INCLUDE_BULK_DELTAF)
      {
        bulkPi = bulkPi_fo[icell];        // bulk pressure
      }

      double muB = 0.0;                   // baryon chemical potential
      double alphaB = 0.0;                // muB / T
      double nB = 0.0;                    // net baryon density
      double Vt = 0.0;                    // net baryon diffusion current V^mu
      double Vx = 0.0;
      double Vy = 0.0;
      double Vn = 0.0;
      double baryon_enthalpy_ratio = 0.0; // nB / (E + P)

      if(INCLUDE_BARYON)
      {
        muB = muB_fo[icell];
        alphaB = muB / T;

        if(INCLUDE_BARYONDIFF_DELTAF)
        {
          nB = nB_fo[icell];
          Vx = Vx_fo[icell];
          Vy = Vy_fo[icell];
          Vn = Vn_fo[icell];
          Vt = (Vx * ux  +  Vy * uy  +  tau2 * Vn * un) / ut;
          baryon_enthalpy_ratio = nB / (E + P);
        }
      }

      // set modified coefficients, temperature, chemical potential
      double shear_coeff = 0.5 / betapi;
      double bulk_coeff = bulkPi / betabulk / 3.0;
      double diff_coeff = T / betaV;

      double T_mod = T + (F * bulkPi / betabulk);
      double alphaB_mod =  alphaB + (G * bulkPi / betabulk);

      // dsigma class (delta_eta_weight factored out of 2+1d volume element since it drops out in rideal)
      dsigma_Vector dsigma(dat, dax, day, dan);
      dsigma.boost_dsigma_to_lrf(basis_vectors, ut, ux, uy, un);
      dsigma.compute_dsigma_max();

      // shear stress class
      Shear_Stress_Tensor pimunu(pitt, pitx, pity, pitn, pixx, pixy, pixn, piyy, piyn, pinn);
      pimunu.boost_shear_stress_to_lrf(basis_vectors, tau2);

      // baryon diffusion class
      Baryon_Diffusion_Current Vmu(Vt, Vx, Vy, Vn);
      Vmu.boost_baryon_diffusion_to_lrf(basis_vectors, tau2);

      // udotdsigma and Vdotdsigma w/o delta_eta_weight factor
      double udsigma = ut * dat  +  ux * dax  +  uy * day  +  un * dan;
      double Vdsigma = Vt * dat  +  Vx * dax  +  Vy * day  +  Vn * dan;


      // loop over eta points
      for(int ieta = 0; ieta < eta_pts; ieta++)
      {
        double delta_eta_weight = etaDeltaWeights[ieta];
        double udotdsigma = delta_eta_weight * udsigma;

        if(udotdsigma <= 0.0)
        {
          break;                                  // skip over cells with u.dsigma < 0
        }

        double eta = etaValues[ieta];
        double cosheta = cosh(eta);
        double sinheta = sinh(eta);
        double Vdotdsigma = delta_eta_weight * Vdsigma;

        std::vector<double> density_list;         // holds mean number of each species in FO cell
        density_list.resize(npart);
        double dN_tot = 0.0;                      // total mean number of hadrons in FO cell

        // loop over particles
        for(int ipart = 0; ipart < npart; ipart++)
        {
          // particle's properties
          double mass = Mass[ipart];               // mass in GeV
          double mbar = mass / T;                  // mass / temperature
          double sign = Sign[ipart];               // quantum statistics sign
          double degeneracy = Degeneracy[ipart];   // spin degeneracy
          double baryon = Baryon[ipart];           // baryon number
          double chem = baryon * alphaB;           // baryon chemical potential term in feq

          // thermal particles
          //:::::::::::::::::::::::::::::::::::
          int jmax = 2;                            // truncation term of Bose-Fermi expansion = jmax - 1
          if(mbar < 2.0) jmax = 10;                // light particles need more terms than the leading order term

          double sign_factor = -sign;

          double neq = 0.0;                        // equilibrium particle density

          for(int j = 1; j < jmax; j++)            // sum truncated expansion of Bose-Fermi distribution
          {
            double k = (double)j;
            sign_factor *= (-sign);
            neq += sign_factor * exp(k * chem) * gsl_sf_bessel_Kn(2, k * mbar) / k;
          }

          neq *= degeneracy * mass * mass * T / two_pi2_hbarC3;

          double dN_thermal = udotdsigma * neq;   // mean number of particles of type ipart in FO cell
          //:::::::::::::::::::::::::::::::::::


          // bulk pressure correction 
          //:::::::::::::::::::::::::::::::::::
          double dN_bulk = 0.0;                   // dN_bulk mod same as df b/c of renorm factor Z)

          if(INCLUDE_BULK_DELTAF)
          {
            double J10_fact = T3 / two_pi2_hbarC3;
            double J20_fact = T4 / two_pi2_hbarC3;
            double J10 = degeneracy * J10_fact * GaussThermal(J10_int, pbar_root1, pbar_weight1, pbar_pts, mbar, alphaB, baryon, sign);
            double J20 = degeneracy * J20_fact * GaussThermal(J20_int, pbar_root2, pbar_weight2, pbar_pts, mbar, alphaB, baryon, sign);
            dN_bulk = udotdsigma * (bulkPi / betabulk) * (neq + (baryon * J10 * G) + (J20 * F / T / T));
          }
          //:::::::::::::::::::::::::::::::::::


          // baryon diffusion correction 
          //:::::::::::::::::::::::::::::::::::
          double dN_diff = 0.0;                   // assumes mod (not baryon) diffusion current of each species ~ same as df one)

          if(INCLUDE_BARYONDIFF_DELTAF)
          {
            double J11_fact = T3 / two_pi2_hbarC3 / 3.0;
            double J11 = degeneracy * J11_fact * GaussThermal(J11_int, pbar_root1, pbar_weight1, pbar_pts, mbar, alphaB, baryon, sign);
            dN_diff = - (Vdotdsigma / betaV) * (neq * T * baryon_enthalpy_ratio - baryon * J11);
          }
          //:::::::::::::::::::::::::::::::::::


          double dN = dN_thermal + dN_bulk + dN_diff;    // mean number of particles of species ipart
          density_list[ipart] = dN;                      // store in density list to sample individual species later
          dN_tot += dN;                                  // add to total mean number of hadrons

        } // loop over particles (ipart)


        // SAMPLE PARTICLES:
        // random number generators (could I move the random_device generators outside the loops?) 
        std::random_device gen1;
        std::random_device gen2;

        // probability distributions for number of hadrons and particle type
        std::poisson_distribution<int> poisson_hadrons(dN_tot);
        std::discrete_distribution<int> discrete_particle_type(density_list.begin(), density_list.end());

        int N_hadrons = poisson_hadrons(gen1);              // sample total number of hadrons in FO cell

        // sample momentum of N_hadrons
        for(int n = 0; n < N_hadrons; n++)
        {
          // sample particle type with discrete distribution (weights = dN_ipart / dN_tot)
          //:::::::::::::::::::::::::::::::::::
          int idx_sampled = discrete_particle_type(gen2);   // chosen index of sampled particle type

          double mass = Mass[idx_sampled];                  // mass of sampled particle in GeV
          double baryon = Baryon[idx_sampled];              // baryon number
          int mcid = MCID[idx_sampled];                     // MC ID of sampled particle
          //:::::::::::::::::::::::::::::::::::


          // sample particle's momentum from modified equilibrium distribution
          lrf_momentum pLRF = Sample_Momentum_mod(mass, baryon, T_mod, alphaB_mod, dsigma, pimunu, Vmu, shear_coeff, bulk_coeff, diff_coeff, baryon_enthalpy_ratio);
          
          // lab frame momentum p^mu (milne components)
          Lab_Momentum pmu(pLRF);
          pmu.boost_pLRF_to_lab_frame(basis_vectors, ut, ux, uy, un); 

          // pdsigma has delta_eta_weight factored out (irrelevant for sign)
          double pdsigma = pmu.ptau * dat  +  pmu.px * dax  +  pmu.py * day  +  pmu.pn * dan;

          // only keep particles with p.dsigma >= 0
          if(pdsigma >= 0.0)
          {
            // add sampled particle to particle_list
            Sampled_Particle new_particle;
            new_particle.mcID = mcid;
            new_particle.tau = tau;
            new_particle.x = x;
            new_particle.y = y;
            new_particle.eta = eta;
            new_particle.E = pmu.ptau * cosheta  +  tau * pmu.pn * sinheta;
            new_particle.px = pmu.px;
            new_particle.py = pmu.py;
            new_particle.pz = tau * pmu.pn * cosheta  +  pmu.ptau * sinheta;

            //add to particle list
            //CAREFUL push_back is not a thread safe operation
            //how should we modify for gpu version ?
            #pragma omp critical
            particle_list.push_back(new_particle);
          } // keep particle


        } // for (int n = 0; n < N_hadrons; n++)
      } // for(int ieta = 0; ieta < eta_ptsl ieta++)
  } // for (int icell = 0; icell < FO_length; icell++)
}

void EmissionFunctionArray::sample_dN_pTdpTdphidy_VAH_PL(double *Mass, double *Sign, double *Degeneracy,
  double *tau_fo, double *eta_fo, double *ux_fo, double *uy_fo, double *un_fo,
  double *dat_fo, double *dax_fo, double *day_fo, double *dan_fo, double *T_fo,
  double *pitt_fo, double *pitx_fo, double *pity_fo, double *pitn_fo, double *pixx_fo, double *pixy_fo, double *pixn_fo, double *piyy_fo, double *piyn_fo, double *pinn_fo,
  double *bulkPi_fo, double *Wx_fo, double *Wy_fo, double *Lambda_fo, double *aL_fo, double *c0_fo, double *c1_fo, double *c2_fo, double *c3_fo, double *c4_fo)
{
  printf("Sampling particles from VAH \n");
  printf("NOTHING HERE YET \n");
}
