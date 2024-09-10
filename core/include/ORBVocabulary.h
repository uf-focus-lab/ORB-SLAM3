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

using namespace std;
using namespace DBoW2;

typedef TemplatedVocabulary<FORB::TDescriptor, FORB> TplVoc;

typedef enum FileType {
  UNKNOWN = 0b00,
  BINARY = 0b01,
  TEXT = 0b10,
  MARKUP = 0b11
} FileType;

class ORBVocabulary : public TplVoc {
  // Binary Interfaces
  typedef struct {
    decltype(ORBVocabulary::m_k) k;
    decltype(ORBVocabulary::m_L) L;
    decltype(ORBVocabulary::m_scoring) s;
    decltype(ORBVocabulary::m_weighting) w;
  } BinHeader;
  typedef struct {
    decltype(Node::parent) parent;
    unsigned char is_leaf;
    unsigned char descriptor[FORB::L];
    decltype(Node::weight) weight;
  } BinNode;
  // Extended functions
  bool loadFromText(const string &filename);
  bool saveAsText(const string &filename);
  bool loadFromBinary(const string &filename);
  bool saveAsBinary(const string &filename);
  FileType inferFileType(const string &filename);
public:
  ORBVocabulary(int k = 10, int L = 5, WeightingType weighting = TF_IDF,
                ScoringType scoring = L1_NORM);
  ORBVocabulary(const TplVoc &voc);
  ORBVocabulary(const string &filename, FileType type = FileType::UNKNOWN);
  bool load(const string &filename, FileType type = FileType::UNKNOWN);
  bool save(const string &filename, FileType type = FileType::UNKNOWN);
};

} // namespace ORB_SLAM3

#endif // ORBVOCABULARY_H
