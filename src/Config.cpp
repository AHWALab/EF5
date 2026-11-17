
#include "Config.h"
#include "BasicConfigSection.h"
#include "BasinConfigSection.h"
#include "CaliParamConfigSection.h"
#include "ConfigSection.h"
#include "EnsTaskConfigSection.h"
#include "ExecuteConfigSection.h"
#include "GaugeConfigSection.h"
#include "Messages.h"
#include "Model.h"
#include "PETConfigSection.h"
#include "ParamSetConfigSection.h"
#include "PrecipConfigSection.h"
#include "TaskConfigSection.h"
#include "TempConfigSection.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

Config *g_config = NULL;

enum CONFIG_PARSE_STATE
{
  PARSE_NORMAL,
  PARSE_CPLUSPLUS_COMMENT,
  PARSE_C_COMMENT,
  PARSE_BASH_COMMENT,
  PARSE_NAME,
  PARSE_VALUE,
  PARSE_SECTION_NAME,
  PARSE_SECTION_VALUE,
};

Config::Config(const char *configName)
{
  // Copy over the config name into the class variable and ensure it is null
  // terminated
  strncpy(name, configName, CONFIG_MAX_LEN);
  name[CONFIG_MAX_LEN - 1] = 0;
}

Config::~Config() {}

namespace
{

// One piece of a control file: a section header, a key=value pair, or the
// error that stopped the reading. line is the line used in messages.
struct ConfigToken
{
  enum Kind
  {
    SECTION,
    KEY_VALUE,
    ERROR
  } kind;
  std::string name, value; // section kind and name, or key and value
  bool hasValue;           // SECTION: written as [kind name], not [kind]
  int line;
  CONFIG_PARSE_RESULTS error; // ERROR: what ParseConfig returns
  std::string message;        // ERROR: the message, without file and line
};

void AddToken(std::vector<ConfigToken> *tokens, ConfigToken::Kind kind,
              const std::string &name, const std::string &value, int line)
{
  ConfigToken t;
  t.kind = kind;
  t.name = name;
  t.value = value;
  t.hasValue = false;
  t.line = line;
  t.error = CONFIG_SUCCESS;
  tokens->push_back(t);
}

void AddError(std::vector<ConfigToken> *tokens, CONFIG_PARSE_RESULTS error,
              int line, const std::string &message)
{
  AddToken(tokens, ConfigToken::ERROR, "", "", line);
  tokens->back().error = error;
  tokens->back().message = message;
}

// Splits the text of a control file into tokens. Reading stops at the first
// error, which becomes the last token. *lastLine is the line reached.
//
// The format: [kind] or [kind name] starts a section; key=value pairs follow,
// separated by spaces or line ends (a value ends at a space, so a section can
// be written on one line). Between them, //, # and /* */ start comments;
// spaces, tabs and \r are skipped. A key runs up to '=' and may contain
// spaces; a section name runs up to ']' and may contain spaces.
void TokenizeConfig(const char *buffer, size_t fileLen,
                    std::vector<ConfigToken> *tokens, int *lastLine)
{
  int line = 1;
  std::string nameBuf, valBuf;
  CONFIG_PARSE_STATE state = PARSE_NORMAL;
  bool inSection = false;

  // buffer has zeros after fileLen, so buffer[i + 1] is always readable
  for (size_t i = 0; i < fileLen; i++)
  {
    const char c = buffer[i];
    switch (state)
    {
    case PARSE_NORMAL:
      if (c == '/' && buffer[i + 1] == '/')
      {
        state = PARSE_CPLUSPLUS_COMMENT;
        i++;
      }
      else if (c == '/' && buffer[i + 1] == '*')
      {
        state = PARSE_C_COMMENT;
        i++;
      }
      else if (c == '#')
      {
        state = PARSE_BASH_COMMENT;
      }
      else if (c == '\r' || c == '\t' || c == ' ')
      {
      }
      else if (c == '\n')
      {
        line++;
      }
      else if (c == '[')
      {
        state = PARSE_SECTION_NAME;
        nameBuf.clear();
        valBuf.clear();
      }
      else if (!inSection)
      {
        AddError(tokens, CONFIG_INV_NAMEKEY, line,
                 "Invalid key-value outside of section");
        *lastLine = line;
        return;
      }
      else
      {
        state = PARSE_NAME;
        nameBuf.assign(1, c);
        valBuf.clear();
      }
      break;

    case PARSE_CPLUSPLUS_COMMENT:
    case PARSE_BASH_COMMENT:
      if (c == '\n')
      {
        line++;
        state = PARSE_NORMAL;
      }
      break;

    case PARSE_C_COMMENT:
      if (c == '\n')
      {
        line++;
      }
      else if (c == '*' && buffer[i + 1] == '/')
      {
        i++;
        state = PARSE_NORMAL;
      }
      break;

    case PARSE_SECTION_NAME:
    case PARSE_SECTION_VALUE:
    {
      const bool inValue = (state == PARSE_SECTION_VALUE);
      if (c == ' ' && !inValue)
      {
        state = PARSE_SECTION_VALUE;
      }
      else if (c == ']')
      {
        state = PARSE_NORMAL;
        AddToken(tokens, ConfigToken::SECTION, nameBuf, valBuf, line);
        tokens->back().hasValue = inValue;
        inSection = true;
      }
      else if (c == '\n')
      {
        AddError(tokens, CONFIG_INV_SECHEAD, line,
                 "Invalid section header contains newline");
        *lastLine = line;
        return;
      }
      else if (c == '\r' && inValue)
      {
      }
      else
      {
        std::string *buf = inValue ? &valBuf : &nameBuf;
        buf->push_back(c);
        if (buf->size() == CONFIG_MAX_LEN)
        {
          char msg[100];
          snprintf(msg, sizeof(msg), "Section name exceeds max length of %i",
                   CONFIG_MAX_LEN);
          AddError(tokens, CONFIG_INV_SECHEAD, line, msg);
          *lastLine = line;
          return;
        }
      }
      break;
    }

    case PARSE_NAME:
      if (c == '=')
      {
        state = PARSE_VALUE;
      }
      else if (c == '\n')
      {
        AddError(tokens, CONFIG_INV_NAME, line,
                 "Invalid key (" + nameBuf + ") contains newline");
        *lastLine = line;
        return;
      }
      else
      {
        nameBuf.push_back(c);
        if (nameBuf.size() == CONFIG_MAX_LEN)
        {
          char msg[100];
          snprintf(msg, sizeof(msg), "Key exceeds max length of %i",
                   CONFIG_MAX_LEN);
          AddError(tokens, CONFIG_INV_NAME, line, msg);
          *lastLine = line;
          return;
        }
      }
      break;

    case PARSE_VALUE:
      if (c == ' ' || c == '\n' || c == '\r')
      {
        state = PARSE_NORMAL;
        if (c == '\n')
        {
          line++;
        }
        AddToken(tokens, ConfigToken::KEY_VALUE, nameBuf, valBuf, line);
      }
      else
      {
        valBuf.push_back(c);
        if (valBuf.size() == CONFIG_MAX_LEN)
        {
          char msg[100];
          snprintf(msg, sizeof(msg), "Value exceeds max length of %i",
                   CONFIG_MAX_LEN);
          AddError(tokens, CONFIG_INV_NAMEKEY, line, msg);
          *lastLine = line;
          return;
        }
      }
      break;
    }
  }

  // A value at the very end of the file, without a line end
  if (state == PARSE_VALUE)
  {
    AddToken(tokens, ConfigToken::KEY_VALUE, nameBuf, valBuf, line);
  }
  *lastLine = line;
}

} // namespace

