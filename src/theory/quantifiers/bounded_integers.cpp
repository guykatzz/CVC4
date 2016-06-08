/*********************                                                        */
/*! \file bounded_integers.cpp
 ** \verbatim
 ** Top contributors (to current version):
 **   Morgan Deters, Andrew Reynolds, Tim King
 ** This file is part of the CVC4 project.
 ** Copyright (c) 2009-2016 by the authors listed in the file AUTHORS
 ** in the top-level source directory) and their institutional affiliations.
 ** All rights reserved.  See the file COPYING in the top-level source
 ** directory for licensing information.\endverbatim
 **
 ** \brief Bounded integers module
 **
 ** This class manages integer bounds for quantifiers
 **/

#include "options/quantifiers_options.h"
#include "theory/quantifiers/bounded_integers.h"
#include "theory/quantifiers/first_order_model.h"
#include "theory/quantifiers/model_engine.h"
#include "theory/quantifiers/quant_util.h"
#include "theory/quantifiers/term_database.h"

using namespace CVC4;
using namespace std;
using namespace CVC4::theory;
using namespace CVC4::theory::quantifiers;
using namespace CVC4::kind;


BoundedIntegers::IntRangeModel::IntRangeModel(BoundedIntegers * bi, Node r, context::Context* c, context::Context* u, bool isProxy) : d_bi(bi),
      d_range(r), d_curr_max(-1), d_lit_to_range(u), d_range_assertions(c), d_has_range(c,false), d_curr_range(c,-1), d_ranges_proxied(u) { 
  if( options::fmfBoundIntLazy() ){
    d_proxy_range = isProxy ? r : NodeManager::currentNM()->mkSkolem( "pbir", r.getType() );
  }else{
    d_proxy_range = r;
  }
  if( !isProxy ){
    Trace("bound-int") << "Introduce proxy " << d_proxy_range << " for " << d_range << std::endl;
  }
}

void BoundedIntegers::IntRangeModel::initialize() {
  //add initial split lemma
  Node ltr = NodeManager::currentNM()->mkNode( LT, d_proxy_range, NodeManager::currentNM()->mkConst( Rational(0) ) );
  ltr = Rewriter::rewrite( ltr );
  Trace("bound-int-lemma") << " *** bound int: initial split on " << ltr << std::endl;
  d_bi->getQuantifiersEngine()->getOutputChannel().split( ltr );
  Node ltr_lit = ltr.getKind()==NOT ? ltr[0] : ltr;
  d_range_literal[-1] = ltr_lit;
  d_lit_to_range[ltr_lit] = -1;
  d_lit_to_pol[ltr_lit] = ltr.getKind()!=NOT;
  //register with bounded integers
  Trace("bound-int-debug") << "Literal " << ltr_lit << " is literal for " << d_range << std::endl;
  d_bi->addLiteralFromRange(ltr_lit, d_range);
}

void BoundedIntegers::IntRangeModel::assertNode(Node n) {
  bool pol = n.getKind()!=NOT;
  Node nlit = n.getKind()==NOT ? n[0] : n;
  if( d_lit_to_range.find( nlit )!=d_lit_to_range.end() ){
    int vrange = d_lit_to_range[nlit];
    Trace("bound-int-assert") << "With polarity = " << pol << " (req "<< d_lit_to_pol[nlit] << ")";
    Trace("bound-int-assert") << ", found literal " << nlit;
    Trace("bound-int-assert") << ", it is bound literal " << vrange << " for " << d_range << std::endl;
    d_range_assertions[nlit] = (pol==d_lit_to_pol[nlit]);
    if( pol!=d_lit_to_pol[nlit] ){
      //check if we need a new split?
      if( !d_has_range ){
        bool needsRange = true;
        for( NodeIntMap::iterator it = d_lit_to_range.begin(); it != d_lit_to_range.end(); ++it ){
          if( d_range_assertions.find( (*it).first )==d_range_assertions.end() ){
            Trace("bound-int-debug") << "Does not need range because of " << (*it).first << std::endl;
            needsRange = false;
            break;
          }
        }
        if( needsRange ){
          allocateRange();
        }
      }
    }else{
      if (!d_has_range || vrange<d_curr_range ){
        Trace("bound-int-bound") << "Successfully bound " << d_range << " to " << vrange << std::endl;
        d_curr_range = vrange;
      }
      //set the range
      d_has_range = true;
    }
  }else{
    Message() << "Could not find literal " << nlit << " for range " << d_range << std::endl;
    exit(0);
  }
}

