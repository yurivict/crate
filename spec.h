#include <string>
#include <vector>
#include <set>
#include <map>

class Spec {
public:
  std::vector<std::string>                           baseKeep;
  std::vector<std::string>                           baseRemove;

  std::vector<std::string>                           pkgInstall;              // 0..oo packages to install
  std::vector<std::pair<std::string, std::string>>   pkgLocalOverride;        // 0..oo packages to override
  std::vector<std::string>                           pkgAdd;                  // 0..oo packages to add
  std::vector<std::string>                           pkgNuke;                 // 0..oo packages to nuke, i.e. delete without regard of them being nominally used

  std::string                                        runExecutable;           // 0..1 executables can be run
  std::vector<std::string>                           runServices;             // 0..oo services can be run

  std::vector<std::pair<std::string, std::string>>   dirsShare;               // any number of directories can be shared, {from -> to} mappings are elements

  std::set<std::string>                              options;                 // various options that this spec uses

  std::map<std::string, std::map<std::string, std::string>> scripts;          // by section, by script name

  void validate() const;
  Spec preprocess() const;
  bool optionExists(const char* opt) const {return options.find(opt) != options.end();}
};

Spec parseSpec(const std::string &fname);
