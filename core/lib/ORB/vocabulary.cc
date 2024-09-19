#include "ORB/vocabulary.h"

#include <cstring>
#include <sstream>

using namespace std;
using namespace DBoW2;

typedef DBoW2::FORB F;

std::string lower_case(std::string str) {
  std::transform(str.begin(), str.end(), str.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return str;
}

namespace ORB_SLAM3 {

FileType ORBVocabulary::inferFileType(const string &filename) {
  auto const suffix =
      lower_case(filename.substr(filename.find_last_of('.') + 1));
  // Binary suffix: bin, obj, db, ""
  if (suffix == "bin" || suffix == "obj" || suffix == "db" || suffix == "")
    return FileType::BINARY;
  if (suffix == "txt" || suffix == "csv")
    return FileType::TEXT;
  // Markup suffix: xml, yaml, json
  if (suffix == "xml" || suffix == "yaml" || suffix == "json")
    return FileType::MARKUP;
  // Unknown suffix
  return FileType::UNKNOWN;
}

ORBVocabulary::ORBVocabulary(int k, int L, WeightingType weighting,
                             ScoringType scoring)
    : TplVoc(k, L, weighting, scoring) {}

ORBVocabulary::ORBVocabulary(const TplVoc &voc) : TplVoc(voc) {}

ORBVocabulary::ORBVocabulary(const std::string &filename, FileType type)
    : TplVoc() {
  load(filename, type);
}

bool ORBVocabulary::load(const string &filename, FileType type) {
  if (type == FileType::UNKNOWN)
    type = inferFileType(filename);
  switch (type) {
  case FileType::BINARY:
    return loadFromBinary(filename);
  case FileType::TEXT:
    return loadFromText(filename);
  case FileType::MARKUP:
    try {
      TplVoc::load(filename);
      return true;
    } catch (const std::exception &e) {
      std::cerr << "Error loading " << filename << ": " << e.what()
                << std::endl;
      return false;
    } catch (...) {
      std::cerr << "Unknown exception loading " << filename << std::endl;
      return false;
    }
  default:
    std::cerr << "Unknown file type: " << filename << std::endl;
    return false;
  }
}

bool ORBVocabulary::save(const string &filename, FileType type) {
  if (type == FileType::UNKNOWN)
    type = inferFileType(filename);
  switch (type) {
  case FileType::BINARY:
    return saveAsBinary(filename);
  case FileType::TEXT:
    return saveAsText(filename);
  case FileType::MARKUP:
    try {
      TplVoc::save(filename);
      return true;
    } catch (const std::exception &e) {
      std::cerr << "Error loading " << filename << ": " << e.what()
                << std::endl;
      return false;
    } catch (...) {
      std::cerr << "Unknown exception saving " << filename << std::endl;
      return false;
    }
  default:
    std::cerr << "Unknown file type: " << filename << std::endl;
    return false;
  }
}

bool ORBVocabulary::loadFromText(const std::string &filename) {
  ifstream f;
  f.open(filename.c_str());
  if (f.eof())
    return false;
  m_words.clear();
  m_nodes.clear();
  string s;
  getline(f, s);
  stringstream ss;
  ss << s;
  ss >> m_k;
  ss >> m_L;
  int n1, n2;
  ss >> n1;
  ss >> n2;
  if (m_k < 0 || m_k > 20 || m_L < 1 || m_L > 10 || n1 < 0 || n1 > 5 ||
      n2 < 0 || n2 > 3) {
    std::cerr << "Vocabulary loading failure: This is not a correct text file!"
              << endl;
    return false;
  }
  m_scoring = (ScoringType)n1;
  m_weighting = (WeightingType)n2;
  createScoringObject();
  // nodes
  int expected_nodes =
      (int)((pow((double)m_k, (double)m_L + 1) - 1) / (m_k - 1));
  m_nodes.reserve(expected_nodes);
  m_words.reserve(pow((double)m_k, (double)m_L + 1));
  m_nodes.resize(1);
  m_nodes[0].id = 0;
  while (!f.eof()) {
    string snode;
    getline(f, snode);
    stringstream ssnode;
    ssnode << snode;
    int nid = m_nodes.size();
    m_nodes.resize(m_nodes.size() + 1);
    m_nodes[nid].id = nid;
    int pid;
    ssnode >> pid;
    m_nodes[nid].parent = pid;
    m_nodes[pid].children.push_back(nid);
    int nIsLeaf;
    ssnode >> nIsLeaf;
    stringstream ssd;
    for (int iD = 0; iD < F::L; iD++) {
      string sElement;
      ssnode >> sElement;
      ssd << sElement << " ";
    }
    F::fromString(m_nodes[nid].descriptor, ssd.str());
    ssnode >> m_nodes[nid].weight;
    if (nIsLeaf > 0) {
      int wid = m_words.size();
      m_words.resize(wid + 1);
      m_nodes[nid].word_id = wid;
      m_words[wid] = &m_nodes[nid];
    } else {
      m_nodes[nid].children.reserve(m_k);
    }
  }
  return true;
};

bool ORBVocabulary::saveAsText(const std::string &filename) {
  fstream f;
  f.open(filename.c_str(), ios_base::out);
  f << m_k << " " << m_L << " " << " " << m_scoring << " " << m_weighting
    << endl;
  for (size_t i = 1; i < m_nodes.size(); i++) {
    const Node &node = m_nodes[i];
    f << node.parent << " ";
    if (node.isLeaf())
      f << 1 << " ";
    else
      f << 0 << " ";
    f << F::toString(node.descriptor) << " " << (double)node.weight << endl;
  }
  f.close();
  return true;
};
// --------------------------------------------------------------------------
bool ORBVocabulary::loadFromBinary(const string &filename) {
  m_words.clear();
  m_nodes.clear();
  ifstream f(filename);
  BinHeader header;
  f.read((char *)&header, sizeof(header));
  if (header.k < 0 || header.k > 20 || header.L < 1 || header.L > 10 ||
      header.s < 0 || header.s > 5 || header.w < 0 || header.w > 3) {
    std::cerr
        << "Vocabulary loading failure: This is not a correct Binary file!"
        << endl;
    return false;
  }
  m_k = header.k;
  m_L = header.L;
  m_scoring = header.s;
  m_weighting = header.w;
  createScoringObject();
  // nodes
  int expected_nodes =
      (int)((pow((double)m_k, (double)m_L + 1) - 1) / (m_k - 1));
  m_nodes.resize(expected_nodes);
  m_words.reserve(pow((double)m_k, (double)m_L + 1));
  m_nodes.resize(1);
  m_nodes[0].id = 0;
  BinNode node;
  size_t i = 1, j = 0;
  while ((!f.eof()) && (i < m_nodes.size())) {
    auto &n = m_nodes[i];
    n.id = i;
    f.read((char *)&node, sizeof(node));
    n.parent = node.parent;
    m_nodes[n.parent].children.push_back(i);
    n.descriptor.create(1, F::L, CV_8U);
    std::memcpy(n.descriptor.ptr(), node.descriptor, sizeof(node.descriptor));
    n.weight = node.weight;
    if (node.is_leaf) {
      int wid = m_words.size();
      m_words.resize(wid + 1);
      n.word_id = wid;
      m_words[wid] = &n;
    } else {
      n.children.reserve(m_k);
    }
  }
  return true;
}
// --------------------------------------------------------------------------
bool ORBVocabulary::saveAsBinary(const std::string &filename) {
  fstream f(filename, ios_base::out);
  BinHeader header = {m_k, m_L, m_scoring, m_weighting};
  f.write((const char *)&header, sizeof(header));
  for (auto const &n : m_nodes) {
    BinNode node;
    node.parent = n.parent;
    node.is_leaf = n.isLeaf() ? 1 : 0;
    std::memcpy(node.descriptor, n.descriptor.ptr(), sizeof(node.descriptor));
    node.weight = n.weight;
    f.write((const char *)&node, sizeof(node));
  }
  return true;
}

} // namespace ORB_SLAM3