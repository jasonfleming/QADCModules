/*------------------------------GPL---------------------------------------//
// This file is part of ADCIRCModules.
//
// (c) 2015-2018 Zachary Cobell
//
// ADCIRCModules is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// ADCIRCModules is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with ADCIRCModules.  If not, see <http://www.gnu.org/licenses/>.
//------------------------------------------------------------------------*/
#include "outputfile.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <utility>
#include "error.h"
#include "io.h"
#include "netcdf.h"
#include "stringconversion.h"

using namespace Adcirc::Output;
using namespace std;

//...netcdf Variable names currently in ADCIRC source code
static const std::vector<string> netcdfVarNames = {"sigmat",
                                                   "salinity",
                                                   "temperature",
                                                   "u-vel3D",
                                                   "v-vel3D",
                                                   "w-vel3D",
                                                   "q20",
                                                   "l",
                                                   "ev",
                                                   "qsurfkp1",
                                                   "zeta",
                                                   "zeta_max",
                                                   "u-vel",
                                                   "v-vel",
                                                   "vel_max",
                                                   "pressure",
                                                   "pressure_min",
                                                   "windx",
                                                   "windy",
                                                   "wind_max",
                                                   "radstress_x",
                                                   "radstress_y",
                                                   "radstress_max",
                                                   "swan_HS",
                                                   "swan_HS_max",
                                                   "swan_DIR",
                                                   "swan_DIR_max",
                                                   "swan_TM01",
                                                   "swan_TM01_max",
                                                   "swan_TPS",
                                                   "swan_TPS_max",
                                                   "swan_windx",
                                                   "swan_windy",
                                                   "swan_wind_max",
                                                   "swan_TM02",
                                                   "swan_TM02_max",
                                                   "swan_TMM10",
                                                   "swan_TMM10_max"};

OutputFile::OutputFile(std::string filename) : m_filename(filename) {
  this->m_currentSnap = 0;
  this->m_numNodes = 0;
  this->m_open = false;
  this->m_isVector = false;
  this->m_isMax = false;
  this->m_filetype = Adcirc::Output::Unknown;
}

OutputFile::~OutputFile() { this->clear(); }

void OutputFile::clear() {
  for (auto& m_record : this->m_records) {
    m_record.reset(nullptr);
  }
  this->m_records.clear();
  this->m_recordMap.clear();
}

void OutputFile::clearAt(size_t position) {
  assert(position < this->m_records.size());
  if (position < this->m_records.size()) {
    this->m_records[position].reset(nullptr);
    this->m_records.erase(this->m_records.begin() + position);
    this->rebuildMap();
  } else {
    Adcirc::Error::throwError("OutputFile: Index exceeds dimension");
  }
}

string OutputFile::filename() const { return this->m_filename; }

void OutputFile::setFilename(const string& filename) {
  if (!this->isOpen()) {
    this->m_filename = filename;
  } else {
    Adcirc::Error::throwError(
        "OutputFile: Cannot change filename since file currently open");
  }
}

bool OutputFile::isOpen() { return this->m_open; }

bool OutputFile::exists() {
  ifstream f(this->m_filename.c_str());
  return f.good();
}

int OutputFile::open() {
  assert(!this->m_open);
  assert(this->exists());

  if (this->isOpen()) {
    Adcirc::Error::throwError("OutputFile: File already open");
  }

  if (!this->exists()) {
    Adcirc::Error::throwError("OutputFile: File does not exist");
  }

  this->m_filetype = this->getFiletype();

  if (this->filetype() == Adcirc::Output::ASCIIFull ||
      this->filetype() == Adcirc::Output::ASCIISparse) {
    this->openAscii();
    this->readAsciiHeader();
  } else if (this->filetype() == Adcirc::Output::Netcdf3 ||
             this->filetype() == Adcirc::Output::Netcdf4) {
    this->openNetcdf();
    this->readNetcdfHeader();
  } else if (this->filetype() == Adcirc::Output::Xdmf) {
    this->openXdmf();
  } else {
    Adcirc::Error::throwError("OutputFile: No valid file type detected");
    return Adcirc::HasError;
  }

  if (this->isOpen()) {
    return Adcirc::NoError;
  } else {
    Adcirc::Error::throwError("OutputFile: Error opening file");
    return Adcirc::HasError;
  }
  return Adcirc::NoError;
}

