/*********************                                                        */
/*! \file proof_manager.cpp
 ** \verbatim
 ** Top contributors (to current version):
 **   Liana Hadarean, Guy Katz, Morgan Deters
 ** This file is part of the CVC4 project.
 ** Copyright (c) 2009-2016 by the authors listed in the file AUTHORS
 ** in the top-level source directory) and their institutional affiliations.
 ** All rights reserved.  See the file COPYING in the top-level source
 ** directory for licensing information.\endverbatim
 **
 ** [[ Add lengthier description here ]]

 ** \todo document this file

**/

#include "proof/proof_manager.h"

#include "base/cvc4_assert.h"
#include "context/context.h"
#include "options/bv_options.h"
#include "options/proof_options.h"
#include "proof/bitvector_proof.h"
#include "proof/clause_id.h"
#include "proof/cnf_proof.h"
#include "proof/proof_utils.h"
#include "proof/sat_proof_implementation.h"
#include "proof/theory_proof.h"
#include "smt/smt_engine.h"
#include "smt/smt_engine_scope.h"
#include "smt_util/node_visitor.h"
#include "theory/arrays/theory_arrays.h"
#include "theory/output_channel.h"
#include "theory/term_registration_visitor.h"
#include "theory/uf/equality_engine.h"
#include "theory/uf/theory_uf.h"
#include "theory/valuation.h"
#include "util/hash.h"
#include "util/proof.h"