void BoundedIntegers::IntRangeModel::allocateRange() {
  d_curr_max++;
  int newBound = d_curr_max;
  Trace("bound-int-proc") << "Allocate range bound " << newBound << " for " << d_range << std::endl;
  //TODO: newBound should be chosen in a smarter way
  Node ltr = NodeManager::currentNM()->mkNode( LEQ, d_proxy_range, NodeManager::currentNM()->mkConst( Rational(newBound) ) );
  ltr = Rewriter::rewrite( ltr );
  Trace("bound-int-lemma") << " *** bound int: split on " << ltr << std::endl;
  d_bi->getQuantifiersEngine()->getOutputChannel().split( ltr );
  Node ltr_lit = ltr.getKind()==NOT ? ltr[0] : ltr;
  d_range_literal[newBound] = ltr_lit;
  d_lit_to_range[ltr_lit] = newBound;
  d_lit_to_pol[ltr_lit] = ltr.getKind()!=NOT;
  //register with bounded integers
  d_bi->addLiteralFromRange(ltr_lit, d_range);
}

Node BoundedIntegers::IntRangeModel::getNextDecisionRequest() {
  //request the current cardinality as a decision literal, if not already asserted
  for( NodeIntMap::iterator it = d_lit_to_range.begin(); it != d_lit_to_range.end(); ++it ){
    int i = (*it).second;
    if( !d_has_range || i<d_curr_range ){
      Node rn = (*it).first;
      Assert( !rn.isNull() );
      if( d_range_assertions.find( rn )==d_range_assertions.end() ){
        if (!d_lit_to_pol[rn]) {
          rn = rn.negate();
        }
        Trace("bound-int-dec-debug") << "For " << d_range << ", make decision " << rn << " to make range " << i << std::endl;
        return rn;
      }
    }
  }
  return Node::null();
}

bool BoundedIntegers::IntRangeModel::proxyCurrentRange() {
  //Trace("model-engine") << "Range(" << d_range << ") currently is " << d_curr_max.get() << std::endl;
  if( d_range!=d_proxy_range ){
    //int curr = d_curr_range.get();
    int curr = d_curr_max;
    if( d_ranges_proxied.find( curr )==d_ranges_proxied.end() ){
      d_ranges_proxied[curr] = true;
      Assert( d_range_literal.find( curr )!=d_range_literal.end() );
      Node lem = NodeManager::currentNM()->mkNode( IFF, d_range_literal[curr].negate(),
                   NodeManager::currentNM()->mkNode( LEQ, d_range, NodeManager::currentNM()->mkConst( Rational(curr) ) ) );
      Trace("bound-int-lemma") << "*** bound int : proxy lemma : " << lem << std::endl;
      d_bi->getQuantifiersEngine()->addLemma( lem );
      return true;
    }
  }
  return false;
}





BoundedIntegers::BoundedIntegers(context::Context* c, QuantifiersEngine* qe) :
QuantifiersModule(qe), d_assertions(c){

}

BoundedIntegers::~BoundedIntegers() { 
  for( std::map< Node, RangeModel * >::iterator it = d_rms.begin(); it != d_rms.end(); ++it ){
    delete it->second;
  }
}

void BoundedIntegers::presolve() {
  d_bnd_it.clear();
}

bool BoundedIntegers::isBound( Node f, Node v ) {
  return std::find( d_set[f].begin(), d_set[f].end(), v )!=d_set[f].end();
}

bool BoundedIntegers::hasNonBoundVar( Node f, Node b ) {
  if( b.getKind()==BOUND_VARIABLE ){
    if( !isBound( f, b ) ){
      return true;
    }
  }else{
    for( unsigned i=0; i<b.getNumChildren(); i++ ){
      if( hasNonBoundVar( f, b[i] ) ){
        return true;
      }
    }
  }
  return false;
}

