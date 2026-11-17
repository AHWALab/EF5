
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

CONFIG_PARSE_RESULTS Config::ParseConfig()
{

  FILE *configFile;
  size_t fileLen;
  char *buffer;

  configFile = fopen(name, "rb");
  if (configFile == NULL)
  {
    ERROR_LOGF("Failed to open configuration file %s\n", name);
    return CONFIG_OPENFAILED;
  }

  // Get file length
  fseek(configFile, 0, SEEK_END);
  fileLen = ftell(configFile);
  fseek(configFile, 0, SEEK_SET);

  // Read the entire file into buffer, +4 to give us extra room to play with!
  buffer = new char[fileLen + 4];
  size_t readLen = fread(buffer, 1, fileLen, configFile);
  if (readLen != fileLen)
  {
    ERROR_LOGF("Failed to read configuration file %s\n", name);
    delete[] buffer;
    fclose(configFile);
    return CONFIG_READFAILED;
  }

  // Fill the extra space with zeros
  buffer[fileLen] = 0;
  buffer[fileLen + 1] = 0;
  buffer[fileLen + 2] = 0;
  buffer[fileLen + 3] = 0;

  // Close the file, we're done with it
  fclose(configFile);

  // Start the process of parsing all the information
  int line = 1;
  char nameBuf[CONFIG_MAX_LEN] = "";
  char valBuf[CONFIG_MAX_LEN] = "";
  size_t i = 0, nameIdx = 0, valIdx = 0;
  CONFIG_PARSE_STATE state = PARSE_NORMAL;
  ConfigSection *currentSection = NULL;

  // This is the meat of the parsing here
  for (i = 0; i < fileLen; i++)
  {
    switch (state)
    {
    case PARSE_NORMAL:
    {
      if (buffer[i] == '/' && buffer[i + 1] == '/')
      {
        state = PARSE_CPLUSPLUS_COMMENT;
        i++;
        continue;
      }
      else if (buffer[i] == '/' && buffer[i + 1] == '*')
      {
        state = PARSE_C_COMMENT;
        i++;
        continue;
      }
      else if (buffer[i] == '#')
      {
        state = PARSE_BASH_COMMENT;
        continue;
      }
      else if (buffer[i] == '\r' || buffer[i] == '\t' || buffer[i] == ' ')
      {
        continue;
      }
      else if (buffer[i] == '\n')
      {
        line++;
        continue;
      }
      else if (buffer[i] == '[')
      {
        state = PARSE_SECTION_NAME;
        nameIdx = 0;
        valIdx = 0;
        continue;
      }
      else if (currentSection == NULL)
      {
        ERROR_LOGF("%s(%i): Invalid key-value outside of section", name, line);
        delete[] buffer;
        return CONFIG_INV_NAMEKEY;
      }
      else
      {
        state = PARSE_NAME;
        nameBuf[0] = buffer[i];
        nameIdx = 1;
        valIdx = 0;
        continue;
      }
      break;
    }
    case PARSE_CPLUSPLUS_COMMENT:
    case PARSE_BASH_COMMENT:
    {
      if (buffer[i] == '\n')
      {
        line++;
        state = PARSE_NORMAL;
        continue;
      }
      else
      {
        continue;
      }
      break;
    }
    case PARSE_C_COMMENT:
    {
      if (buffer[i] == '\n')
      {
        line++;
        continue;
      }
      else if (buffer[i] == '*' && buffer[i + 1] == '/')
      {
        i++;
        state = PARSE_NORMAL;
        continue;
      }
      else
      {
        continue;
      }
      break;
    }
    case PARSE_SECTION_NAME:
    {
      if (buffer[i] == ' ')
      {
        state = PARSE_SECTION_VALUE;
        nameBuf[nameIdx] = 0; // Add the null terminator to the name string
        continue;
      }
      else if (buffer[i] == ']')
      {
        state = PARSE_NORMAL;
        valBuf[0] = 0;
        nameBuf[nameIdx] = 0;
        // Allow old section to throw errors
        if (currentSection != NULL && currentSection->ValidateSection())
        {
          ERROR_LOGF("%s(%i): Previous section not valid", name, line);
          delete[] buffer;
          return CONFIG_INV_SECTION;
        }
        currentSection = GetConfigSection(nameBuf, valBuf);
        if (currentSection == NULL)
        {
          ERROR_LOGF("%s(%i): Unknown section \"%s\"", name, line, nameBuf);
          delete[] buffer;
          return CONFIG_INV_SECHEAD;
        }
        continue;
      }
      else if (buffer[i] == '\n')
      {
        // Invalid case!
        ERROR_LOGF("%s(%i): Invalid section header contains newline", name,
                   line);
        delete[] buffer;
        return CONFIG_INV_SECHEAD;
      }
      else
      {
        nameBuf[nameIdx] = buffer[i];
        nameIdx++;
        if (nameIdx == CONFIG_MAX_LEN)
        {
          ERROR_LOGF("%s(%i): Section name exceeds max length of %i", name,
                     line, CONFIG_MAX_LEN);
          delete[] buffer;
          return CONFIG_INV_SECHEAD;
        }
        continue;
      }
      break;
    }
    case PARSE_SECTION_VALUE:
    {
      if (buffer[i] == ']')
      {
        state = PARSE_NORMAL;
        valBuf[valIdx] = 0; // Add the null terminator
        // Allow old section to throw errors
        if (currentSection != NULL && currentSection->ValidateSection())
        {
          ERROR_LOGF("%s(%i): Previous section not valid", name, line);
          delete[] buffer;
          return CONFIG_INV_SECTION;
        }
        currentSection = GetConfigSection(nameBuf, valBuf);
        if (currentSection == NULL)
        {
          ERROR_LOGF("%s(%i): Unknown or invalid section \"%s\"", name, line,
                     nameBuf);
          delete[] buffer;
          return CONFIG_INV_SECHEAD;
        }
        continue;
      }
      else if (buffer[i] == '\n')
      {
        // Invalid case!
        ERROR_LOGF("%s(%i): Invalid section header contains newline", name,
                   line);
        delete[] buffer;
        return CONFIG_INV_SECHEAD;
      }
      else if (buffer[i] == '\r')
      {
        continue;
      }
      else
      {
        valBuf[valIdx] = buffer[i];
        valIdx++;
        if (valIdx == CONFIG_MAX_LEN)
        {
          ERROR_LOGF("%s(%i): Section name exceeds max length of %i", name,
                     line, CONFIG_MAX_LEN);
          delete[] buffer;
          return CONFIG_INV_SECHEAD;
        }
        continue;
      }
      break;
    }
    case PARSE_NAME:
    {
      if (buffer[i] == '=')
      {
        state = PARSE_VALUE;
        nameBuf[nameIdx] = 0; // Add null terminator
        continue;
      }
      else if (buffer[i] == '\n')
      {
        nameBuf[nameIdx] = 0; // Add null terminator
        ERROR_LOGF("%s(%i): Invalid key (%s) contains newline", name, line,
                   nameBuf);
        delete[] buffer;
        return CONFIG_INV_NAME;
      }
      else
      {
        nameBuf[nameIdx] = buffer[i];
        nameIdx++;
        if (nameIdx == CONFIG_MAX_LEN)
        {
          ERROR_LOGF("%s(%i): Key exceeds max length of %i", name, line,
                     CONFIG_MAX_LEN);
          delete[] buffer;
          return CONFIG_INV_NAME;
        }
        continue;
      }
      break;
    }
    case PARSE_VALUE:
    {
      if (buffer[i] == ' ' || buffer[i] == '\n' || buffer[i] == '\r')
      {
        state = PARSE_NORMAL;
        valBuf[valIdx] = 0; // Add null terminator
        if (buffer[i] == '\n')
        {
          line++;
        }
        if (currentSection->ProcessKeyValue(nameBuf, valBuf))
        {
          // ProcessKeyValue is responsible for printing error messages
          ERROR_LOGF("%s(%i): Invalid key-value pair!", name, line);
          delete[] buffer;
          return CONFIG_INV_NAMEKEY;
        }
        continue;
      }
      else
      {
        valBuf[valIdx] = buffer[i];
        valIdx++;
        if (valIdx == CONFIG_MAX_LEN)
        {
          ERROR_LOGF("%s(%i): Value exceeds max length of %i", name, line,
                     CONFIG_MAX_LEN);
          delete[] buffer;
          return CONFIG_INV_NAMEKEY;
        }
        continue;
      }
      break;
    }
    default:
      INFO_LOGF("Line(%i): First unprocessed line!", line);
      return CONFIG_SUCCESS;
    }
  }

  if (state == PARSE_VALUE)
  {
    state = PARSE_NORMAL;
    valBuf[valIdx] = 0; // Add null terminator
    if (currentSection->ProcessKeyValue(nameBuf, valBuf))
    {
      // ProcessKeyValue is responsible for printing error messages
      ERROR_LOGF("%s(%i): Invalid key-value pair!", name, line);
      delete[] buffer;
      return CONFIG_INV_NAMEKEY;
    }
  }

  delete[] buffer;

  if (currentSection != NULL && currentSection->ValidateSection())
  {
    ERROR_LOGF("%s(%i): Previous section not valid", name, line);
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