CONFIG_PARSE_RESULTS Config::ParseConfig()
{
  FILE *configFile = fopen(name, "rb");
  if (configFile == NULL)
  {
    ERROR_LOGF("Failed to open configuration file %s\n", name);
    return CONFIG_OPENFAILED;
  }

  fseek(configFile, 0, SEEK_END);
  size_t fileLen = ftell(configFile);
  fseek(configFile, 0, SEEK_SET);

  // Read the whole file, with zeros after it so the reader can look one
  // character ahead
  std::vector<char> buffer(fileLen + 4, 0);
  size_t readLen = fread(buffer.data(), 1, fileLen, configFile);
  fclose(configFile);
  if (readLen != fileLen)
  {
    ERROR_LOGF("Failed to read configuration file %s\n", name);
    return CONFIG_READFAILED;
  }

  std::vector<ConfigToken> tokens;
  int lastLine = 1;
  TokenizeConfig(buffer.data(), fileLen, &tokens, &lastLine);

  // Give the pieces to the sections, in file order. Each section checks its
  // settings when the next one starts, and the last one at the end.
  ConfigSection *currentSection = NULL;
  char nameBuf[CONFIG_MAX_LEN], valBuf[CONFIG_MAX_LEN];
  for (size_t t = 0; t < tokens.size(); t++)
  {
    const ConfigToken &tok = tokens[t];
    if (tok.kind == ConfigToken::ERROR)
    {
      ERROR_LOGF("%s(%i): %s", name, tok.line, tok.message.c_str());
      return tok.error;
    }
    strcpy(nameBuf, tok.name.c_str());
    strcpy(valBuf, tok.value.c_str());
    if (tok.kind == ConfigToken::SECTION)
    {
      if (currentSection != NULL && currentSection->ValidateSection())
      {
        ERROR_LOGF("%s(%i): Previous section not valid", name, tok.line);
        return CONFIG_INV_SECTION;
      }
      currentSection = GetConfigSection(nameBuf, valBuf);
      if (currentSection == NULL)
      {
        if (tok.hasValue)
        {
          ERROR_LOGF("%s(%i): Unknown or invalid section \"%s\"", name,
                     tok.line, nameBuf);
        }
        else
        {
          ERROR_LOGF("%s(%i): Unknown section \"%s\"", name, tok.line,
                     nameBuf);
        }
        return CONFIG_INV_SECHEAD;
      }
    }
    else if (currentSection->ProcessKeyValue(nameBuf, valBuf))
    {
      // ProcessKeyValue is responsible for printing error messages
      ERROR_LOGF("%s(%i): Invalid key-value pair!", name, tok.line);
      return CONFIG_INV_NAMEKEY;
    }
  }

  if (currentSection != NULL && currentSection->ValidateSection())
  {
    ERROR_LOGF("%s(%i): Previous section not valid", name, lastLine);
    return CONFIG_INV_SECTION;
  }

  return CONFIG_SUCCESS;
}