void BoundedIntegers::processLiteral( Node q, Node lit, bool pol,
                                      std::map< Node, unsigned >& bound_lit_type_map,
                                      std::map< int, std::map< Node, Node > >& bound_lit_map,
                                      std::map< int, std::map< Node, bool > >& bound_lit_pol_map,
                                      std::map< int, std::map< Node, Node > >& bound_int_range_term ) {
  if( lit.getKind()==GEQ ){
    if( lit[0].getType().isInteger() ){
      std::map< Node, Node > msum;
      if( QuantArith::getMonomialSumLit( lit, msum ) ){
        Trace("bound-int-debug") << "Literal (polarity = " << pol << ") " << lit << " is monomial sum : " << std::endl;
        QuantArith::debugPrintMonomialSum( msum, "bound-int-debug" );
        for( std::map< Node, Node >::iterator it = msum.begin(); it != msum.end(); ++it ){
          if ( !it->first.isNull() && it->first.getKind()==BOUND_VARIABLE && !isBound( q, it->first ) ){
            Node veq;
            if( QuantArith::isolate( it->first, msum, veq, GEQ )!=0 ){
              Node n1 = veq[0];
              Node n2 = veq[1];
              if(pol){
                //flip
                n1 = veq[1];
                n2 = veq[0];
                if( n1.getKind()==BOUND_VARIABLE ){
                  n2 = QuantArith::offset( n2, 1 );
                }else{
                  n1 = QuantArith::offset( n1, -1 );
                }
                veq = NodeManager::currentNM()->mkNode( GEQ, n1, n2 );
              }
              Trace("bound-int-debug") << "Isolated for " << it->first << " : (" << n1 << " >= " << n2 << ")" << std::endl;
              Node t = n1==it->first ? n2 : n1;
              if( !hasNonBoundVar( q, t ) ) {
                Trace("bound-int-debug") << "The bound is relevant." << std::endl;
                int loru = n1==it->first ? 0 : 1;
                bound_lit_type_map[it->first] = BOUND_INT_RANGE;
                bound_int_range_term[loru][it->first] = t;
                bound_lit_map[loru][it->first] = lit;
                bound_lit_pol_map[loru][it->first] = pol;
              }else{
                Trace("bound-int-debug") << "The term " << t << " has non-bound variable." << std::endl;
              }
            }
          }
        }
      }
    }
  }else if( lit.getKind()==MEMBER ){
    //TODO: enable this when sets models are fixed
    /*
    if( !pol && lit[0].getKind()==BOUND_VARIABLE && !isBound( q, lit[0] ) && !lit[1].hasBoundVar() ){
      Trace("bound-int-debug") << "Literal (polarity = " << pol << ") " << lit << " is membership." << std::endl;
      bound_lit_type_map[lit[0]] = BOUND_SET_MEMBER;
      bound_lit_map[0][lit[0]] = lit;
      bound_lit_pol_map[0][lit[0]] = pol;
    }
    */
  }else if( lit.getKind()==LEQ || lit.getKind()==LT || lit.getKind()==GT ) {
    Message() << "BoundedIntegers : Bad kind for literal : " << lit << std::endl;
  }
}

void BoundedIntegers::process( Node q, Node n, bool pol,
                               std::map< Node, unsigned >& bound_lit_type_map,
                               std::map< int, std::map< Node, Node > >& bound_lit_map,
                               std::map< int, std::map< Node, bool > >& bound_lit_pol_map,
                               std::map< int, std::map< Node, Node > >& bound_int_range_term ){
  if( (n.getKind()==OR && pol) || (n.getKind()==AND && !pol) ){
    for( unsigned i=0; i<n.getNumChildren(); i++) {
      process( q, n[i], pol, bound_lit_type_map, bound_lit_map, bound_lit_pol_map, bound_int_range_term );
    }
  }else if( n.getKind()==NOT ){
    process( q, n[0], !pol, bound_lit_type_map, bound_lit_map, bound_lit_pol_map, bound_int_range_term );
  }else {
    processLiteral( q, n, pol, bound_lit_type_map, bound_lit_map, bound_lit_pol_map, bound_int_range_term );
  }
}

bool BoundedIntegers::needsCheck( Theory::Effort e ) {
  return e==Theory::EFFORT_LAST_CALL;
}

void BoundedIntegers::check( Theory::Effort e, unsigned quant_e ) {
  if( quant_e==QuantifiersEngine::QEFFORT_STANDARD ){
    Trace("bint-engine") << "---Bounded Integers---" << std::endl;
    bool addedLemma = false;
    //make sure proxies are up-to-date with range
    for( unsigned i=0; i<d_ranges.size(); i++) {
      if( d_rms[d_ranges[i]]->proxyCurrentRange() ){
        addedLemma = true;
      }
    }
    Trace("bint-engine") << "   addedLemma = " << addedLemma << std::endl;
  }
}


