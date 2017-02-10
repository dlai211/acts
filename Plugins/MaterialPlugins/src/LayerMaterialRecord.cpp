// This file is part of the ACTS project.
//
// Copyright (C) 2016 ACTS project team
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

///////////////////////////////////////////////////////////////////
// LayerMaterialRecord.cpp, ACTS project
///////////////////////////////////////////////////////////////////

#include "ACTS/Plugins/MaterialPlugins/LayerMaterialRecord.hpp"

Acts::LayerMaterialRecord::LayerMaterialRecord()
  : m_binUtility(nullptr), m_materialMatrix(), m_matStepsAndAssignedPos()
{
}

Acts::LayerMaterialRecord::LayerMaterialRecord(const BinUtility* binutility)
  : m_binUtility(binutility), m_materialMatrix(), m_matStepsAndAssignedPos()
{
  // reserve
  m_materialMatrix.reserve(m_binUtility->max(1) + 1);
  for (unsigned int ibin1 = 0; ibin1 < (unsigned int)m_binUtility->max(1) + 1;
       ++ibin1) {
    // create the vector for the push_back
    Acts::MaterialPropertiesVector matVec;
    matVec.reserve(m_binUtility->max(0) + 1);
    for (unsigned int ibin = 0; ibin < (unsigned int)m_binUtility->max(0) + 1;
         ++ibin)
      matVec.push_back(new MaterialProperties(0., 0., 0., 0., 0., 0., 0., 0));
    m_materialMatrix.push_back(matVec);
  }
}

Acts::LayerMaterialRecord::LayerMaterialRecord(
    const LayerMaterialRecord& lmrecord)
  : m_binUtility(lmrecord.m_binUtility)
  , m_materialMatrix(lmrecord.m_materialMatrix)
  , m_matStepsAndAssignedPos(lmrecord.m_matStepsAndAssignedPos)
{
}

Acts::LayerMaterialRecord*
Acts::LayerMaterialRecord::clone() const
{
  return (new LayerMaterialRecord(*this));
}

Acts::LayerMaterialRecord&
Acts::LayerMaterialRecord::operator=(const LayerMaterialRecord& lmrecord)
{
  if (this != &lmrecord) {
    m_binUtility     = lmrecord.m_binUtility;
    m_materialMatrix = lmrecord.m_materialMatrix;
    m_matStepsAndAssignedPos.clear();
  }
  return (*this);
}

void
Acts::LayerMaterialRecord::addLayerMaterialProperties(
    const Acts::Vector3D&                  pos,
    const std::vector<Acts::MaterialStep>& layerMaterialSteps)
{
  // sum up all material at this point for this layer
  float newThickness = 0.;
  float newRho       = 0.;
  float newX0        = 0.;
  float newL0        = 0.;
  float newA         = 0.;
  float newZ         = 0.;

  for (auto& layerStep : layerMaterialSteps) {
    float t       = layerStep.material().thickness();
    float density = layerStep.material().averageRho();
    newThickness += t;
    newRho += density * t;
    newX0 += layerStep.material().x0() * t;
    newL0 += layerStep.material().l0() * t;
    newA += layerStep.material().averageA() * density * t;
    newZ += layerStep.material().averageZ() * density * t;
  }

  if (newRho != 0.) {
    newA /= newRho;
    newZ /= newRho;
  }
  if (newThickness != 0.) {
    newX0 /= newThickness;
    newL0 /= newThickness;
    newRho /= newThickness;
  }

  // now add it at the corresponding assigned position
  // get the bins corresponding to the position
  size_t bin0 = m_binUtility->bin(pos, 0);
  size_t bin1 = m_binUtility->bin(pos, 1);
  // get the material which might be there already, add new material and
  // weigh it
  const Acts::MaterialProperties* material = m_materialMatrix.at(bin2).at(bin1);
  float                           thickness = 0.;
  float                           rho       = 0.;
  float                           x0        = 0.;
  float                           l0        = 0.;
  float                           A         = 0.;
  float                           Z         = 0.;
  // access the old material properties
  if (material) {
    thickness += material->thickness();
    rho += material->averageRho();
    x0 += material->x0();
    l0 += material->l0();
    A += material->averageA();
    Z += material->averageZ();
  }
  // add the new material properties and weigh them
  thickness += newThickness;
  rho += newRho * newThickness;
  x0 += newX0 * newThickness;
  l0 += newL0 * newThickness;
  A += newA * newRho * newThickness;
  Z += newZ * newRho * newThickness;
  // set the new current material (not averaged yet)
  const Acts::Material updatedMaterial(x0, l0, A, Z, rho);
  // pick the number of entries for the next material entry
  size_t entries = m_materialMatrix.at(bin2).at(bin1)->entries();
  // set the material with the right number of entries
  m_materialMatrix.at(bin2).at(bin1)->setMaterial(
      updatedMaterial, thickness, entries);
  // increase the number of entries for this material
  m_materialMatrix.at(bin2).at(bin1)->addEntry();
}

void
Acts::LayerMaterialRecord::averageMaterial()
{
  // access the bins
  size_t bins0 = m_binUtility->bins(0);
  size_t bins1 = m_binUtility->bins(1);
  // loop through the material properties matrix and average
  for (size_t bin0 = 0; bin0 < bins0; bin0++) {
    for (size_t bin1 = 0; bin1 < bins1; bin1++) {
      const Acts::MaterialProperties* material
          = m_materialMatrix.at(bin1).at(bin0);

      float thickness = material->thickness();
      float rho       = material->averageRho();
      float x0        = material->x0();
      float l0        = material->l0();
      float A         = material->averageA();
      float Z         = material->averageZ();
      // divide
      if (x0 != 0.) x0 /= thickness;
      if (l0 != 0.) l0 /= thickness;
      if (A != 0.) A /= rho;
      if (Z != 0.) Z /= rho;
      if (rho != 0.) rho /= thickness;
      if (thickness != 0. && material->entries() != 0)
        thickness /= material->entries();
      // set the new current material
      const Acts::Material updatedMaterial(x0, l0, A, Z, rho);
      m_materialMatrix.at(bin1).at(bin0)->setMaterial(updatedMaterial,
                                                      thickness);
    }  // b2
  }    // b1
}

std::shared_ptr<const Acts::BinnedSurfaceMaterial>
Acts::LayerMaterialRecord::layerMaterial() const
{
  // return the binned surface material
  return (std::make_shared<const Acts::BinnedSurfaceMaterial>(
      *m_binUtility, m_materialMatrix));
}