namespace CVC4 {

std::string append(const std::string& str, uint64_t num) {
  std::ostringstream os;
  os << str << num;
  return os.str();
}

ProofManager::ProofManager(ProofFormat format):
  d_satProof(NULL),
  d_cnfProof(NULL),
  d_theoryProof(NULL),
  d_inputFormulas(),
  d_inputCoreFormulas(),
  d_outputCoreFormulas(),
  d_nextId(0),
  d_fullProof(NULL),
  d_format(format),
  d_deps()
{
}

ProofManager::~ProofManager() {
  if (d_satProof) delete d_satProof;
  if (d_cnfProof) delete d_cnfProof;
  if (d_theoryProof) delete d_theoryProof;
  if (d_fullProof) delete d_fullProof;
}

ProofManager* ProofManager::currentPM() {
  return smt::currentProofManager();
}

Proof* ProofManager::getProof(SmtEngine* smt) {
  if (currentPM()->d_fullProof != NULL) {
    return currentPM()->d_fullProof;
  }
  Assert (currentPM()->d_format == LFSC);

  currentPM()->d_fullProof = new LFSCProof(smt,
                                           (LFSCCoreSatProof*)getSatProof(),
                                           (LFSCCnfProof*)getCnfProof(),
                                           (LFSCTheoryProofEngine*)getTheoryProofEngine());
  return currentPM()->d_fullProof;
}

CoreSatProof* ProofManager::getSatProof() {
  Assert (currentPM()->d_satProof);
  return currentPM()->d_satProof;
}

CnfProof* ProofManager::getCnfProof() {
  Assert (currentPM()->d_cnfProof);
  return currentPM()->d_cnfProof;
}

TheoryProofEngine* ProofManager::getTheoryProofEngine() {
  Assert (currentPM()->d_theoryProof != NULL);
  return currentPM()->d_theoryProof;
}

UFProof* ProofManager::getUfProof() {
  Assert (options::proof());
  TheoryProof* pf = getTheoryProofEngine()->getTheoryProof(theory::THEORY_UF);
  return (UFProof*)pf;
}

BitVectorProof* ProofManager::getBitVectorProof() {
  Assert (options::proof());
  TheoryProof* pf = getTheoryProofEngine()->getTheoryProof(theory::THEORY_BV);
  return (BitVectorProof*)pf;
}

ArrayProof* ProofManager::getArrayProof() {
  Assert (options::proof());
  TheoryProof* pf = getTheoryProofEngine()->getTheoryProof(theory::THEORY_ARRAY);
  return (ArrayProof*)pf;
}

ArithProof* ProofManager::getArithProof() {
  Assert (options::proof());
  TheoryProof* pf = getTheoryProofEngine()->getTheoryProof(theory::THEORY_ARITH);
  return (ArithProof*)pf;
}

SkolemizationManager* ProofManager::getSkolemizationManager() {
  Assert (options::proof());
  return &(currentPM()->d_skolemizationManager);
}

void ProofManager::initSatProof(Minisat::Solver* solver) {
  Assert (currentPM()->d_satProof == NULL);
  Assert(currentPM()->d_format == LFSC);
  currentPM()->d_satProof = new LFSCCoreSatProof(solver, "");
}

void ProofManager::initCnfProof(prop::CnfStream* cnfStream,
                                context::Context* ctx) {
  ProofManager* pm = currentPM();
  Assert (pm->d_cnfProof == NULL);
  Assert (pm->d_format == LFSC);
  CnfProof* cnf = new LFSCCnfProof(cnfStream, ctx, "");
  pm->d_cnfProof = cnf;
  Assert(pm-> d_satProof != NULL);
  pm->d_satProof->setCnfProof(cnf);

  // true and false have to be setup in a special way
  Node true_node = NodeManager::currentNM()->mkConst<bool>(true);
  Node false_node = NodeManager::currentNM()->mkConst<bool>(false).notNode();

  pm->d_cnfProof->pushCurrentAssertion(true_node);
  pm->d_cnfProof->pushCurrentDefinition(true_node);
  pm->d_cnfProof->registerConvertedClause(pm->d_satProof->getTrueUnit());
  pm->d_cnfProof->popCurrentAssertion();
  pm->d_cnfProof->popCurrentDefinition();

  pm->d_cnfProof->pushCurrentAssertion(false_node);
  pm->d_cnfProof->pushCurrentDefinition(false_node);
  pm->d_cnfProof->registerConvertedClause(pm->d_satProof->getFalseUnit());
  pm->d_cnfProof->popCurrentAssertion();
  pm->d_cnfProof->popCurrentDefinition();

}

void ProofManager::initTheoryProofEngine() {
  Assert (currentPM()->d_theoryProof == NULL);
  Assert (currentPM()->d_format == LFSC);
  currentPM()->d_theoryProof = new LFSCTheoryProofEngine();
}

std::string ProofManager::getInputClauseName(ClauseId id,
                                             const std::string& prefix) {
  return append(prefix+".pb", id);
}

std::string ProofManager::getLemmaClauseName(ClauseId id,
                                             const std::string& prefix) {
  return append(prefix+".lemc", id);
}

std::string ProofManager::getLemmaName(ClauseId id, const std::string& prefix) {
  return append(prefix+"lem", id);
}

std::string ProofManager::getLearntClauseName(ClauseId id,
                                              const std::string& prefix) {
  return append(prefix+".cl", id);
}
std::string ProofManager::getVarName(prop::SatVariable var,
                                     const std::string& prefix) {
  return append(prefix+".v", var);
}
std::string ProofManager::getAtomName(prop::SatVariable var,
                                      const std::string& prefix) {
  return append(prefix+".a", var);
}
std::string ProofManager::getLitName(prop::SatLiteral lit,
                                     const std::string& prefix) {
  return append(prefix+".l", lit.toInt());
}

std::string ProofManager::getPreprocessedAssertionName(Node node,
                                                       const std::string& prefix) {
  node = node.getKind() == kind::BITVECTOR_EAGER_ATOM ? node[0] : node;
  return append(prefix+".PA", node.getId());
}
std::string ProofManager::getAssertionName(Node node,
                                           const std::string& prefix) {
  return append(prefix+".A", node.getId());
}

std::string ProofManager::getAtomName(TNode atom,
                                      const std::string& prefix) {
  prop::SatLiteral lit = currentPM()->d_cnfProof->getLiteral(atom);
  Assert(!lit.isNegated());
  return getAtomName(lit.getSatVariable(), prefix);
}

std::string ProofManager::getLitName(TNode lit,
                                     const std::string& prefix) {
  std::string litName = getLitName(currentPM()->d_cnfProof->getLiteral(lit), prefix);
  if (currentPM()->d_rewriteFilters.find(litName) != currentPM()->d_rewriteFilters.end()) {
    return currentPM()->d_rewriteFilters[litName];
  }

  return litName;
}

std::string ProofManager::sanitize(TNode node) {
  Assert (node.isVar() || node.isConst());

  std::string name = node.toString();
  if (node.isVar()) {
    std::replace(name.begin(), name.end(), ' ', '_');
  } else if (node.isConst()) {
    name.erase(std::remove(name.begin(), name.end(), '('), name.end());
    name.erase(std::remove(name.begin(), name.end(), ')'), name.end());
    name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
    name = "const" + name;
  }

  return name;
}

void ProofManager::traceDeps(TNode n) {
  Debug("cores") << "trace deps " << n << std::endl;
  if ((n.isConst() && n == NodeManager::currentNM()->mkConst<bool>(true)) ||
      (n.getKind() == kind::NOT && n[0] == NodeManager::currentNM()->mkConst<bool>(false))) {
    return;
  }
  if(d_inputCoreFormulas.find(n.toExpr()) != d_inputCoreFormulas.end()) {
    // originating formula was in core set
    Debug("cores") << " -- IN INPUT CORE LIST!" << std::endl;
    d_outputCoreFormulas.insert(n.toExpr());
  } else {
    Debug("cores") << " -- NOT IN INPUT CORE LIST!" << std::endl;
    if(d_deps.find(n) == d_deps.end()) {
      InternalError("Cannot trace dependence information back to input assertion:\n`%s'", n.toString().c_str());
    }
    Assert(d_deps.find(n) != d_deps.end());
    std::vector<Node> deps = (*d_deps.find(n)).second;
    for(std::vector<Node>::const_iterator i = deps.begin(); i != deps.end(); ++i) {
      Debug("cores") << " + tracing deps: " << n << " -deps-on- " << *i << std::endl;
      if( !(*i).isNull() ){
        traceDeps(*i);
      }
    }
  }
}

void ProofManager::traceUnsatCore() {
  Assert (options::unsatCores());
  d_satProof->constructProof();
  IdToSatClause used_lemmas;
  IdToSatClause used_inputs;
  d_satProof->collectClausesUsed(used_inputs,
                                 used_lemmas);

  if (options::dumpUsedInstLemmas())
    dumpUsedInstLemmas(used_lemmas);

  IdToSatClause::const_iterator it = used_inputs.begin();
  for(; it != used_inputs.end(); ++it) {
    Node node = d_cnfProof->getAssertionForClause(it->first);
    ProofRule rule = d_cnfProof->getProofRule(node);

    Debug("cores") << "core input assertion " << node << std::endl;
    Debug("cores") << "with proof rule " << rule << std::endl;
    if (rule == RULE_TSEITIN ||
        rule == RULE_GIVEN) {
      // trace dependences back to actual assertions
      // (this adds them to the unsat core)
      traceDeps(node);
    }
  }
}

void ProofManager::dumpUsedInstLemmas(IdToSatClause used_lemmas) {
  IdToSatClause::const_iterator it;

  Debug("dump-used-insts") << std::endl << "Dumping used instantiated lemmas:" << std::endl;
  for (it = used_lemmas.begin(); it != used_lemmas.end(); ++it) {

    std::set<Node> lemma;
    for(unsigned i = 0; i < it->second->size(); ++i) {
      prop::SatLiteral lit = (*(it->second))[i];
      Node node = ProofManager::getCnfProof()->getAtom(lit.getSatVariable());
      Expr atom = node.toExpr();
      if (atom.isConst()) {
        Assert (atom == utils::mkTrue());
        continue;
      }
      lemma.insert(lit.isNegated() ? node.notNode() : node);
    }

    std::set<Node>::iterator lemmaIt;
    if (d_instantiatedLemmas.find(lemma) != d_instantiatedLemmas.end()) {
      Debug("dump-used-insts") << "\t";
      for (lemmaIt = lemma.begin(); lemmaIt != lemma.end(); ++lemmaIt) {
        Debug("dump-used-insts") << *lemmaIt << " ";
      }
      Debug("dump-used-insts") << std::endl;
    }
    Debug("dump-used-insts") << std::endl << std::endl << std::endl;
  }
}

void ProofManager::addAssertion(Expr formula) {
  Debug("proof:pm") << "assert: " << formula << std::endl;
  d_inputFormulas.insert(formula);
}

void ProofManager::addCoreAssertion(Expr formula) {
  Debug("cores") << "assert: " << formula << std::endl;
  d_deps[Node::fromExpr(formula)]; // empty vector of deps
  d_inputCoreFormulas.insert(formula);
}

void ProofManager::addDependence(TNode n, TNode dep) {
  if(dep != n) {
    Debug("cores") << "dep: " << n << " : " << dep << std::endl;
    if( !dep.isNull() && d_deps.find(dep) == d_deps.end()) {
      Debug("cores") << "WHERE DID " << dep << " come from ??" << std::endl;
    }
    //Assert(d_deps.find(dep) != d_deps.end());
    d_deps[n].push_back(dep);
  }
}

void ProofManager::addUnsatCore(Expr formula) {
  Assert (d_inputCoreFormulas.find(formula) != d_inputCoreFormulas.end());
  d_outputCoreFormulas.insert(formula);
}

void ProofManager::setLogic(const LogicInfo& logic) {
  d_logic = logic;
}



LFSCProof::LFSCProof(SmtEngine* smtEngine,
                     LFSCCoreSatProof* sat,
                     LFSCCnfProof* cnf,
                     LFSCTheoryProofEngine* theory)
  : d_satProof(sat)
  , d_cnfProof(cnf)
  , d_theoryProof(theory)
  , d_smtEngine(smtEngine)
{}

void LFSCProof::toStream(std::ostream& out, const ProofLetMap& map) {
  Unreachable();
}

void LFSCProof::toStream(std::ostream& out) {

  Assert(options::bitblastMode() != theory::bv::BITBLAST_MODE_EAGER);

  d_satProof->constructProof();

  // collecting leaf clauses in resolution proof
  IdToSatClause used_lemmas;
  IdToSatClause used_inputs;
  d_satProof->collectClausesUsed(used_inputs,
                                 used_lemmas);

  IdToSatClause::iterator it2;
  Debug("pf::pm") << std::endl << "Used inputs: " << std::endl;
  for (it2 = used_inputs.begin(); it2 != used_inputs.end(); ++it2) {
    Debug("pf::pm") << "\t input = " << *(it2->second) << std::endl;
  }
  Debug("pf::pm") << std::endl;

  // Debug("pf::pm") << std::endl << "Used lemmas: " << std::endl;
  // for (it2 = used_lemmas.begin(); it2 != used_lemmas.end(); ++it2) {
  //   Debug("pf::pm") << "\t lemma = " << *(it2->second) << std::endl;
  // }
  // Debug("pf::pm") << std::endl;
  Debug("pf::pm") << std::endl << "Used lemmas: " << std::endl;
  for (it2 = used_lemmas.begin(); it2 != used_lemmas.end(); ++it2) {

    std::vector<Expr> clause_expr;
    for(unsigned i = 0; i < it2->second->size(); ++i) {
      prop::SatLiteral lit = (*(it2->second))[i];
      Expr atom = d_cnfProof->getAtom(lit.getSatVariable()).toExpr();
      if (atom.isConst()) {
        Assert (atom == utils::mkTrue());
        continue;
      }
      Expr expr_lit = lit.isNegated() ? atom.notExpr(): atom;
      clause_expr.push_back(expr_lit);
    }

    Debug("pf::pm") << "\t lemma " << it2->first << " = " << *(it2->second) << std::endl;
    Debug("pf::pm") << "\t";
    for (unsigned i = 0; i < clause_expr.size(); ++i) {
      Debug("pf::pm") << clause_expr[i] << " ";
    }
    Debug("pf::pm") << std::endl;
  }
  Debug("pf::pm") << std::endl;

  // collecting assertions that lead to the clauses being asserted
  NodeSet used_assertions;
  d_cnfProof->collectAssertionsForClauses(used_inputs, used_assertions);

  NodeSet::iterator it3;
  Debug("pf::pm") << std::endl << "Used assertions: " << std::endl;
  for (it3 = used_assertions.begin(); it3 != used_assertions.end(); ++it3)
    Debug("pf::pm") << "\t assertion = " << *it3 << std::endl;

  std::set<Node> atoms;
  NodePairSet rewrites;
  // collects the atoms in the clauses
  d_cnfProof->collectAtomsAndRewritesForLemmas(used_lemmas, atoms, rewrites);

  if (!rewrites.empty()) {
    Debug("pf::pm") << std::endl << "Rewrites used in lemmas: " << std::endl;
    NodePairSet::const_iterator rewriteIt;
    for (rewriteIt = rewrites.begin(); rewriteIt != rewrites.end(); ++rewriteIt) {
      Debug("pf::pm") << "\t" << rewriteIt->first << " --> " << rewriteIt->second << std::endl;
    }
    Debug("pf::pm") << std::endl << "Rewrite printing done" << std::endl;
  } else {
    Debug("pf::pm") << "No rewrites in lemmas found" << std::endl;
  }

  // The derived/unrewritten atoms may not have CNF literals required later on.
  // If they don't, add them.
  std::set<Node>::const_iterator it;
  for (it = atoms.begin(); it != atoms.end(); ++it) {
    Debug("pf::pm") << "Ensure literal for atom: " << *it << std::endl;
    if (!d_cnfProof->hasLiteral(*it)) {
      // For arithmetic: these literals are not normalized, causing an error in Arith.
      if (theory::Theory::theoryOf(*it) == theory::THEORY_ARITH) {
        d_cnfProof->ensureLiteral(*it, true); // This disables preregistration with the theory solver.
      } else {
        d_cnfProof->ensureLiteral(*it); // Normal method, with theory solver preregisteration.
      }
    }
  }

  d_cnfProof->collectAtomsForClauses(used_inputs, atoms);
  d_cnfProof->collectAtomsForClauses(used_lemmas, atoms);

  // collects the atoms in the assertions
  for (NodeSet::const_iterator it = used_assertions.begin();
       it != used_assertions.end(); ++it) {
    utils::collectAtoms(*it, atoms);
    // utils::collectAtoms(*it, newAtoms);
  }

  std::set<Node>::iterator atomIt;
  Debug("pf::pm") << std::endl << "Dumping atoms from lemmas, inputs and assertions: "
                  << std::endl << std::endl;
  for (atomIt = atoms.begin(); atomIt != atoms.end(); ++atomIt) {
    Debug("pf::pm") << "\tAtom: " << *atomIt << std::endl;
  }
  smt::SmtScope scope(d_smtEngine);
  std::ostringstream paren;
  out << "(check\n";
  out << " ;; Declarations\n";

  // declare the theory atoms
  Debug("pf::pm") << "LFSCProof::toStream: registering terms:" << std::endl;
  for(it = atoms.begin(); it != atoms.end(); ++it) {
    Debug("pf::pm") << "\tTerm: " << (*it).toExpr() << std::endl;
    d_theoryProof->registerTerm((*it).toExpr());
  }

  Debug("pf::pm") << std::endl << "Term registration done!" << std::endl << std::endl;

  Debug("pf::pm") << std::endl << "LFSCProof::toStream: starting to print assertions" << std::endl;

  // print out all the original assertions
  d_theoryProof->registerTermsFromAssertions();
  d_theoryProof->printSortDeclarations(out, paren);
  d_theoryProof->printTermDeclarations(out, paren);
  d_theoryProof->printAssertions(out, paren);

  Debug("pf::pm") << std::endl << "LFSCProof::toStream: print assertions DONE" << std::endl;

  out << "(: (holds cln)\n\n";

  // Have the theory proofs print deferred declarations, e.g. for skolem variables.
  out << " ;; Printing deferred declarations \n\n";
  d_theoryProof->printDeferredDeclarations(out, paren);

  d_theoryProof->finalizeBvConflicts(used_lemmas, out);
  ProofManager::getBitVectorProof()->calculateAtomsInBitblastingProof();

  out << "\n ;; Printing the global let map \n";

  ProofLetMap globalLetMap;
  if (options::lfscLetification()) {
    ProofManager::currentPM()->printGlobalLetMap(atoms, globalLetMap, out, paren);
  }

  out << " ;; Printing aliasing declarations \n\n";
  d_theoryProof->printAliasingDeclarations(out, paren);

  out << " ;; Rewrites for Lemmas \n";
  d_theoryProof->printLemmaRewrites(rewrites, out, paren);

  // print trust that input assertions are their preprocessed form
  printPreprocessedAssertions(used_assertions, out, paren, globalLetMap);

  // print mapping between theory atoms and internal SAT variables
  out << ";; Printing mapping from preprocessed assertions into atoms \n";
  d_cnfProof->printAtomMapping(atoms, out, paren, globalLetMap);

  Debug("pf::pm") << std::endl << "Printing cnf proof for clauses" << std::endl;

  IdToSatClause::const_iterator cl_it = used_inputs.begin();
  // print CNF conversion proof for each clause
  for (; cl_it != used_inputs.end(); ++cl_it) {
    d_cnfProof->printCnfProofForClause(cl_it->first, cl_it->second, out, paren);
  }

  Debug("pf::pm") << std::endl << "Printing cnf proof for clauses DONE" << std::endl;

  Debug("pf::pm") << "Proof manager: printing theory lemmas" << std::endl;
  d_theoryProof->printTheoryLemmas(used_lemmas, out, paren, globalLetMap);
  Debug("pf::pm") << "Proof manager: printing theory lemmas DONE!" << std::endl;

  if (options::bitblastMode() == theory::bv::BITBLAST_MODE_EAGER && ProofManager::getBitVectorProof()) {
    // print actual resolution proof
    // d_satProof->printResolutions(out, paren);
    ProofManager::getBitVectorProof()->getSatProof()->printResolutionEmptyClause(out, paren);
    paren <<")))\n;;";
    out << paren.str();
    out << "\n";
  } else {
    // print actual resolution proof
    d_satProof->printResolutions(out, paren);
    d_satProof->printResolutionEmptyClause(out, paren);
    paren <<")))\n;;";
    out << paren.str();
    out << "\n";
  }
}

void LFSCProof::printPreprocessedAssertions(const NodeSet& assertions,
                                            std::ostream& os,
                                            std::ostream& paren,
                                            ProofLetMap& globalLetMap) {
  os << "\n ;; In the preprocessor we trust \n";
  NodeSet::const_iterator it = assertions.begin();
  NodeSet::const_iterator end = assertions.end();

  Debug("pf::pm") << "LFSCProof::printPreprocessedAssertions starting" << std::endl;

  for (; it != end; ++it) {
    os << "(th_let_pf _ ";

    //TODO
    os << "(trust_f ";
    ProofManager::currentPM()->getTheoryProofEngine()->printTheoryTerm((*it).toExpr(), os, globalLetMap);
    os << ") ";

    os << "(\\ "<< ProofManager::getPreprocessedAssertionName(*it, "") << "\n";
    paren << "))";
  }

  os << "\n";
}

//---from Morgan---

bool ProofManager::hasOp(TNode n) const {
  return d_bops.find(n) != d_bops.end();
}

Node ProofManager::lookupOp(TNode n) const {
  std::map<Node, Node>::const_iterator i = d_bops.find(n);
  Assert(i != d_bops.end());
  return (*i).second;
}

Node ProofManager::mkOp(TNode n) {
  Trace("mgd-pm-mkop") << "MkOp : " << n << " " << n.getKind() << std::endl;
  if(n.getKind() != kind::BUILTIN) {
    return n;
  }

  Node& op = d_ops[n];
  if(op.isNull()) {
    Assert((n.getConst<Kind>() == kind::SELECT) || (n.getConst<Kind>() == kind::STORE));

    Debug("mgd-pm-mkop") << "making an op for " << n << "\n";

    std::stringstream ss;
    ss << n;
    std::string s = ss.str();
    Debug("mgd-pm-mkop") << " : " << s << std::endl;
    std::vector<TypeNode> v;
    v.push_back(NodeManager::currentNM()->integerType());
    if(n.getConst<Kind>() == kind::SELECT) {
      v.push_back(NodeManager::currentNM()->integerType());
      v.push_back(NodeManager::currentNM()->integerType());
    } else if(n.getConst<Kind>() == kind::STORE) {
      v.push_back(NodeManager::currentNM()->integerType());
      v.push_back(NodeManager::currentNM()->integerType());
      v.push_back(NodeManager::currentNM()->integerType());
    }
    TypeNode type = NodeManager::currentNM()->mkFunctionType(v);
    Debug("mgd-pm-mkop") << "typenode is: " << type << "\n";
    op = NodeManager::currentNM()->mkSkolem(s, type, " ignore", NodeManager::SKOLEM_NO_NOTIFY);
    d_bops[op] = n;
  }
  Debug("mgd-pm-mkop") << "returning the op: " << op << "\n";
  return op;
}
//---end from Morgan---

bool ProofManager::wasPrinted(const Type& type) const {
  return d_printedTypes.find(type) != d_printedTypes.end();
}

void ProofManager::markPrinted(const Type& type) {
  d_printedTypes.insert(type);
}

void ProofManager::addRewriteFilter(const std::string &original, const std::string &substitute) {
  d_rewriteFilters[original] = substitute;
}

void ProofManager::clearRewriteFilters() {
  d_rewriteFilters.clear();
}

std::ostream& operator<<(std::ostream& out, CVC4::ProofRule k) {
  switch(k) {
  case RULE_GIVEN:
    out << "RULE_GIVEN";
    break;
  case RULE_DERIVED:
    out << "RULE_DERIVED";
    break;
  case RULE_RECONSTRUCT:
    out << "RULE_RECONSTRUCT";
    break;
  case RULE_TRUST:
    out << "RULE_TRUST";
    break;
  case RULE_INVALID:
    out << "RULE_INVALID";
    break;
  case RULE_CONFLICT:
    out << "RULE_CONFLICT";
    break;
  case RULE_TSEITIN:
    out << "RULE_TSEITIN";
    break;
  case RULE_SPLIT:
    out << "RULE_SPLIT";
    break;
  case RULE_ARRAYS_EXT:
    out << "RULE_ARRAYS";
    break;
  case RULE_ARRAYS_ROW:
    out << "RULE_ARRAYS";
    break;
  default:
    out << "ProofRule Unknown! [" << unsigned(k) << "]";
  }

  return out;
}

void ProofManager::registerRewrite(unsigned ruleId, Node original, Node result){
  Assert (currentPM()->d_theoryProof != NULL);
  currentPM()->d_rewriteLog.push_back(RewriteLogEntry(ruleId, original, result));
}

void ProofManager::clearRewriteLog() {
  Assert (currentPM()->d_theoryProof != NULL);
  currentPM()->d_rewriteLog.clear();
}

std::vector<RewriteLogEntry> ProofManager::getRewriteLog() {
  return currentPM()->d_rewriteLog;
}

void ProofManager::dumpRewriteLog() const {
  Debug("pf::rr") << "Dumpign rewrite log:" << std::endl;

  for (unsigned i = 0; i < d_rewriteLog.size(); ++i) {
    Debug("pf::rr") << "\tRule " << d_rewriteLog[i].getRuleId()
                    << ": "
                    << d_rewriteLog[i].getOriginal()
                    << " --> "
                    << d_rewriteLog[i].getResult() << std::endl;
  }
}

void bind(Expr term, ProofLetMap& map, Bindings& letOrder) {
  ProofLetMap::iterator it = map.find(term);
  if (it != map.end())
    return;

  for (unsigned i = 0; i < term.getNumChildren(); ++i)
    bind(term[i], map, letOrder);

  // Special case: chain operators. If we have and(a,b,c), it will be prineted as and(a,and(b,c)).
  // The subterm and(b,c) may repeat elsewhere, so we need to bind it, too.
  Kind k = term.getKind();
  if (((k == kind::OR) || (k == kind::AND)) && term.getNumChildren() > 2) {
    Node currentExpression = term[term.getNumChildren() - 1];
    for (int i = term.getNumChildren() - 2; i >= 0; --i) {
      NodeBuilder<> builder(k);
      builder << term[i];
      builder << currentExpression.toExpr();
      currentExpression = builder;
      bind(currentExpression.toExpr(), map, letOrder);
    }
  } else {
    unsigned newId = ProofLetCount::newId();
    ProofLetCount letCount(newId);
    map[term] = letCount;
    letOrder.push_back(LetOrderElement(term, newId));
  }
}

void ProofManager::printGlobalLetMap(std::set<Node>& atoms,
                                     ProofLetMap& letMap,
                                     std::ostream& out,
                                     std::ostringstream& paren) {
  Bindings letOrder;
  std::set<Node>::const_iterator atom;
  for (atom = atoms.begin(); atom != atoms.end(); ++atom) {
    bind(atom->toExpr(), letMap, letOrder);
  }

  // TODO: give each theory a chance to add atoms. For now, just query BV directly...
  const std::set<Node>* additionalAtoms = ProofManager::getBitVectorProof()->getAtomsInBitblastingProof();
  for (atom = additionalAtoms->begin(); atom != additionalAtoms->end(); ++atom) {
    bind(atom->toExpr(), letMap, letOrder);
  }

  for (unsigned i = 0; i < letOrder.size(); ++i) {
    Expr currentExpr = letOrder[i].expr;
    unsigned letId = letOrder[i].id;
    ProofLetMap::iterator it = letMap.find(currentExpr);
    Assert(it != letMap.end());
    out << "\n(@ let" << letId << " ";
    d_theoryProof->printBoundTerm(currentExpr, out, letMap);
    paren << ")";
    it->second.increment();
  }

  out << std::endl << std::endl;
}

void ProofManager::markInstantiatedLemma(Node lemma) {
  Debug("pf::pm") << "ProofManager::markInstantiatedLemma( " << lemma << " )" << std::endl;

  std::set<Node> lemmaConjunction;

  if (lemma.getKind() == kind::OR) {
    for (unsigned i = 0; i < lemma.getNumChildren(); ++i) {
      lemmaConjunction.insert(lemma[i]);
    }
  } else {
    lemmaConjunction.insert(lemma);
  }

  currentPM()->d_instantiatedLemmas.insert(lemmaConjunction);
}

} /* CVC4  namespace */