void BoundedIntegers::addLiteralFromRange( Node lit, Node r ) {
  d_lit_to_ranges[lit].push_back(r);
  //check if it is already asserted?
  if(d_assertions.find(lit)!=d_assertions.end()){
    d_rms[r]->assertNode( d_assertions[lit] ? lit : lit.negate() );
  }
}

void BoundedIntegers::setBoundedVar( Node q, Node v, unsigned bound_type ) {
  d_bound_type[q][v] = bound_type;
  d_set_nums[q][v] = d_set[q].size();
  d_set[q].push_back( v );
  Trace("bound-int-var") << "Bound variable #" << d_set_nums[q][v] << " : " << v << std::endl; 
}

void BoundedIntegers::registerQuantifier( Node f ) {
  Trace("bound-int") << "Register quantifier " << f << std::endl;
  
  bool success;
  do{
    std::map< Node, unsigned > bound_lit_type_map;
    std::map< int, std::map< Node, Node > > bound_lit_map;
    std::map< int, std::map< Node, bool > > bound_lit_pol_map;
    std::map< int, std::map< Node, Node > > bound_int_range_term;
    success = false;
    process( f, f[1], true, bound_lit_type_map, bound_lit_map, bound_lit_pol_map, bound_int_range_term );
    //for( std::map< Node, Node >::iterator it = d_bounds[0][f].begin(); it != d_bounds[0][f].end(); ++it ){
    for( std::map< Node, unsigned >::iterator it = bound_lit_type_map.begin(); it != bound_lit_type_map.end(); ++it ){
      Node v = it->first;
      if( !isBound( f, v ) ){
        bool setBoundVar = false;
        if( it->second==BOUND_INT_RANGE ){
          //must have both
          if( bound_lit_map[0].find( v )!=bound_lit_map[0].end() && bound_lit_map[1].find( v )!=bound_lit_map[1].end() ){
            setBoundedVar( f, v, BOUND_INT_RANGE );
            setBoundVar = true;
            success = true;
            for( unsigned b=0; b<2; b++ ){
              //set the bounds
              Assert( bound_int_range_term[b].find( v )!=bound_int_range_term[b].end() );
              d_bounds[b][f][v] = bound_int_range_term[b][v];
            }
            Node r = NodeManager::currentNM()->mkNode( MINUS, d_bounds[1][f][v], d_bounds[0][f][v] );
            d_range[f][v] = Rewriter::rewrite( r );
            Trace("bound-int") << "Variable " << v << " is bound because of int range literals " << bound_lit_map[0][v] << " and " << bound_lit_map[1][v] << std::endl;
          }
        }else if( it->second==BOUND_SET_MEMBER ){
          setBoundedVar( f, v, BOUND_SET_MEMBER );
          setBoundVar = true;
          d_setm_range[f][v] = bound_lit_map[0][v][1];
          Trace("bound-int") << "Variable " << v << " is bound because of set membership literal " << bound_lit_map[0][v] << std::endl;
        }
        if( setBoundVar ){
          //set Attributes on literals
          for( unsigned b=0; b<2; b++ ){
            if( bound_lit_map[b].find( v )!=bound_lit_map[b].end() ){
              Assert( bound_lit_pol_map[b].find( v )!=bound_lit_pol_map[b].end() );
              BoundIntLitAttribute bila;
              bound_lit_map[b][v].setAttribute( bila, bound_lit_pol_map[b][v] ? 1 : 0 );
            }else{
              Assert( it->second!=BOUND_INT_RANGE );
            }
          }
        }
      }
    }
  }while( success );
  
  Trace("bound-int") << "Bounds are : " << std::endl;
  for( unsigned i=0; i<d_set[f].size(); i++) {
    Node v = d_set[f][i];
    if( d_bound_type[f][v]==BOUND_INT_RANGE ){
      Trace("bound-int") << "  " << d_bounds[0][f][v] << " <= " << v << " <= " << d_bounds[1][f][v] << " (range is " << d_range[f][v] << ")" << std::endl;
    }else if( d_bound_type[f][v]==BOUND_SET_MEMBER ){
      Trace("bound-int") << "  " << v << " in " << d_setm_range[f][v] << std::endl;
    }
  }
  
  bool bound_success = true;
  for( unsigned i=0; i<f[0].getNumChildren(); i++) {
    if( d_bound_type[f].find( f[0][i] )==d_bound_type[f].end() ){
      TypeNode tn = f[0][i].getType();
      if( !tn.isSort() && !getTermDatabase()->mayComplete( tn ) ){
        Trace("bound-int-warn") << "Warning : Bounded Integers : Due to quantification on " << f[0][i] << ", could not find bounds for " << f << std::endl;
        bound_success = false;
        break;
      }
    }
  }
  
  if( bound_success ){
    d_bound_quants.push_back( f );
    for( unsigned i=0; i<d_set[f].size(); i++) {
      Node v = d_set[f][i];
      if( d_bound_type[f][v]==BOUND_INT_RANGE || d_bound_type[f][v]==BOUND_SET_MEMBER ){
        Node r;
        if( d_bound_type[f][v]==BOUND_INT_RANGE ){
          r = d_range[f][v];
        }else if( d_bound_type[f][v]==BOUND_SET_MEMBER ){
          r = NodeManager::currentNM()->mkNode( CARD, d_setm_range[f][v] );
        }
        bool isProxy = false;
        if( r.hasBoundVar() ){
          //introduce a new bound
          Node new_range = NodeManager::currentNM()->mkSkolem( "bir", r.getType(), "bound for term" );
          d_nground_range[f][v] = d_range[f][v];
          d_range[f][v] = new_range;
          r = new_range;
          isProxy = true;
        }
        if( !r.isConst() ){
          if( std::find(d_ranges.begin(), d_ranges.end(), r)==d_ranges.end() ){
            Trace("bound-int") << "For " << v << ", bounded Integer Module will try to minimize : " << r << std::endl;
            d_ranges.push_back( r );
            d_rms[r] = new IntRangeModel( this, r, d_quantEngine->getSatContext(), d_quantEngine->getUserContext(), isProxy );
            d_rms[r]->initialize();
          }
        }
      }
    }
  }
}