// Creates a named section (e.g. [Gauge name]) and files it under its name,
// unless a section of that kind already has the name.
template <class T, class Make>
static ConfigSection *AddNamedSection(std::map<std::string, T *> *sections,
                                      bool duplicate, char *sectionVal,
                                      const char *kind, Make make)
{
  if (duplicate)
  {
    ERROR_LOGF("Duplicate %s section \"%s\"!", kind, sectionVal);
    return NULL;
  }
  T *sec = make();
  sections->insert(std::pair<std::string, T *>(sectionVal, sec));
  return sec;
}

// The parameter and calibration sections of one kind of model: for model i,
// [<paramSetNames[i]> name] and [<caliNames[i]> name]. found is set when the
// section name is one of them.
template <class E, class PS, class CS>
static ConfigSection *
AddModelSection(char *sectionName, char *sectionVal, int qty,
                const char **paramSetNames, const char **caliNames,
                std::map<std::string, PS *> *paramSets,
                std::map<std::string, CS *> *caliSets, const char *kind,
                bool *found)
{
  for (int i = 0; i < qty; i++)
  {
    E model = (E)i;
    if (strcasecmp(sectionName, paramSetNames[i]) == 0)
    {
      *found = true;
      std::string label = std::string(kind) + "ParamSet";
      return AddNamedSection(&paramSets[i],
                             PS::IsDuplicate(sectionVal, model), sectionVal,
                             label.c_str(),
                             [&]() { return new PS(sectionVal, model); });
    }
    else if (strcasecmp(sectionName, caliNames[i]) == 0)
    {
      *found = true;
      std::string label = std::string(kind) + "CaliParam";
      return AddNamedSection(&caliSets[i], CS::IsDuplicate(sectionVal, model),
                             sectionVal, label.c_str(),
                             [&]() { return new CS(sectionVal, model); });
    }
  }
  return NULL;
}