int OutputFile::close() {
  assert(this->m_open);

  if (!this->isOpen()) {
    Adcirc::Error::throwError("OutputFile: File not open");
    return Adcirc::HasError;
  }

  if (this->filetype() == Adcirc::Output::ASCIIFull ||
      this->filetype() == Adcirc::Output::ASCIISparse) {
    return this->closeAscii();
  }

  if (this->filetype() == Adcirc::Output::Netcdf3 ||
      this->filetype() == Adcirc::Output::Netcdf4) {
    return this->closeNetcdf();
  }

  if (this->filetype() == Adcirc::Output::Xdmf) {
    return this->closeXdmf();
  }

  return Adcirc::NoError;
}

int OutputFile::read(size_t snap) {
  int ierr;
  unique_ptr<OutputRecord> record;

  if (this->m_filetype == Adcirc::Output::ASCIIFull ||
      this->m_filetype == Adcirc::Output::ASCIISparse) {
    if (snap != Adcirc::Output::NextOutputSnap) {
      std::cerr
          << "[ADCIRCModules WARNING]: ASCII Output must be read record by "
             "record. Specified snap number ignored.\n";
    }
    if (this->m_currentSnap > this->m_numSnaps) {
      Adcirc::Error::throwError(
          "OutputFile: Attempt to read past last record in file");
    }
    ierr = this->readAsciiRecord(record);
  } else if (this->m_filetype == Adcirc::Output::Netcdf3 ||
             this->m_filetype == Adcirc::Output::Netcdf4) {
    ierr = this->readNetcdfRecord(snap, record);
  } else {
    Adcirc::Error::throwError("OutputFile: Unknown filetype");
    return Adcirc::HasError;
  }

  if (ierr == Adcirc::NoError) {
    this->m_records.push_back(std::move(record));
  } else {
    if (record.get() != nullptr) {
      record.reset(nullptr);
    }
    Adcirc::Error::throwError("OutputFile: Error reading output record");
    return Adcirc::HasError;
  }

  return Adcirc::NoError;
}

int OutputFile::write(size_t snap) {
  Adcirc::Error::throwError("OutputFile: Not implemented");
  return Adcirc::HasError;
}

int OutputFile::openAscii() {
  assert(!this->isOpen());
  if (this->isOpen()) {
    Adcirc::Error::throwError("OutputFile: File already open");
    return Adcirc::HasError;
  }
  this->m_fid.open(this->filename());
  if (this->m_fid.is_open()) {
    this->m_open = true;
    return Adcirc::NoError;
  } else {
    Adcirc::Error::throwError("OutputFile: File could not be opened");
    return Adcirc::HasError;
  }
}

int OutputFile::openNetcdf() {
  assert(!this->isOpen());
  if (!this->isOpen()) {
    int ierr = nc_open(this->m_filename.c_str(), NC_NOWRITE, &this->m_ncid);
    if (ierr != NC_NOERR) {
      Adcirc::Error::throwError("OutputFile: Error opening netcdf file");
      return Adcirc::HasError;
    }
    this->m_open = true;
  } else {
    Adcirc::Error::throwError("OutputFile: Error opening netcdf file");
    return Adcirc::HasError;
  }
  return Adcirc::NoError;
}

int OutputFile::openXdmf() {
  Adcirc::Error::throwError("OutputFile: Not implemented");
  return Adcirc::HasError;
}

int OutputFile::closeAscii() {
  assert(this->isOpen());
  if (this->isOpen()) {
    this->m_fid.close();
    this->m_open = false;
    return Adcirc::NoError;
  }
  Adcirc::Error::throwError("OutputFile: Error closing ascii file");
  return Adcirc::HasError;
}

int OutputFile::closeNetcdf() {
  assert(this->isOpen());
  if (this->isOpen()) {
    nc_close(this->m_ncid);
    this->m_open = false;
    return Adcirc::NoError;
  }
  Adcirc::Error::throwError("OutputFile: Error closing netcdf file");
  return Adcirc::HasError;
}

int OutputFile::closeXdmf() {
  Adcirc::Error::throwError("OutputFile: Not implemented");
  return Adcirc::HasError;
}

