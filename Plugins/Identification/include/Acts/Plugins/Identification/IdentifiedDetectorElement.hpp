// This file is part of the Acts project.
//
// Copyright (C) 2018 Acts project team
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

///////////////////////////////////////////////////////////////////
// IdentifiedDetectorElement.hpp, Acts project
///////////////////////////////////////////////////////////////////

#pragma once

#include "Acts/Detector/DetectorElementBase.hpp"
#include "Acts/Plugins/Identification/Identifier.hpp"

namespace Acts {

/// @brief The identified detector element
class IdentifiedDetectorElement : public DetectorElementBase
{
public:
  /// Identifier
  virtual Identifier
  identifier() const = 0;
};

}  // end of namespace Acts