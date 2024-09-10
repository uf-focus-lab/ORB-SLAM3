/**
 * This file is part of ORB-SLAM3
 *
 * Copyright (C) 2017-2021 Carlos Campos, Richard Elvira, Juan J. Gómez
 * Rodríguez, José M.M. Montiel and Juan D. Tardós, University of Zaragoza.
 * Copyright (C) 2014-2016 Raúl Mur-Artal, José M.M. Montiel and Juan D. Tardós,
 * University of Zaragoza.
 *
 * ORB-SLAM3 is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * ORB-SLAM3 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ORB-SLAM3. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef ORBVOCABULARY_H
#define ORBVOCABULARY_H

#include <DBoW2/FORB.h>
#include <DBoW2/TemplatedVocabulary.h>
namespace ORB_SLAM3 {

using namespace DBoW2;

typedef TemplatedVocabulary<FORB::TDescriptor, FORB> TplVoc;

class ORBVocabulary : public TplVoc {
public:
  ORBVocabulary(int k = 10, int L = 5, WeightingType weighting = TF_IDF,
                ScoringType scoring = L1_NORM)
      : TplVoc(k, L, weighting, scoring) {}
  ORBVocabulary(const std::string &filename) : TplVoc(filename){};
  ORBVocabulary(const char *filename) : TplVoc(filename){};
  ORBVocabulary(const TplVoc &voc) : TplVoc(voc){};
  // Extended functions
  bool loadFromTextFile(const std::string &filename);
  bool saveToTextFile(const std::string &filename);
};

} // namespace ORB_SLAM3

#endif // ORBVOCABULARY_H