OutputRecord* OutputFile::data(size_t snap) {
  bool ok;
  return this->data(snap, ok);
}

OutputRecord* OutputFile::data(size_t snap, bool& ok) {
  assert(this->m_recordMap.find(snap) != this->m_recordMap.end());

  if (this->m_recordMap.find(snap) == this->m_recordMap.end()) {
    ok = false;
    Adcirc::Error::throwError("OutputFile: Data requested is out of bounds");
    return nullptr;
  } else {
    ok = true;
    return this->m_recordMap[snap];
  }
}

OutputRecord* OutputFile::dataAt(size_t position) {
  bool ok;
  return this->dataAt(position, ok);
}

OutputRecord* OutputFile::dataAt(size_t position, bool& ok) {
  assert(position < this->m_records.size());

  if (position < this->m_records.size()) {
    ok = false;
    Adcirc::Error::throwError("OutputFile: Data requested is out of bounds");
    return nullptr;
  } else {
    ok = true;
    return this->m_records[position].get();
  }
}

size_t OutputFile::getNumSnaps() const { return this->m_numSnaps; }

void OutputFile::setNumSnaps(int numSnaps) { this->m_numSnaps = numSnaps; }

size_t OutputFile::getNumNodes() const { return this->m_numNodes; }

void OutputFile::setNumNodes(size_t numNodes) { this->m_numNodes = numNodes; }

double OutputFile::getDt() const { return this->m_dt; }

void OutputFile::setDt(double dt) { this->m_dt = dt; }

int OutputFile::getDiteration() const { return this->m_dit; }

void OutputFile::setDiteration(int dit) { this->m_dit = dit; }

int OutputFile::filetype() const { return this->m_filetype; }

std::string OutputFile::getHeader() const { return this->m_header; }

void OutputFile::setHeader(const std::string& header) {
  this->m_header = header;
}

int OutputFile::getFiletype() {
  if (OutputFile::checkFiletypeNetcdf3(this->filename())) {
    return Adcirc::Output::Netcdf3;
  }
  if (OutputFile::checkFiletypeNetcdf4(this->filename())) {
    return Adcirc::Output::Netcdf4;
  }
  if (OutputFile::checkFiletypeXdmf(this->filename())) {
    return Adcirc::Output::Xdmf;
  }
  if (OutputFile::checkFiletypeAsciiFull(this->filename())) {
    return Adcirc::Output::ASCIIFull;
  }
  if (OutputFile::checkFiletypeAsciiSparse(this->filename())) {
    return Adcirc::Output::ASCIISparse;
  }
  return Adcirc::Output::Unknown;
}

bool OutputFile::checkFiletypeAsciiSparse(const string& filename) {
  assert(!filename.empty());

  fstream fid(filename);

  try {
    string line;
    std::getline(fid, line);  // header
    std::getline(fid, line);  // header
    std::getline(fid, line);  // first record

    vector<string> list;
    int ierr = IO::splitString(line, list);
    if (ierr != 0) {
      fid.close();
      return false;
    }

    if (list.size() == 4) {
      fid.close();
      return true;
    } else {
      fid.close();
      return false;
    }
  } catch (...) {
    if (fid.is_open()) {
      fid.close();
    }
    return false;
  }
}

bool OutputFile::checkFiletypeAsciiFull(const string& filename) {
  assert(!filename.empty());

  fstream fid(filename);

  try {
    string line;

    getline(fid, line);  // header
    getline(fid, line);  // header
    getline(fid, line);  // first record header

    vector<string> list;
    int ierr = IO::splitString(line, list);
    if (ierr != 0) {
      fid.close();
      return false;
    }

    if (list.size() == 2) {
      fid.close();
      return true;
    } else {
      fid.close();
      return false;
    }
  } catch (...) {
    if (fid.is_open()) {
      fid.close();
    }
    return false;
  }
}

bool OutputFile::inquireNetcdfFormat(const string& filename, int& format) {
  int ncid;
  format = Adcirc::Output::Unknown;
  int ierr = nc_open(filename.c_str(), NC_NOWRITE, &ncid);
  if (ierr != NC_NOERR) {
    return false;
  }
  ierr = nc_inq_format(ncid, &format);
  if (ierr != NC_NOERR) {
    nc_close(ncid);
    return false;
  }
  nc_close(ncid);
  return true;
}

