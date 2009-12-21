#pragma once

#include <iostream>

#include "Macros.h"
#include "Expr.h"
#include "IExprVisitor.h"
#include "Ref.h"
#include "String.h"

namespace Finch
{
    using std::ostream;
    
    // AST node for a pair of sequential expressions: "a b . c d"
    class SequenceExpr : public Expr
        {
        public:
            SequenceExpr(Ref<Expr> first, Ref<Expr> second)
            :   mFirst(first),
                mSecond(second)
            {}
            
            Ref<Expr> First()  const { return mFirst; }
            Ref<Expr> Second() const { return mSecond; }
            
            virtual void Trace(ostream & stream) const
            {
                stream << mFirst << "; " << mSecond;
            }
                
            EXPRESSION_VISITOR

        private:
            Ref<Expr> mFirst;
            Ref<Expr> mSecond;
        };
}