void BoundedIntegers::assertNode( Node n ) {
  Trace("bound-int-assert") << "Assert " << n << std::endl;
  Node nlit = n.getKind()==NOT ? n[0] : n;
  if( d_lit_to_ranges.find(nlit)!=d_lit_to_ranges.end() ){
    Trace("bound-int-assert") << "This is the bounding literal for " << d_lit_to_ranges[nlit].size() << " ranges." << std::endl;
    for( unsigned i=0; i<d_lit_to_ranges[nlit].size(); i++) {
      Node r = d_lit_to_ranges[nlit][i];
      Trace("bound-int-assert") << "  ...this is a bounding literal for " << r << std::endl;
      d_rms[r]->assertNode( n );
    }
  }
  d_assertions[nlit] = n.getKind()!=NOT;
}

Node BoundedIntegers::getNextDecisionRequest() {
  Trace("bound-int-dec-debug") << "bi: Get next decision request?" << std::endl;
  for( unsigned i=0; i<d_ranges.size(); i++) {
    Node d = d_rms[d_ranges[i]]->getNextDecisionRequest();
    if (!d.isNull()) {
      bool polLit = d.getKind()!=NOT;
      Node lit = d.getKind()==NOT ? d[0] : d;
      bool value;
      if( d_quantEngine->getValuation().hasSatValue( lit, value ) ) {
        if( value==polLit ){
          Trace("bound-int-dec-debug") << "...already asserted properly." << std::endl;
          //already true, we're already fine
        }else{
          Trace("bound-int-dec-debug") << "...already asserted with wrong polarity, re-assert." << std::endl;
          assertNode( d.negate() );
          i--;
        }
      }else{
        Trace("bound-int-dec") << "Bounded Integers : Decide " << d << std::endl;
        return d;
      }
    }
  }
  Trace("bound-int-dec-debug") << "No decision request." << std::endl;
  return Node::null();
}

unsigned BoundedIntegers::getBoundVarType( Node q, Node v ) {
  std::map< Node, unsigned >::iterator it = d_bound_type[q].find( v );
  if( it==d_bound_type[q].end() ){
    return BOUND_NONE;
  }else{
    return it->second;
  }
}