ConfigSection *Config::GetConfigSection(char *sectionName, char *sectionVal)
{
  if (strcasecmp(sectionName, "basic") == 0)
  {
    g_basicConfig = new BasicConfigSection();
    return g_basicConfig;
  }
  if (strcasecmp(sectionName, "execute") == 0)
  {
    g_executeConfig = new ExecuteConfigSection();
    return g_executeConfig;
  }

  // Every other section has a name, which is not case sensitive
  TOLOWER(sectionVal);
  if (strcasecmp(sectionName, "precipforcing") == 0)
  {
    return AddNamedSection(&g_precipConfigs,
                           PrecipConfigSection::IsDuplicate(sectionVal),
                           sectionVal, "Precip",
                           []() { return new PrecipConfigSection(); });
  }
  if (strcasecmp(sectionName, "petforcing") == 0)
  {
    return AddNamedSection(&g_petConfigs,
                           PETConfigSection::IsDuplicate(sectionVal),
                           sectionVal, "PET",
                           []() { return new PETConfigSection(); });
  }
  if (strcasecmp(sectionName, "tempforcing") == 0)
  {
    return AddNamedSection(&g_tempConfigs,
                           TempConfigSection::IsDuplicate(sectionVal),
                           sectionVal, "Temp",
                           []() { return new TempConfigSection(); });
  }
  if (strcasecmp(sectionName, "gauge") == 0)
  {
    return AddNamedSection(&g_gaugeConfigs,
                           GaugeConfigSection::IsDuplicate(sectionVal),
                           sectionVal, "Gauge",
                           [&]() { return new GaugeConfigSection(sectionVal); });
  }
  if (strcasecmp(sectionName, "basin") == 0)
  {
    return AddNamedSection(&g_basinConfigs,
                           BasinConfigSection::IsDuplicate(sectionVal),
                           sectionVal, "Basin",
                           [&]() { return new BasinConfigSection(sectionVal); });
  }
  if (strcasecmp(sectionName, "task") == 0)
  {
    return AddNamedSection(&g_taskConfigs,
                           TaskConfigSection::IsDuplicate(sectionVal),
                           sectionVal, "Task",
                           [&]() { return new TaskConfigSection(sectionVal); });
  }
  if (strcasecmp(sectionName, "ensembletask") == 0)
  {
    return AddNamedSection(
        &g_ensTaskConfigs, EnsTaskConfigSection::IsDuplicate(sectionVal),
        sectionVal, "Ensemble Task",
        [&]() { return new EnsTaskConfigSection(sectionVal); });
  }

  // Parameter and calibration sets: [crestparamset name], [kwcaliparams
  // name], ...
  bool found = false;
  ConfigSection *sec =
      AddModelSection<MODELS, ParamSetConfigSection, CaliParamConfigSection>(
          sectionName, sectionVal, MODEL_QTY, modelParamSetStrings,
          modelCaliParamStrings, g_paramSetConfigs, g_caliParamConfigs, "",
          &found);
  if (!found)
  {
    sec = AddModelSection<ROUTES, RoutingParamSetConfigSection,
                          RoutingCaliParamConfigSection>(
        sectionName, sectionVal, ROUTE_QTY, routeParamSetStrings,
        routeCaliParamStrings, g_routingParamSetConfigs,
        g_routingCaliParamConfigs, "Routing ", &found);
  }
  if (!found)
  {
    sec = AddModelSection<SNOWS, SnowParamSetConfigSection,
                          SnowCaliParamConfigSection>(
        sectionName, sectionVal, SNOW_QTY, snowParamSetStrings,
        snowCaliParamStrings, g_snowParamSetConfigs, g_snowCaliParamConfigs,
        "Snow ", &found);
  }
  if (!found)
  {
    sec = AddModelSection<INUNDATIONS, InundationParamSetConfigSection,
                          InundationCaliParamConfigSection>(
        sectionName, sectionVal, INUNDATION_QTY, inundationParamSetStrings,
        inundationCaliParamStrings, g_inundationParamSetConfigs,
        g_inundationCaliParamConfigs, "Inundation ", &found);
  }
  return sec;
}