bool OutputFile::checkFiletypeNetcdf3(string filename) {
  int format;
  bool b = OutputFile::inquireNetcdfFormat(std::move(filename), format);
  if (b && format == NC_FORMAT_CLASSIC) {
    return true;
  }
  return false;
}

bool OutputFile::checkFiletypeNetcdf4(string filename) {
  int format;
  bool b = OutputFile::inquireNetcdfFormat(std::move(filename), format);
  if (b) {
    if (format == NC_FORMAT_NETCDF4_CLASSIC || format == NC_FORMAT_NETCDF4) {
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

bool OutputFile::checkFiletypeXdmf(const string& filename) { return false; }

int OutputFile::findNetcdfVarId() {
  assert(this->isOpen());
  assert(this->filetype() == Adcirc::Output::Netcdf3 ||
         this->filetype() == Adcirc::Output::Netcdf4);

  if (!this->isOpen()) {
    Adcirc::Error::throwError("OutputFile: Netcdf file not open");
    return Adcirc::HasError;
  }

  if (!(this->filetype() != Adcirc::Output::Netcdf3) ||
      !(this->filetype() == Adcirc::Output::Netcdf4)) {
    Adcirc::Error::throwError("OutputFile: Filetype is not netcdf");
    return Adcirc::HasError;
  }

  int varid;

  for (const auto& varname : netcdfVarNames) {
    int ierr = nc_inq_varid(this->m_ncid, varname.c_str(), &varid);
    if (ierr == NC_NOERR) {
      this->m_varid_data.push_back(varid);
      if (varname.substr(varname.size() - 3, varname.size()) == "max" ||
          varname.substr(varname.size() - 3, varname.size()) == "min") {
        this->m_isMax = true;
      }
    }
  }

  if (this->m_varid_data.size() == 0) {
    Adcirc::Error::throwError("OutputFile: No valid netcdf variables found");
    return Adcirc::HasError;
  }
  if (this->m_varid_data.size() > 2) {
    Adcirc::Error::throwError("OutputFile: Too many netcdf variables found");
    return Adcirc::HasError;
  }

  if (this->m_varid_data.size() == 1) {
    this->m_isVector = false;
  }
  if (this->m_varid_data.size() == 2) {
    this->m_isVector = true;
  }

  return Adcirc::NoError;
}

int OutputFile::readAsciiHeader() {
  assert(!this->m_filename.empty());

  if (this->m_filename.empty()) {
    Adcirc::Error::throwError("OutputFile: No filename specified");
    return Adcirc::HasError;
  }

  string line;
  std::getline(this->m_fid, line);  // header
  this->setHeader(line);

  std::getline(this->m_fid, line);  // file info header

  vector<string> list;
  int ierr = IO::splitString(line, list);
  if (ierr != Adcirc::NoError) {
    this->m_fid.close();
    Adcirc::Error::throwError("OutputFile: Error reading ascii header");
    return Adcirc::HasError;
  }

  bool ok;
  this->setNumSnaps(StringConversion::stringToInt(list.at(0), ok));
  if (!ok) {
    this->m_fid.close();
    Adcirc::Error::throwError("OutputFile: Error reading ascii header");
    return Adcirc::HasError;
  }

  this->setNumNodes(StringConversion::stringToSizet(list.at(1), ok));
  if (!ok) {
    this->m_fid.close();
    Adcirc::Error::throwError("OutputFile: Error reading ascii header");
    return Adcirc::HasError;
  }

  this->setDt(StringConversion::stringToDouble(list.at(2), ok));
  if (!ok) {
    this->m_fid.close();
    Adcirc::Error::throwError("OutputFile: Error reading ascii header");
    return Adcirc::HasError;
  }

  this->setDiteration(StringConversion::stringToInt(list.at(3), ok));
  if (!ok) {
    this->m_fid.close();
    Adcirc::Error::throwError("OutputFile: Error reading ascii header");
    return Adcirc::HasError;
  }

  int numCols = StringConversion::stringToInt(list.at(4), ok);
  if (numCols == 1) {
    this->m_isVector = false;
  } else if (numCols == 2) {
    this->m_isVector = true;
  } else {
    this->m_fid.close();
    Adcirc::Error::throwError("OutputFile: Invalid number of columns in file");
    return Adcirc::HasError;
  }

  return Adcirc::NoError;
}

int OutputFile::readNetcdfHeader() {
  assert(this->isOpen());

  int ierr = nc_inq_dimid(this->m_ncid, "time", &this->m_dimid_time);
  if (ierr != NC_NOERR) {
    Adcirc::Error::throwError("OutputFile: Time dimension not found");
    return Adcirc::HasError;
  }

  ierr = nc_inq_varid(this->m_ncid, "time", &this->m_varid_time);
  if (ierr != NC_NOERR) {
    Adcirc::Error::throwError("OutputFile: Time variable not found");
    return Adcirc::HasError;
  }

  ierr = nc_inq_dimlen(this->m_ncid, this->m_dimid_time, &this->m_numSnaps);
  if (ierr != NC_NOERR) {
    Adcirc::Error::throwError("OutputFile: Error reading time dimension");
    return Adcirc::HasError;
  }

  ierr = nc_inq_dimid(this->m_ncid, "node", &this->m_dimid_node);
  if (ierr != NC_NOERR) {
    Adcirc::Error::throwError("OutputFile: Node dimension not found");
    return Adcirc::HasError;
  }

  ierr = nc_inq_dimlen(this->m_ncid, this->m_dimid_node, &this->m_numNodes);
  if (ierr != NC_NOERR) {
    Adcirc::Error::throwError("OutputFile: Error reading node dimension");
    return Adcirc::HasError;
  }

  double dt;
  ierr = nc_get_att_double(this->m_ncid, NC_GLOBAL, "dt", &dt);
  if (ierr != NC_NOERR) {
    Adcirc::Error::throwError("OutputFile: Error reading model dt");
    return Adcirc::HasError;
  }

  double* t = (double*)malloc(sizeof(double) * this->m_numSnaps);

  ierr = nc_get_var_double(this->m_ncid, this->m_varid_time, t);
  if (ierr != NC_NOERR) {
    free(t);
    Adcirc::Error::throwError(nc_strerror(ierr));
    return Adcirc::HasError;
  }

  if (this->m_numSnaps > 1) {
    this->m_dt = t[1] - t[0];
  } else {
    this->m_dt = t[0];
  }
  this->m_dit = this->m_dt / dt;
  this->m_time = vector<double>(t, t + this->m_numSnaps);
  free(t);

  ierr = this->findNetcdfVarId();
  if (ierr != Adcirc::NoError) {
    Adcirc::Error::throwError(
        "OutputFile: Error locating netcdf output variables");
    return Adcirc::HasError;
  }

  int nofill;
  ierr = nc_inq_var_fill(this->m_ncid, this->m_varid_data[0], &nofill,
                         &this->m_defaultValue);
  if (ierr != Adcirc::NoError) {
    Adcirc::Error::throwError("OutputFile: Error reading default value");
    return Adcirc::HasError;
  }

  return Adcirc::NoError;
}

int OutputFile::readAsciiRecord(unique_ptr<OutputRecord>& record) {
  string line;

  record = unique_ptr<OutputRecord>(new OutputRecord(
      this->m_currentSnap, this->m_numNodes, this->m_isVector));

  //...Record header
  getline(this->m_fid, line);
  std::vector<string> list;
  IO::splitString(line, list);
  bool ok;

  double t = StringConversion::stringToDouble(list[0], ok);
  if (ok) {
    record.get()->setTime(t);
  } else {
    record.reset(nullptr);
    Adcirc::Error::throwError("OutputFile: Error reading ascii record");
    return Adcirc::HasError;
  }

  int it = StringConversion::stringToInt(list[1], ok);
  if (ok) {
    record.get()->setIteration(it);
  } else {
    record.reset(nullptr);
    Adcirc::Error::throwError("OutputFile: Error reading ascii record");
    return Adcirc::HasError;
  }

  size_t numNonDefault = this->m_numNodes;
  double dflt = Adcirc::Output::DefaultOutputValue;

  if (list.size() > 2) {
    numNonDefault = StringConversion::stringToSizet(list[2], ok);
    if (!ok) {
      record.reset(nullptr);
      Adcirc::Error::throwError("OutputFile: Error reading ascii record");
      return Adcirc::HasError;
    }

    dflt = StringConversion::stringToDouble(list[3], ok);
    if (!ok) {
      record.reset(nullptr);
      Adcirc::Error::throwError("OutputFile: Error reading ascii record");
      return Adcirc::HasError;
    }
  }
  record.get()->setDefaultValue(dflt);
  record.get()->fill(dflt);

  //...Record loop
  for (size_t i = 0; i < numNonDefault; i++) {
    getline(this->m_fid, line);

    if (this->m_isVector) {
      size_t id;
      double v1, v2;
      int ierr = IO::splitStringAttribute2Format(line, id, v1, v2);
      if (ierr == 0) {
        record.get()->set(id - 1, v1, v2);
      } else {
        record.reset(nullptr);
        Adcirc::Error::throwError("OutputFile: Error reading ascii record");
        return Adcirc::HasError;
      }
    } else {
      size_t id;
      double v1;
      int ierr = IO::splitStringAttribute1Format(line, id, v1);
      if (ierr == 0) {
        record.get()->set(id - 1, v1);
      } else {
        record.reset(nullptr);
        Adcirc::Error::throwError("OutputFile: Error reading ascii record");
        return Adcirc::HasError;
      };
    }
  }

  //...Setup the map for record indicies
  this->m_recordMap[record.get()->record()] = record.get();
  this->m_currentSnap++;

  return Adcirc::NoError;
}

int OutputFile::readNetcdfRecord(size_t snap,
                                 std::unique_ptr<OutputRecord>& record) {
  if (snap == Output::NextOutputSnap) {
    snap = this->m_currentSnap;
  }

  assert(snap < this->m_numSnaps);
  assert(this->isOpen());

  if (snap > this->m_numSnaps) {
    Adcirc::Error::throwError(
        "OutputFile: Record requested > number of records in file");
    return Adcirc::HasError;
  }
  record = unique_ptr<OutputRecord>(
      new OutputRecord(snap, this->m_numNodes, this->m_isVector));

  record.get()->setTime(this->m_time[snap]);
  record.get()->setIteration(std::floor(this->m_time[snap] / this->m_dt));

  //..Read the data record. If it is a max record, there is
  //  no time dimension
  if (this->m_isMax) {
    double* u = (double*)malloc(sizeof(double) * this->m_numNodes);
    int ierr = nc_get_var(this->m_ncid, this->m_varid_data[0], u);

    if (ierr != NC_NOERR) {
      free(u);
      Adcirc::Error::throwError("OutputFile: Error reading netcdf record");
      return Adcirc::HasError;
    }
    record.get()->setAll(this->m_numNodes, u);
    free(u);

  } else {
    size_t start[2], count[2];
    start[0] = snap;
    start[1] = 0;
    count[0] = 1;
    count[1] = this->m_numNodes;
    double* u = (double*)malloc(sizeof(double) * this->m_numNodes);

    int ierr =
        nc_get_vara(this->m_ncid, this->m_varid_data[0], start, count, u);
    if (ierr != NC_NOERR) {
      free(u);
      Adcirc::Error::throwError("OutputFile: Error reading netcdf record");
      return Adcirc::HasError;
    }

    if (this->m_isVector) {
      double* v = (double*)malloc(sizeof(double) * this->m_numNodes);
      ierr = nc_get_vara(this->m_ncid, this->m_varid_data[1], start, count, v);
      if (ierr != NC_NOERR) {
        free(u);
        free(v);
        Adcirc::Error::throwError("OutputFile: Error reading netcdf record");
        return Adcirc::HasError;
      }
      record.get()->setAll(this->m_numNodes, u, v);
      free(v);
    } else {
      record.get()->setAll(this->m_numNodes, u);
    }

    free(u);
  }

  this->m_recordMap[record.get()->record()] = record.get();
  this->m_currentSnap++;
  return Adcirc::NoError;
}

int OutputFile::rebuildMap() {
  this->m_recordMap.clear();
  for (auto& m_record : this->m_records) {
    this->m_recordMap[m_record->record()] = m_record.get();
  }
  return Adcirc::NoError;
}