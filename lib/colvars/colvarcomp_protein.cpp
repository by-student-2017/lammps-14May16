/// -*- c++ -*-

#include <cmath>

#include "colvarmodule.h"
#include "colvarvalue.h"
#include "colvarparse.h"
#include "colvar.h"
#include "colvarcomp.h"


//////////////////////////////////////////////////////////////////////
// alpha component
//////////////////////////////////////////////////////////////////////

    // FIXME: this will not make collect_gradients work
    // because gradients in individual atom groups
    // are those of the sub-cvcs (angle, hb), not those
    // of this cvc (alpha)
    // This is true of all cvcs with sub-cvcs, and those
    // that do not calculate explicit gradients
    // SO: we need a flag giving the availability of
    // atomic gradients

colvar::alpha_angles::alpha_angles(std::string const &conf)
  : cvc(conf)
{
  if (cvm::debug())
    cvm::log("Initializing alpha_angles object.\n");

  function_type = "alpha_angles";
  x.type(colvarvalue::type_scalar);

  std::string segment_id;
  get_keyval(conf, "psfSegID", segment_id, std::string("MAIN"));

  std::vector<int> residues;
  {
    std::string residues_conf = "";
    key_lookup(conf, "residueRange", residues_conf);
    if (residues_conf.size()) {
      std::istringstream is(residues_conf);
      int initial, final;
      char dash;
      if ( (is >> initial) && (initial > 0) &&
           (is >> dash) && (dash == '-') &&
           (is >> final) && (final > 0) ) {
        for (int rnum = initial; rnum <= final; rnum++) {
          residues.push_back(rnum);
        }
      }
    } else {
      cvm::fatal_error("Error: no residues defined in \"residueRange\".\n");
    }
  }

  if (residues.size() < 5) {
    cvm::fatal_error("Error: not enough residues defined in \"residueRange\".\n");
  }

  std::string const &sid    = segment_id;
  std::vector<int> const &r = residues;


  get_keyval(conf, "hBondCoeff", hb_coeff, 0.5);
  if ( (hb_coeff < 0.0) || (hb_coeff > 1.0) ) {
    cvm::fatal_error("Error: hBondCoeff must be defined between 0 and 1.\n");
  }


  get_keyval(conf, "angleRef", theta_ref, 88.0);
  get_keyval(conf, "angleTol", theta_tol, 15.0);

  if (hb_coeff < 1.0) {

    for (size_t i = 0; i < residues.size()-2; i++) {
      theta.push_back(new colvar::angle(cvm::atom(r[i  ], "CA", sid),
                                        cvm::atom(r[i+1], "CA", sid),
                                        cvm::atom(r[i+2], "CA", sid)));
      atom_groups.push_back(theta.back()->atom_groups[0]);
      atom_groups.push_back(theta.back()->atom_groups[1]);
      atom_groups.push_back(theta.back()->atom_groups[2]);
    }

  } else {
    cvm::log("The hBondCoeff specified will disable the Calpha-Calpha-Calpha angle terms.\n");
  }

  {
    cvm::real r0;
    size_t en, ed;
    get_keyval(conf, "hBondCutoff",   r0, (3.3 * cvm::unit_angstrom()));
    get_keyval(conf, "hBondExpNumer", en, 6);
    get_keyval(conf, "hBondExpDenom", ed, 8);

    if (hb_coeff > 0.0) {

      for (size_t i = 0; i < residues.size()-4; i++) {
        hb.push_back(new colvar::h_bond(cvm::atom(r[i  ], "O",  sid),
                                        cvm::atom(r[i+4], "N",  sid),
                                        r0, en, ed));
        atom_groups.push_back(hb.back()->atom_groups[0]);
      }

    } else {
      cvm::log("The hBondCoeff specified will disable the hydrogen bond terms.\n");
    }
  }

  if (cvm::debug())
    cvm::log("Done initializing alpha_angles object.\n");
}


colvar::alpha_angles::alpha_angles()
  : cvc()
{
  function_type = "alpha_angles";
  x.type(colvarvalue::type_scalar);
}

colvar::alpha_angles::~alpha_angles()
{
  while (theta.size() != 0) {
    delete theta.back();
    theta.pop_back();
  }
  while (hb.size() != 0) {
    delete hb.back();
    hb.pop_back();
  }
}

