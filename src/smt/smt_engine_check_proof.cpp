/*********************                                                        */
/*! \file smt_engine_check_proof.cpp
 ** \verbatim
 ** Original author: Morgan Deters
 ** Major contributors: none
 ** Minor contributors (to current version): none
 ** This file is part of the CVC4 project.
 ** Copyright (c) 2009-2014  New York University and The University of Iowa
 ** See the file COPYING in the top-level source directory for licensing
 ** information.\endverbatim
 **
 ** \brief [[ Add one-line brief description here ]]
 **
 ** [[ Add lengthier description here ]]
 ** \todo document this file
 **/

#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#warning "TODO: Why is lfsc's check.h being included like this?"
#include "check.h"

#include "base/configuration_private.h"
#include "base/cvc4_assert.h"
#include "base/output.h"
#include "smt/smt_engine.h"
#include "util/statistics_registry.h"

using namespace CVC4;
using namespace std;

namespace CVC4 {

namespace proof {
  extern const char *const plf_signatures;
}/* CVC4::proof namespace */

namespace smt {

class UnlinkProofFile {
  string d_filename;
public:
  UnlinkProofFile(const char* filename) : d_filename(filename) {}
  ~UnlinkProofFile() { unlink(d_filename.c_str()); }
};/* class UnlinkProofFile */

}/* CVC4::smt namespace */

}/* CVC4 namespace */

void SmtEngine::checkProof() {

#if IS_PROOFS_BUILD

  Chat() << "generating proof..." << endl;

  Proof* pf = getProof();

  Chat() << "checking proof..." << endl;

  std::string logicString = d_logic.getLogicString();

  if (!(
        // Pure logics
        logicString == "QF_UF" ||
        logicString == "QF_AX" ||
        logicString == "QF_BV" ||
        // Non-pure logics
        logicString == "QF_AUF" ||
        logicString == "QF_UFBV" ||
        logicString == "QF_ABV" ||
        logicString == "QF_AUFBV" ||
        // Arithmetic with holes
        logicString == "QF_UFLRA" ||
        logicString == "QF_UFLIA"
        )) {
    // This logic is not yet supported
    Notice() << "Notice: no proof-checking for " << logicString << " proofs yet" << endl;
    return;
  }

  // if ( ( !(d_logic.isPure(theory::THEORY_BOOL) ||
  //          d_logic.isPure(theory::THEORY_BV) ||
  //          d_logic.isPure(theory::THEORY_ARRAY) ||
  //          (d_logic.isPure(theory::THEORY_UF) &&
  //           ! d_logic.hasCardinalityConstraints())) ||
  //        d_logic.isQuantified())
  //      // Override for Arith proofs with holds
  //      // && (! (d_logic.getLogicString() == "QF_UFLRA" || d_logic.getLogicString() == "QF_UFLIA" )))
  //      && (! (d_logic.getLogicString() == "QF_ABV" )))
  // {
  //   // no checking for these yet
  //   Notice() << "Notice: no proof-checking for non-UF/Bool/BV proofs yet" << endl;
  //   return;
  // }

  char const* tempDir = getenv("TMPDIR");
  if (!tempDir) {
    tempDir = "/tmp";
  }

  stringstream pfPath;
  pfPath << tempDir << "/cvc4_proof.XXXXXX";

  char* pfFile = strdup(pfPath.str().c_str());
  int fd = mkstemp(pfFile);

  // ensure this temp file is removed after
  smt::UnlinkProofFile unlinker(pfFile);

  ofstream pfStream(pfFile);
  pfStream << proof::plf_signatures << endl;
  pf->toStream(pfStream);
  pfStream.close();
  args a;
  a.show_runs = false;
  a.no_tail_calls = false;
  a.compile_scc = false;
  a.compile_scc_debug = false;
  a.run_scc = false;
  a.use_nested_app = false;
  a.compile_lib = false;
  init();
  check_file(pfFile, a);
  close(fd);

#else /* IS_PROOFS_BUILD */

  Unreachable("This version of CVC4 was built without proof support; cannot check proofs.");

#endif /* IS_PROOFS_BUILD */

}
