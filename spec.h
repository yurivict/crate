#include <string>
#include <vector>
#include <set>

class Spec {
public:
  std::vector<std::string>   baseKeep;
  std::vector<std::string>   baseRemove;

  std::vector<std::string>   pkgInstall;    // 0..oo packages to install
  std::vector<std::string>   pkgAdd;        // 0..oo packages to install

  std::string                runExecutable; // 0..1 executables can be run
  std::vector<std::string>   runServices;   // 0..oo services can be run

  std::set<std::string>      options;       // various options that this spec uses

  void validate() const;
  Spec preprocess() const;
  bool optionExists(const char* opt) const {return options.find(opt) != options.end();}
};

Spec parseSpec(const std::string &fname);
