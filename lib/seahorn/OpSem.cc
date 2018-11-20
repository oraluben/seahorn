#include "seahorn/OpSem.hh"

using namespace seahorn;

namespace
{
  struct OpSemBase
  {
    SymStore &m_s;
    ExprFactory &m_efac;
    IntLightOpSem &m_sem;


    OpSemBase (SymStore &s, IntLightOpSem &sem) :
      m_s(s), m_efac (s.getExprFactory ()), m_sem (sem) {}

    void read (const Value &v)
    {if (m_sem.isTracked (v)) m_s.read (symb (v));}

    Expr symb (const Value &I) {return m_sem.symb (I);}
    Expr lookup (const Value &v) {return m_sem.lookup (m_s, v);}
    Expr havoc (const Value &v)
    {return m_sem.isTracked (v) ? m_s.havoc (symb (v)) : Expr (0);}
  };


  struct OpSemVisitor : public InstVisitor<OpSemVisitor> ,
                          OpSemBase
  {
    OpSemVisitor (SymStore &s, IntLightOpSem &sem) : OpSemBase (s, sem) {}

    void visitInstruction (Instruction &I) {havoc (I);}

    void visitPHINode (PHINode &I) {/* do nothing */}
    void visitReturnInst (ReturnInst &I) {lookup (*I.getOperand (0));}

    void visitBranchInst (BranchInst &I)
    {if (I.isConditional ()) lookup (*I.getOperand (0));}

    void visitBinaryOperator (BinaryOperator &I)
    {
      this->visitInstruction (I);
      lookup (*I.getOperand (0));
      lookup (*I.getOperand (1));
    }

    void visitCmpInst (CmpInst &I)
    {
      this->visitInstruction (I);
      lookup (*I.getOperand (0));
      lookup (*I.getOperand (1));
    }

    void visitCastInst (CastInst &I)
    {
      this->visitInstruction (I);
      lookup (*I.getOperand (0));
    }
  };

  struct OpSemPhiVisitor : public InstVisitor<OpSemPhiVisitor>,
                             OpSemBase
  {
    const BasicBlock &m_dst;

    OpSemPhiVisitor (SymStore &s, IntLightOpSem &sem, const BasicBlock &dst) :
      OpSemBase (s, sem), m_dst (dst) {}

    void visitPHINode (PHINode &I)
    {
      havoc (I);
      lookup (*I.getIncomingValueForBlock (&m_dst));
    }
  };

}

namespace seahorn
{
  void IntLightOpSem::exec (SymStore &s, const BasicBlock &bb, ExprVector &side)
  {
    OpSemVisitor v(s, *this);
    v.visit (const_cast<BasicBlock&>(bb));
  }

  void IntLightOpSem::exec (SymStore &s, const Instruction &inst, ExprVector &side)
  {
    OpSemVisitor v (s, *this);
    v.visit (const_cast<Instruction&>(inst));
  }


  void IntLightOpSem::execPhi (SymStore &s, const BasicBlock &bb,
                                 const BasicBlock &from, ExprVector &side)
  {
    OpSemPhiVisitor v(s, *this, from);
    v.visit (const_cast<BasicBlock&>(bb));
  }

  void IntLightOpSem::execEdg (SymStore &s, const BasicBlock &src,
                                 const BasicBlock &dst, ExprVector &side)
  {
    exec (s, src, side);
    execPhi (s, dst, src, side);
  }

  void IntLightOpSem::execBr (SymStore &s, const BasicBlock &src, const BasicBlock &dst,
                                ExprVector &side)
  {
    if (const BranchInst* br = dyn_cast<const BranchInst> (src.getTerminator ()))
      if (br->isConditional ()) lookup (s, *br->getCondition ());
  }

  Expr IntLightOpSem::symb (const Value &I)
  {
    assert (I.getType ()->isIntegerTy ());

    Expr v = mkTerm<const Value*> (&I, m_efac);
    if (I.getType ()->isIntegerTy (1))
      v = bind::boolConst (v);
    else
      v = bind::intConst (v);
    return v;
  }

  const Value &IntLightOpSem::conc (Expr v)
  {
    assert (isOpX<FAPP> (v));
    // name of the app
    Expr u = bind::fname (v);
    // name of the fdecl
    u = bind::fname (u);
    assert (isOpX<VALUE> (v));
    return *getTerm<const Value*> (v);
  }


  bool IntLightOpSem::isTracked (const Value &v) {return v.getType ()->isIntegerTy ();}

  Expr IntLightOpSem::lookup (SymStore &s, const Value &v)
  {
    if (const ConstantInt *c = dyn_cast<const ConstantInt> (&v))
    {
      if (c->getType ()->isIntegerTy (1))
        return c->isOne () ? mk<TRUE> (m_efac) : mk<FALSE> (m_efac);
      mpz_class k = toMpz (c->getValue ());
      return mkTerm<mpz_class> (k, m_efac);
    }

    return isTracked (v) ? s.read (symb (v)) : Expr(0);
  }



}