void colvar::alpha_angles::calc_value()
{
  x.real_value = 0.0;

  if (theta.size()) {

    cvm::real const theta_norm =
      (1.0-hb_coeff) / cvm::real(theta.size());

    for (size_t i = 0; i < theta.size(); i++) {

      (theta[i])->calc_value();

      cvm::real const t = ((theta[i])->value().real_value-theta_ref)/theta_tol;
      cvm::real const f = ( (1.0 - std::pow(t, (int) 2)) /
                            (1.0 - std::pow(t, (int) 4)) );

      x.real_value += theta_norm * f;

      if (cvm::debug())
        cvm::log("Calpha-Calpha angle no. "+cvm::to_str(i+1)+" in \""+
                  this->name+"\" has a value of "+
                  (cvm::to_str((theta[i])->value().real_value))+
                  " degrees, f = "+cvm::to_str(f)+".\n");
    }
  }

  if (hb.size()) {

    cvm::real const hb_norm =
      hb_coeff / cvm::real(hb.size());

    for (size_t i = 0; i < hb.size(); i++) {
      (hb[i])->calc_value();
      x.real_value += hb_norm * (hb[i])->value().real_value;
      if (cvm::debug())
        cvm::log("Hydrogen bond no. "+cvm::to_str(i+1)+" in \""+
                  this->name+"\" has a value of "+
                  (cvm::to_str((hb[i])->value().real_value))+".\n");
    }
  }
}


void colvar::alpha_angles::calc_gradients()
{
  size_t i;
  for (i = 0; i < theta.size(); i++)
    (theta[i])->calc_gradients();

  for (i = 0; i < hb.size(); i++)
    (hb[i])->calc_gradients();
}


void colvar::alpha_angles::apply_force(colvarvalue const &force)
{

  if (theta.size()) {

    cvm::real const theta_norm =
      (1.0-hb_coeff) / cvm::real(theta.size());

    for (size_t i = 0; i < theta.size(); i++) {

      cvm::real const t = ((theta[i])->value().real_value-theta_ref)/theta_tol;
      cvm::real const f = ( (1.0 - std::pow(t, (int) 2)) /
                            (1.0 - std::pow(t, (int) 4)) );

      cvm::real const dfdt =
        1.0/(1.0 - std::pow(t, (int) 4)) *
        ( (-2.0 * t) + (-1.0*f)*(-4.0 * std::pow(t, (int) 3)) );

      (theta[i])->apply_force(theta_norm *
                               dfdt * (1.0/theta_tol) *
                               force.real_value );
    }
  }

  if (hb.size()) {

    cvm::real const hb_norm =
      hb_coeff / cvm::real(hb.size());

    for (size_t i = 0; i < hb.size(); i++) {
      (hb[i])->apply_force(0.5 * hb_norm * force.real_value);
    }
  }
}


//////////////////////////////////////////////////////////////////////
// dihedral principal component
//////////////////////////////////////////////////////////////////////

    // FIXME: this will not make collect_gradients work
    // because gradients in individual atom groups
    // are those of the sub-cvcs (dihedral), not those
    // of this cvc
    // This is true of all cvcs with sub-cvcs, and those
    // that do not calculate explicit gradients
    // SO: we need a flag giving the availability of
    // atomic gradients

