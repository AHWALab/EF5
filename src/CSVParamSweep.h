#ifndef CSV_PARAM_SWEEP_H
#define CSV_PARAM_SWEEP_H

#include "Model.h"
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// ParamSweepRow — One row from the sweep CSV, representing a single
// set of model + routing parameters to simulate with.
// ─────────────────────────────────────────────────────────────────────────────
struct ParamSweepRow {
  std::string runId;
  float       crestParams[PARAM_CREST_QTY];
  float       kwParams[PARAM_KINEMATIC_QTY];
  bool        crestSet[PARAM_CREST_QTY];
  bool        kwSet[PARAM_KINEMATIC_QTY];

  ParamSweepRow() {
    memset(crestParams, 0, sizeof(crestParams));
    memset(kwParams, 0, sizeof(kwParams));
    memset(crestSet, 0, sizeof(crestSet));
    memset(kwSet, 0, sizeof(kwSet));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// CSVParamSweep — Lightweight CSV parser for parameter sweep files.
//
// Expected CSV format:
//   run_id,wm,b,im,ke,fc,iwu,under,leaki,th,isu,alpha,beta,alpha0
//   set_001,200.0,0.5,0.02,0.4,2.0,0.15,0.05,0.5,0.001,0.0,1.0,1.0,0.05
//   ...
//
// Column names are matched case-insensitively against CREST and KW param names
// defined in Models.tbl. Unrecognized columns are silently ignored.
// ─────────────────────────────────────────────────────────────────────────────
class CSVParamSweep {
 public:
  // Parse a CSV file. Returns true on success.
  // On failure, sets errorMsg and returns false.
  static bool Parse(const char* csvPath, MODELS model, ROUTES route,
                    std::vector<ParamSweepRow>& rows, std::string& errorMsg) {
    rows.clear();

    std::ifstream file(csvPath);
    if (!file.is_open()) {
      errorMsg = std::string("Cannot open CSV file: ") + csvPath;
      return false;
    }

    int          numCrestParams = numModelParams[model];
    int          numKWParams    = numRouteParams[route];
    const char** crestNames     = modelParamStrings[model];
    const char** kwNames        = routeParamStrings[route];

    // ── Read header line ──────────────────────────────────────────────────
    std::string headerLine;
    if (!std::getline(file, headerLine)) {
      errorMsg = "CSV file is empty (no header)";
      return false;
    }

    // Trim trailing \r (Windows line endings)
    if (!headerLine.empty() && headerLine.back() == '\r') {
      headerLine.pop_back();
    }

    std::vector<std::string> headers;
    SplitCSVLine(headerLine, headers);

    if (headers.empty()) {
      errorMsg = "CSV header is empty";
      return false;
    }

    // ── Map column indices to param types ─────────────────────────────────
    int              runIdCol = -1;
    std::vector<int> modelColMap(headers.size(), -1);  // header col → model param idx
    std::vector<int> routeColMap(headers.size(), -1);  // header col → route param idx

    for (size_t col = 0; col < headers.size(); col++) {
      std::string h = ToLower(Trim(headers[col]));

      if (h == "run_id" || h == "runid" || h == "id" || h == "name") {
        runIdCol = (int)col;
        continue;
      }

      // Try model param match (e.g., CREST: wm, b, im, ke, fc, iwu)
      bool found = false;
      for (int p = 0; p < numCrestParams; p++) {
        if (h == ToLower(std::string(crestNames[p]))) {
          modelColMap[col] = p;
          found            = true;
          break;
        }
      }
      if (found) continue;

      // Try route param match (e.g., KW: under, leaki, th, isu, alpha, beta, alpha0)
      for (int p = 0; p < numKWParams; p++) {
        if (h == ToLower(std::string(kwNames[p]))) {
          routeColMap[col] = p;
          found            = true;
          break;
        }
      }
      // Unrecognized columns are silently ignored
    }

    // ── Read data rows ───────────────────────────────────────────────────
    std::string line;
    int         lineNum = 1;
    while (std::getline(file, line)) {
      lineNum++;

      // Trim and skip empty lines / comments
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      std::string trimmed = Trim(line);
      if (trimmed.empty() || trimmed[0] == '#') continue;

      std::vector<std::string> fields;
      SplitCSVLine(line, fields);

      if (fields.size() != headers.size()) {
        errorMsg = "CSV line " + std::to_string(lineNum) + " has " + std::to_string(fields.size()) +
                   " fields, expected " + std::to_string(headers.size());
        return false;
      }

      ParamSweepRow row;

      // Run ID
      if (runIdCol >= 0) {
        row.runId = Trim(fields[runIdCol]);
      } else {
        row.runId = "sweep_" + std::to_string(rows.size() + 1);
      }

      // Map fields to params
      for (size_t col = 0; col < fields.size(); col++) {
        std::string val = Trim(fields[col]);
        if (val.empty()) continue;

        if (modelColMap[col] >= 0) {
          row.crestParams[modelColMap[col]] = std::stof(val);
          row.crestSet[modelColMap[col]]    = true;
        }
        if (routeColMap[col] >= 0) {
          row.kwParams[routeColMap[col]] = std::stof(val);
          row.kwSet[routeColMap[col]]    = true;
        }
      }

      rows.push_back(row);
    }

    if (rows.empty()) {
      errorMsg = "CSV file has header but no data rows";
      return false;
    }

    return true;
  }

 private:
  // Split a CSV line by commas
  static void SplitCSVLine(const std::string& line, std::vector<std::string>& fields) {
    fields.clear();
    std::stringstream ss(line);
    std::string       field;
    while (std::getline(ss, field, ',')) {
      fields.push_back(field);
    }
  }

  static std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
  }

  static std::string ToLower(const std::string& s) {
    std::string result = s;
    for (auto& c : result) {
      c = (char)tolower((unsigned char)c);
    }
    return result;
  }
};

#endif  // CSV_PARAM_SWEEP_H
