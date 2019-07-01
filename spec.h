#include <string>
#include <vector>

class Spec {
public:
  std::vector<std::string>   baseKeep;
  std::vector<std::string>   baseRemove;

  std::vector<std::string>   pkgInstall;    // 0..oo packages to install

  std::string                runExecutable; // 0..1 executables can be run
  std::vector<std::string>   runService;    // 0..oo services can be run

  void validate() const;
};

Spec parseSpec(const std::string &fname);