void BoundedIntegers::getBounds( Node f, Node v, RepSetIterator * rsi, Node & l, Node & u ) {
  l = d_bounds[0][f][v];
  u = d_bounds[1][f][v];
  if( d_nground_range[f].find(v)!=d_nground_range[f].end() ){
    //get the substitution
    std::vector< Node > vars;
    std::vector< Node > subs;
    if( getRsiSubsitution( f, v, vars, subs, rsi ) ){
      u = u.substitute( vars.begin(), vars.end(), subs.begin(), subs.end() );
      l = l.substitute( vars.begin(), vars.end(), subs.begin(), subs.end() );
    }else{
      u = Node::null();
      l = Node::null();
    }
  }
}

void BoundedIntegers::getBoundValues( Node f, Node v, RepSetIterator * rsi, Node & l, Node & u ) {
  getBounds( f, v, rsi, l, u );
  Trace("bound-int-rsi") << "Get value in model for..." << l << " and " << u << std::endl;
  if( !l.isNull() ){
    l = d_quantEngine->getModel()->getCurrentModelValue( l );
  }
  if( !u.isNull() ){
    u = d_quantEngine->getModel()->getCurrentModelValue( u );
  }
  Trace("bound-int-rsi") << "Value is " << l << " ... " << u << std::endl;
  return;
}

bool BoundedIntegers::isGroundRange( Node q, Node v ) {
  if( isBoundVar(q,v) ){
    if( d_bound_type[q][v]==BOUND_INT_RANGE ){
      return !getLowerBound(q,v).hasBoundVar() && !getUpperBound(q,v).hasBoundVar();
    }else if( d_bound_type[q][v]==BOUND_SET_MEMBER ){
      return !d_setm_range[q][v].hasBoundVar();
    }
  }
  return false;
}

Node BoundedIntegers::getSetRange( Node q, Node v, RepSetIterator * rsi ) {
  Node sr = d_setm_range[q][v];
  if( d_nground_range[q].find(v)!=d_nground_range[q].end() ){
    //get the substitution
    std::vector< Node > vars;
    std::vector< Node > subs;
    if( getRsiSubsitution( q, v, vars, subs, rsi ) ){
      sr = sr.substitute( vars.begin(), vars.end(), subs.begin(), subs.end() );
    }else{
      sr = Node::null();
    }
  }
  return sr;
}

Node BoundedIntegers::getSetRangeValue( Node q, Node v, RepSetIterator * rsi ) {
  Node sr = getSetRange( q, v, rsi );
  if( !sr.isNull() ){
    Trace("bound-int-rsi") << "Get value in model for..." << sr << std::endl;
    sr = d_quantEngine->getModel()->getCurrentModelValue( sr );
    Trace("bound-int-rsi") << "Value is " << sr << std::endl;
  }
  return sr;
}

bool BoundedIntegers::getRsiSubsitution( Node q, Node v, std::vector< Node >& vars, std::vector< Node >& subs, RepSetIterator * rsi ) {

  Trace("bound-int-rsi") << "Get bound value in model of variable " << v << std::endl;
  Assert( d_set_nums[q].find( v )!=d_set_nums[q].end() );
  int vindex = d_set_nums[q][v];
  Assert( d_set_nums[q][v]==vindex );
  Trace("bound-int-rsi-debug") << "  index order is " << vindex << std::endl;
  //must take substitution for all variables that are iterating at higher level
  for( int i=0; i<vindex; i++) {
    Assert( d_set_nums[q][d_set[q][i]]==i );
    Trace("bound-int-rsi") << "Look up the value for " << d_set[q][i] << " " << i << std::endl;
    int v = rsi->getVariableOrder( i );
    Assert( q[0][v]==d_set[q][i] );
    Node t = rsi->getCurrentTerm( v );
    Trace("bound-int-rsi") << "term : " << t << std::endl;
    vars.push_back( d_set[q][i] );
    subs.push_back( t );
  }
  
  //check if it has been instantiated
  if( !vars.empty() && !d_bnd_it[q][v].hasInstantiated(subs) ){
    if( d_bound_type[q][v]==BOUND_INT_RANGE ){
      //must add the lemma
      Node nn = d_nground_range[q][v];
      nn = nn.substitute( vars.begin(), vars.end(), subs.begin(), subs.end() );
      Node lem = NodeManager::currentNM()->mkNode( LEQ, nn, d_range[q][v] );
      Trace("bound-int-lemma") << "*** Add lemma to minimize instantiated non-ground term " << lem << std::endl;
      d_quantEngine->getOutputChannel().lemma(lem, false, true);
    }else{
      //TODO : sets
    }
    return false;
  }else{
    return true;
  }
}

