#ifndef CNN_IDENTITY_H
#define CNN_IDENTITY_H
#include <string>
// Validate installed candidate declarations before opening a device. Live GET_INFO
// remains a separate gate; JSON metadata cannot prove the physically loaded bit.
bool cnn_identity_check(const std::string &manifest,const std::string &commit,
 const std::string &build,const std::string &boot,const std::string &machine,
 const std::string &kernel,std::string *reason);
#endif