colvar::dihedPC::dihedPC(std::string const &conf)
  : cvc(conf)
{
  if (cvm::debug())
    cvm::log("Initializing dihedral PC object.\n");

  function_type = "dihedPC";
  x.type(colvarvalue::type_scalar);

  std::string segment_id;
  get_keyval(conf, "psfSegID", segment_id, std::string("MAIN"));

  std::vector<int> residues;
  {
    std::string residues_conf = "";
    key_lookup(conf, "residueRange", residues_conf);
    if (residues_conf.size()) {
      std::istringstream is(residues_conf);
      int initial, final;
      char dash;
      if ( (is >> initial) && (initial > 0) &&
           (is >> dash) && (dash == '-') &&
           (is >> final) && (final > 0) ) {
        for (int rnum = initial; rnum <= final; rnum++) {
          residues.push_back(rnum);
        }
      }
    } else {
      cvm::fatal_error("Error: no residues defined in \"residueRange\".\n");
    }
  }

  if (residues.size() < 2) {
    cvm::fatal_error("Error: dihedralPC requires at least two residues.\n");
  }

  std::string const &sid    = segment_id;
  std::vector<int> const &r = residues;

  std::string vecFileName;
  int         vecNumber;
  if (get_keyval(conf, "vectorFile", vecFileName, vecFileName)) {
    get_keyval(conf, "vectorNumber", vecNumber, 0);
    if (vecNumber < 1)
      cvm::fatal_error("A positive value of vectorNumber is required.");

    std::ifstream vecFile;
    vecFile.open(vecFileName.c_str());
    if (!vecFile.good())
      cvm::fatal_error("Error opening dihedral PCA vector file " + vecFileName + " for reading");

    // TODO: adapt to different formats by setting this flag
    bool eigenvectors_as_columns = true;

    if (eigenvectors_as_columns) {
      // Carma-style dPCA file
      std::string line;
      cvm::real c;
      while (vecFile.good()) {
        getline(vecFile, line);
        if (line.length() < 2) break;
        std::istringstream ls(line);
        for (int i=0; i<vecNumber; i++) ls >> c;
        coeffs.push_back(c);
      }
    }
/*  TODO Uncomment this when different formats are recognized
    else {
      // Eigenvectors as lines
      // Skip to the right line
      for (int i = 1; i<vecNumber; i++)
        vecFile.ignore(999999, '\n');

      if (!vecFile.good())
        cvm::fatal_error("Error reading dihedral PCA vector file " + vecFileName);

      std::string line;
      getline(vecFile, line);
      std::istringstream ls(line);
      cvm::real c;
      while (ls.good()) {
        ls >> c;
        coeffs.push_back(c);
      }
    }
 */
    vecFile.close();

  } else {
    get_keyval(conf, "vector", coeffs, coeffs);
  }

  if ( coeffs.size() != 4 * (residues.size() - 1)) {
    cvm::fatal_error("Error: wrong number of coefficients: " +
        cvm::to_str(coeffs.size()) + ". Expected " +
        cvm::to_str(4 * (residues.size() - 1)) +
        " (4 coeffs per residue, minus one residue).\n");
  }

  for (size_t i = 0; i < residues.size()-1; i++) {
    // Psi
    theta.push_back(new colvar::dihedral(cvm::atom(r[i  ], "N", sid),
                                           cvm::atom(r[i  ], "CA", sid),
                                           cvm::atom(r[i  ], "C", sid),
                                           cvm::atom(r[i+1], "N", sid)));
    // Phi (next res)
    theta.push_back(new colvar::dihedral(cvm::atom(r[i  ], "C", sid),
                                           cvm::atom(r[i+1], "N", sid),
                                           cvm::atom(r[i+1], "CA", sid),
                                           cvm::atom(r[i+1], "C", sid)));
  }

  if (cvm::debug())
    cvm::log("Done initializing dihedPC object.\n");
}


colvar::dihedPC::dihedPC()
  : cvc()
{
  function_type = "dihedPC";
  x.type(colvarvalue::type_scalar);
}


colvar::dihedPC::~dihedPC()
{
  while (theta.size() != 0) {
    delete theta.back();
    theta.pop_back();
  }
}


void colvar::dihedPC::calc_value()
{
  x.real_value = 0.0;
  for (size_t i = 0; i < theta.size(); i++) {
    theta[i]->calc_value();
    cvm::real const t = (PI / 180.) * theta[i]->value().real_value;
    x.real_value += coeffs[2*i  ] * std::cos(t)
                  + coeffs[2*i+1] * std::sin(t);
  }
}


void colvar::dihedPC::calc_gradients()
{
  for (size_t i = 0; i < theta.size(); i++) {
    theta[i]->calc_gradients();
  }
}


void colvar::dihedPC::apply_force(colvarvalue const &force)
{
  for (size_t i = 0; i < theta.size(); i++) {
    cvm::real const t = (PI / 180.) * theta[i]->value().real_value;
    cvm::real const dcosdt = - (PI / 180.) * std::sin(t);
    cvm::real const dsindt =   (PI / 180.) * std::cos(t);

    theta[i]->apply_force((coeffs[2*i  ] * dcosdt +
                           coeffs[2*i+1] * dsindt) * force);
  }
}
